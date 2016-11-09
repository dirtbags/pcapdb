/*
 * Created by pflarr on 7/21/15.
 */

function dt_render(data, type, row, meta) {
    /* Render the object data according to it's type.
    This expects the following attributes for each cell object:
      value - The base value of the cell to display.
      header - The header for

     */
    if (type == 'display') {
        var disp_str = '<span ';
        if (data.title) {
            disp_str = disp_str + 'title="' + data.title + '"';
        }
        disp_str = disp_str + '>';
        if (data.value != null) {
            disp_str = disp_str + data.value;
        }
        return disp_str + '</span>';
    } else if (type == 'sort' || type == 'type') {
        // The type is used to determine how to sort and filter.
        if (data.sort) {
            // Parse this as a float if we can.
            return isNaN(data.sort) ? data.sort : parseFloat(data.sort);
        } else {
            return data.value;
        }
    } else {
        return data.value;
    }

}

function dt_ajax_selected(key, url, options) {
    /* Perform an ajax call for the given data table using the selected tables rows.
       Results are posted using the result_alerts function.
    key - The attribute or index of the 'key' column in the table. The value in that column is
          what will be sent to denote the selected rows. Note that due to pagination,
          it doesn't make sense to just send the row index itself.
    url - Where to send the ajax request.
    options - An object with the following optional parameters
        extra_data - The base json for the request as a JS object. If there is anything else the API
                     expects, it should be attributes in this object. You may also pass a
                     function that takes no arguments, and returns an object.
        dest_attr - The name to give the 'rows' when sending the ajax data. Defaults to 'rows'.
        reloader - A function to call to reload the table data. Defaults to the table's reload
                   function.
    */

    if ( options === undefined ) options = {};

    var extra_data = {};
    if (typeof options['extra_data'] === 'function') {
        extra_data = options['extra_data']();
    } else if (options['extra_data'] !== undefined) {
        extra_data = options['extra_data'];
    }

    var dest_attr = 'rows';
    if (options['dest_attr'] !== undefined)
        dest_attr = options['dest_attr'];

    return function (event, dt, node, config) {
        var selected_rows = dt.rows('.selected').data();
        var keys = [];
        var row, i;
        for (i=0; i < selected_rows.length; i++) {
            row = selected_rows[i];
            keys.push(row[key]);
        }

        // Copy the attributes from the given json into our request.
        var json_data = {};


        for (var attr in extra_data) {
            if (extra_data.hasOwnProperty(attr)) {
                json_data[attr] = extra_data[attr];
            }
        }

        json_data[dest_attr] = keys;

        // Default to the datatables reload function
        var reloader = dt.ajax.reload;
        if (options['reloader'] !== undefined) {
            reloader = options['reloader'];
        }

        $.ajax(url,
                {
                    contentType: 'application/json',
                    data: JSON.stringify(json_data),
                    headers : {'X-CSRFToken': CSRF_TOKEN},
                    method: 'POST',
                    success: [result_alerts,
                      function (a,b,c) {
                          console.log(a);

                        reloader();
                    } ]

                });
    }
}