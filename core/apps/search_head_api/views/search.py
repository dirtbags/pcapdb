import json
import os
import subprocess as sp
import tempfile
import uuid

from celery import chord
from django.http import Http404, HttpResponse
from django.db import transaction
from django.db.models import Q
from django.core.files import File
from django.views.decorators.csrf import csrf_exempt
from rest_framework.authentication import SessionAuthentication
from rest_framework.response import Response
from rest_framework.renderers import JSONRenderer, BrowsableAPIRenderer
from rest_framework.parsers import FileUploadParser
from rest_framework.views import APIView
from rest_framework import serializers
from wsgiref.util import FileWrapper

from apps.capture_node_api.lib.search import parse
from apps.capture_node_api.tasks.search import search_node
from apps.search_head_api.models.search import NodeSearch, SearchSite, SearchResult
from apps.search_head_api.models import CaptureNode, Site, SearchInfo
from apps.search_head_api.views import SearchHeadAPIView
from apps.search_head_api.tasks import search_merge
from apps.task_api.models import TaskTrack

from libs.view_helpers import format_errors

from django.conf import settings

import logging
log = logging.getLogger(__name__)

__author__ = 'pflarr'


class Search(SearchHeadAPIView):
    """Starts a search of flows on the capture_nodes."""

    class PostSerializer(serializers.Serializer):
        _VALID_PROTOCOLS = tuple(settings.SEARCH_TRANSPORT_PROTOS.items())

        sites = serializers.ListField(child=serializers.IntegerField())
        action = serializers.ChoiceField((SearchInfo.T_FLOW, SearchInfo.T_PCAP))
        query = serializers.CharField()
        proto = serializers.ChoiceField(_VALID_PROTOCOLS)
        start = serializers.DateTimeField()
        end = serializers.DateTimeField()

    @transaction.atomic
    def post(self, request):
        """Perform a search over flows on all capture_nodes. The expected result (delivered via
        a separate file request) is a flow result file.
        :param request: Request
        :return:
        :rtype: Response
        """

        log.info("search start: %s", request.data.get('start_time'))

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        data = ser.validated_data

        for key in data.keys():
            log.info(key)

        user_groups = request.user.groups.all()

        # Make sure the search can be executed.
        check_res = search_syntax_check(data['query'])
        if 'ps_error_msg' in check_res:
            check_res['warning'] = 'Invalid Search'
            return Response(data=check_res)

        # Get all the asked for sites that the user has permission to search.
        sites_q = Q(group__in=user_groups) | Q(admin_group__in=user_groups)
        if data['sites']:
            q = Q()
            for site_id in data['sites']:
                q = q | Q(id=site_id)
            sites_q = sites_q & q

        sites = Site.objects.filter(sites_q)

        # Get a list of all active capture_nodes.
        capnodes = CaptureNode.objects.filter(site__in=sites)
        if len(capnodes) == 0:
            return Response(data={'warning': 'You lack the permissions to search any of '
                                             'the sites selected.'})

        search_info = SearchInfo(type=data['action'],
                                 proto=data['proto'],
                                 start=data['start'],
                                 end=data['end'],
                                 query=data['query'])
        search_info.save()

        # Associate each of the searched sites with the search.
        for site in sites:
            search_site = SearchSite(search=search_info,
                                     site=site)
            search_site.save()

        ser_start = data['start'].isoformat()
        ser_end = data['end'].isoformat()

        subtasks = []
        for capnode in capnodes:
            node_search = NodeSearch(search=search_info,
                                     token=uuid.uuid4(),
                                     capture_node=capnode)
            node_search.save()
            args = [data['query'],
                    ser_start,
                    ser_end,
                    data['proto'],
                    node_search.post_url()]
            kwargs = {'packets': data['action'] == SearchInfo.T_PCAP}

            task = search_node.subtask(args=args,
                                       kwargs=kwargs,
                                       options={'queue': capnode.hostname})
            subtasks.append(task)

        # Create the final merge task to combine the results from the capture_nodes.
        callback_task = search_merge.subtask(args=[search_info.id],
                                             options={'queue': 'search_head'})
        # This will execute the subtasks, then the callback task when those are done.
        task_group = chord(subtasks, callback_task)
        result = task_group.apply_async(queue='search_head')

        if data['action'] == SearchInfo.T_PCAP:
            message = "Fetching PCAP File"
        else:
            message = "Performing Flow Search"
        TaskTrack.track(result, message, request)

        response_data = {'success': 'Search Started'}
        response_data.update(check_res)

        if data['action'] == SearchInfo.T_FLOW:
            # Hand back the task id to watch for completion, and the URL to use
            # to get the results. This is only needed for flow searches.
            response_data['task_id'] = result.task_id
            response_data['results_url'] = search_info.flow_results_url
        return Response(data=response_data)


class ParseSearch(SearchHeadAPIView):
    """Does a test parsing of a search string."""

    def post(self, request):
        """
        Parse the given search string ('search' post variable) and return a Response with the
        following data (* are optional:
        :param request:
        :return: A Response object
        """
        query = request.data.getlist('query')
        if not query:
            return Response(data={'warning': 'No query given.'})

        return Response(data=search_syntax_check(query[0]))


class PutNodeSearchResult(APIView):
    """For allowing the capture nodes to upload their results. Authentication is via
    a random, view related token."""

    class CsrfExemptSessionAuthentication(SessionAuthentication):
        def enforce_csrf(self, request):
            # Don't enforce CSRF protection for this view.
            return None

    renderer_classes = (JSONRenderer,)
    permission_classes = []
    parser_classes = (FileUploadParser,)

    def put(self, request, token):

        # Make sure we expect this result.
        try:
            node_search = NodeSearch.objects.get(token=token, file='')
        except NodeSearch.DoesNotExist:
            raise Http404
        except Exception as err:
            log.info("Other exc: {}".format(err))
            raise
        log.info('request data keys: {}'.format(request.data.keys()))

        if 'file' in request.data:
            data = request.data['file']
        else:
            # If we don't get a file, that means the search results were empty.
            data = File(open('/dev/null', 'rb'))

        node_search.file.save(str(uuid.uuid4()), data)
        node_search.file.close()

        path = node_search.file.path
        log.info("Stored: {}, size: {}, exists? {}".format(path, os.path.getsize(path),
                                                           os.path.exists(path)))

        return Response(status=204)


def search_syntax_check(search):
    """Syntax check the given search string. If there was an error,
    the 'ps_error_msg' key will be set in the returned dict. The returned dict will contain
    the following key/value pairs (* are optional):
      ps_search: The original search text.
      ps_tokens: A list of successfully parsed tokens
      ps_error_msg*: An error message to deliver to the user. Only set on error.
      ps_error_token*: A serialized token of where an error occured.
    :param search: str
    :rtype: dict
    """
    if not search:
        return {'ps_search': None,
                'ps_tokens': [],
                'ps_error_msg': 'No search given'}

    tokens = []
    token_data = []
    data = {'ps_search': search,
            'ps_tokens': token_data}

    try:
        for tok in parse.tokenize(search):
            tokens.append(tok)
            token_data.append(tok.serial_data())
    except parse.ParseError as err:
        data['ps_error_msg'] = err.args[0]
        data['ps_error_token'] = err.args[1].serial_data()

    if 'ps_error_msg' not in data:
        try:
            tree = parse.build_tree(iter(tokens))
            if tree is not None:
                tree = tree.normalize()
                tree.prune()
        except parse.ParseError as err:
            data['ps_error_msg'] = err.args[0]
            data['ps_error_token'] = err.args[1].serial_data()

    return data


class ResultView(SearchHeadAPIView):
    NULL_PATH = '/dev/null'

    def get(self, request, search_id, site=None):

        try:
            search = SearchInfo.objects.get(id=search_id)
        except SearchInfo.DoesNotExist:
            raise Http404

        if not search.has_permission(request.user):
            return Response(data={'warning': 'You don\'t have the permissions to view results from'
                                             'sites involved in these results.'})

        # Get the file that corresponds to the site we're looking for
        # The None/NULL site is all sites combined.
        try:
            result = search.results.get(site=site)
        except SearchResult.DoesNotExist:
            raise Http404

        fn = []
        site_count = search.sites.all()
        if site_count == 1:
            fn.append(site.name)
        else:
            fn.append('multi-site')

        fn.append(search.start.strftime('%m:%d_%h%m'))
        fn.append(search.end.strftime('%m:%d_%h%m'))

        if search.type == search.T_FLOW:
            fn.append('.flows')
        else:
            fn.append('.pcap')



        size = os.path.getsize(result.file.path)

        file = FileWrapper(open(result.file.path, 'rb'), 8192)
        response = HttpResponse(file, content_type='application/octet-stream')
        response['Content-Length'] = size
        response['Content-Disposition'] = 'attachment; filename={}'.format(''.join(fn))

        return response


class FlowResultView(SearchHeadAPIView):
    """Grab flow search results (by task_id) and return them as json."""

    # Mapping of client side field names to those compatible with
    # read_flows. Also limits the acceptable field names we pass to the command line.
    order_fields = {'start_ts': 'start_ts',
                    'end_ts': 'end_ts',
                    'src_ip': 'src_ip',
                    'src_port': 'src_port',
                    'dst_ip': 'dst_ip',
                    'dst_port': 'dst_port',
                    'size': 'size',
                    'packets': 'packets'}

    def get(self, request, search_id):
        """
        Get the results for the given task_id.
        :param request: Request
        :param int search_id: The id of the search we're looking for.
        :rtype: {}
        :return:
        """

        params = request.query_params

        try:
            search = SearchInfo.objects.get(id=search_id)
        except SearchInfo.DoesNotExist:
            raise Http404

        if not search.has_permission(request.user):
            return Response(data={'warning': 'You don\'t have the permissions to view results from'
                                             'sites involved in these results.'})

        if search.type != search.T_FLOW:
            raise Http404

        results = search.results.all()
        if len(results) == 0:
            return Response(data={'warning': "No results for this search.",
                                  'data': {}})
        if len(results) > 1:
            log.error("Invalid result set for flows. More than one result file.")
            return Response(status=200, data={'warning': 'Error retreiving results.'})

        result = results[0]

        cmd = [settings.SITE_ROOT/'bin'/'read_flows', result.file.path]

        # Get the direction of the order column
        if 'order[0][dir]' in params:
            if params["order[0][dir]"] == "asc":
                order = "-a"
            else:
                order = "-d"

            # Figure out the name of the order column
            order_index = params['order[0][column]']
            order_field_param = 'columns[' + order_index + '][name]'
            order_field = self.order_fields.get(params.get(order_field_param))
            if order_field is not None:
                cmd.extend([order, order_field])

        if 'length' in params:
            try:
                window_size = int(params['length'])
            except ValueError:
                return Response(data={'warning': "Invalid Window size."})
            if int(window_size) < 1:
                return Response(data={'warning': 'Table size cannot be empty.'})
            cmd.extend(['-w', str(window_size)])

        if 'start' in params:
            try:
                cmd.extend(['-s', str(int(params['start']))])
            except ValueError:
                return Response(data={'warning': 'Invalid start position.'})

        proc = sp.Popen(cmd, stdout=sp.PIPE, stderr=sp.DEVNULL)
        stdout, _ = proc.communicate()

        try:
            data = json.loads(stdout.decode('utf-8'))
        except ValueError:
            log.info(stdout.decode('utf-8'))
            raise
        # Data tables needs this to tell the difference between the total number of records
        # and the total number of records remaining.
        data['recordsFiltered'] = data['recordsTotal']
        if 'draw' in params:
            data['draw'] = params['draw']

        return Response(status=200, data=data)
