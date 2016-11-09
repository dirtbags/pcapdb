from django.db import models
from collections import defaultdict
import re
from rest_framework.response import Response
from rest_framework.serializers import Serializer
from rest_framework.views import APIView

__author__ = 'pflarr'


class DataTableAPIView(APIView):
    """
    A base API view that expects values as sent by the jquery DataTables library. It returns a
    json object with a 'data' element that a list of resulting rows.

    Usage:
        Inherit from this class instead of APIView, and override the following class variables.

        REQUIRED:
        model - The model from which to build the table.
        serializer - The serializer class for the given model


        presented_columns - The columns/attributes you want to return to the table, in the order
                            you want to return them. These should be strings in the format
                            expected by django filter operations if they are actual columns.
                            Other, non-SQL searchable/orderable attributes can be in whatever
                            format you want. This can also include columns from linked tables. This
                            defaults to all of the model's columns, sorted. (sequence)

        searchable_columns - Columns that can and should be searchable. This should only include
                             real columns. This defaults to all presented columns. The client can
                             disable a column's searchability, but not enable it. (sequence)

        orderable_columns - Columns that can and should be orderable, as per searchable_columns.

        allow_regex_search - If True, perform regex searches instead of regular searches when
                            asked. Defaults to False.

        column_callbacks - A dictionary of callbacks.
                            It is assumed that the name in presented columns is the same as the
                            attribute name for that column in the serializer. If that is not
                            true, or if the data for a column must be accessed through other
                            means, then a callback function should be added here.
                            callback(modelInstance, serializer_data, column_name) -> column_value

        prefetch - A sequence of fields that should be prefetched using the prefetch_related
                            query set method. If this is empty (default), no prefetching is done.

        select_related - As per prefetch, except using the 'select_related' method.

        queryset_base - A method that takes the view (self), request, and the base queryset,
                        and returns a new queryset. The purpose of this is to provide for
                        initial prefilter conditions, such as filtering by User, that couldn't
                        otherwise be done.


    Example:

    class MyView(DataTableAPIView):
        model = MyModel
        serializer = MySerializer
        presented_columns = ['id', 'col1', 'col2', # Real model fields
                             'attr1',              # A serializer attribute. No callback needed.
                             'fake_col1', 'fake_col2']          # Generated data.
        searchable_columns= ['id', 'col1', 'col2']
        orderable_columns = searchable_columns
        column_callbacks = {'fake_col1': lambda row, data, col:

    """

    # Required
    model = models.Model
    serializer = Serializer

    # Optional
    presented_columns = None
    searchable_columns = None
    orderable_columns = None
    allow_regex_search = False
    column_callbacks = {}
    prefetch_related = []
    select_related = []

    def queryset_base(self, request, query_set):
        return query_set

    columns_re = re.compile(r'columns\[(?P<col_num>\d+)\]'
                            r'\[(?P<col_key>data|name|searchable|orderable|search)\]'
                            r'(?:\[(?P<col_search_key>value|regex)\])?')
    order_re = re.compile(r'order\[(?P<col_num>\d+)\]'
                          r'\[(?P<type>column|dir)\]')
    search_re = re.compile(r'search\[(?P<search_key>value|regex)\]')

    def __init__(self, **kwargs):
        super(APIView, self).__init__(**kwargs)

        # Make sure the _meta class is sane, and set defaults for missing attributes.
        # I could do this in the base Meta classes init, but I don't want people to have
        # to inherit from the base.
        if self.model is models.Model:
            raise NotImplementedError("You must provide a model.")

        # If this has the dummy serializer or no serializer, just set the serializer to None
        if self.serializer is Serializer:
            raise NotImplementedError("You must provide a serializer.")

        if self.presented_columns is None:
            self.presented_columns = sorted(self.model.Meta.get_all_field_names())

        if self.searchable_columns is None:
            self.searchable_columns = self.presented_columns

        if self.orderable_columns is None:
            self.orderable_columns = self.presented_columns

    def get(self, request):
        errors = []
        ret = {'draw': int(request.query_params['draw'])}
        try:
            limit = int(request.query_params['length'])
            if limit < -1:
                raise ValueError("Invalid query length {}.".format(limit))
            offset = int(request.query_params['start'])
            if offset < 0:
                raise ValueError("Invalid offset {}.".format(offset))

            columns = defaultdict(lambda: {})
            search = {}
            order = defaultdict(lambda: {})
            for key in request.query_params.keys():
                col_match = self.columns_re.match(key)
                val = request.query_params[key].strip()
                # Skip empty values
                if not val:
                    continue

                if col_match is not None:
                    gdict = col_match.groupdict()
                    # This is a column key
                    if gdict['col_key'] != 'search':
                        columns[int(gdict['col_num'])][gdict['col_key']] = val
                    else:
                        columns[int(gdict['col_num'])][gdict['col_search_key']] = val
                    continue

                ord_match = self.order_re.match(key)
                if ord_match is not None:
                    gdict = ord_match.groupdict()
                    order[int(gdict['col_num'])][gdict['type']] = val
                    continue

                search_match = self.search_re.match(key)
                if search_match is not None:
                    gdict = search_match.groupdict()
                    search[gdict['search_key']] = val

            query = models.Q()
            if 'value' in search:
                # Apply the global search values to all searchable columns
                # Note: Multiple search items are subtractive. To be in the match set, a  row must
                # have at least one searchable column match each search term.
                values = search['value'].split()
                for value in values:
                    sub_query = models.Q()
                    for col_idx in range(len(self.searchable_columns)):
                        col_name = self.presented_columns[col_idx]
                        if (search.get('regex', 'false') == 'true' and
                                self.allow_regex_search and
                                columns[col_idx].get('searchable', 'true')):
                            # Build a regex search param.
                            fkey = '{}__regex'.format(col_name)
                        else:
                            fkey = '{}__contains'.format(col_name)
                        sub_query = sub_query | models.Q(**{fkey: value})
                    query = query & sub_query

            # Apply column by column searches
            for col_idx in columns:
                search_key = columns[col_idx].get('value', '')
                col_name = self.presented_columns[col_idx]

                # Only search if it actually has a search key and both the table and the server
                # say this is a searchable column
                if not (search_key and
                        col_name in self.searchable_columns and
                        columns[col_idx].get('searchable', 'false') == 'true'):
                    continue

                # Do a regex search if asked for and allowed.
                if (self.allow_regex_search and
                        columns[col_idx].get('regex', 'false') == 'true'):
                    fkey = '{}__regex'.format(col_name)
                else:
                    fkey = '{}__exact'.format(col_name)
                query = query & models.Q(**{fkey: search_key})

            # Apply ordering
            ord_steps = []
            for ord_idx in sorted(order.keys()):
                if self.presented_columns[ord_idx] not in self.orderable_columns:
                    continue

                ord_dir = order[ord_idx].get('dir', 'desc')
                # Make sure the ordering direction makes sense
                if ord_dir not in ('asc', 'desc'):
                    errors.append("Invalid ordering direction {}.".format(ord_dir))
                    continue

                # The column is refered to by index. It is assumed that the table order
                # and the order in presented_columns match.
                col_idx = int(order[ord_idx]['column'])
                col = self.presented_columns[col_idx]
                if ord_dir == 'desc':
                    col = '-{}'.format(col)
                ord_steps.append(col)

            # Turn our Q objects into a proper query set
            query_set = self.model.objects.filter(query)

            # Run our query_set through our basic filter. This does nothing by default.
            query_set = self.queryset_base(request, query_set)

            # Set the sort order for the query set
            query_set = query_set.order_by(*ord_steps)

            ret['recordsTotal'] = self.model.objects.count()
            ret['recordsFiltered'] = query_set.count()

            # Only return the slice/page of results asked for (-1 denote return all)
            if limit != -1:
                query_set = query_set[offset:offset+limit]

            if self.prefetch_related:
                query_set = query_set.prefetch_related(*self.prefetch_related)

            if self.select_related:
                query_set = query_set.select_related(*self.prefetch_related)

            data = []
            # Use the included serializer to get the data.
            for row in query_set:
                ser_data = self.serializer(row).data
                out_row = []
                for col_name in self.presented_columns:
                    if col_name in self.column_callbacks:
                        #
                        callback = self.column_callbacks[col_name]
                        out_row.append(callback(row, ser_data, col_name))
                    elif '__' in col_name:
                        sub_dict = ser_data
                        name_stack = col_name.split('__')
                        next_name = name_stack.pop(0)
                        while name_stack:
                            sub_dict = sub_dict.get(next_name, None)
                            if sub_dict is None:
                                break
                            next_name = name_stack.pop(0)
                        if sub_dict is not None:
                            out_row.append(sub_dict.get(next_name, 'error'))
                        else:
                            out_row.append('error')
                    else:
                        out_row.append(ser_data.get(col_name, 'error'))

                data.append(out_row)

            print(data[0])

            ret['data'] = data

        except (KeyError, IndexError, ValueError) as err:
            errors.extend(err.args)

        if errors:
            ret['errors'] = errors

        return Response(data=ret)
