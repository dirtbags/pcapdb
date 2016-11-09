function toggle_panel_body(element) {
    var panel_body = element.next();
    // Edit the classes
    element.find('i.toggle-class').toggleClass('fa-minus-circle txt-color-red');
    element.find('i.toggle-class').toggleClass('fa-plus-circle txt-color-green');

    panel_body.toggle();
}

// Global DOCUMENT READY BLOCK, use with care.
$(document).ready(function(){
    // The page title prefix
    var page_prefix = "PcapDB | ";

    // Set the page title everywhere
    document.title = page_prefix += $(".page-header").text().trim();
});

var _ALERT_TYPES = ['success', 'info', 'warning', 'danger'];
var _ALERT_ICONS = {'success': 'fa-ok',
                    'info': 'fa-envelope',
                    'warning': 'fa-lightning',
                    'danger': 'fa-exclamation'};

function show_alert(message, atype) {
    /* Pop up an alert in the alert area of the page. The alerts are set to close on click. */
    var dest = $("#alert-list");

    var alert_li = $(document.createElement("li"));
    var alert_div = $(document.createElement("div"));
    alert_li.append(alert_div);

    alert_li.hide();

    if ($.inArray(atype, _ALERT_TYPES) != -1) {
        alert_div.addClass('navbar-alert-' + atype);
        var alert_icon = $(document.createElement("i"));
        alert_icon.addClass("fa " + _ALERT_ICONS[atype]);
        alert_div.append(alert_icon);
    }
    alert_div.addClass('navbar-alert navbar-alert-highlight');
    alert_div.attr('title', 'Click to dismiss.');

    alert_div.append($(document.createTextNode(' ' + message)));

    alert_li.click(clear_alerts_callback(alert_li));

    dest.append(alert_li);
    alert_li.fadeIn();
}

function clear_alerts_callback(alerts_sel) {
    // Returns a function that will clear all the given alerts when called.
    // alerts - A jquery object referencing the alert components (li) that need to be
    //          cleared.
    "use strict";

    return function (event) {
        var alerts = $(alerts_sel);
        event.stopPropagation();
        alerts.slideUp(400, function () {alerts.remove()});
        return false;
    }
}

function result_alerts(results, status, jqXHR) {
    /* Check the results object for various alert attributes, and display the contents of each as
    appropriate alerts.
     */

    for (var i=0; i < _ALERT_TYPES.length; i++) {
        var atype = _ALERT_TYPES[i];
        if (results[atype] != undefined) {
            var alerts = results[atype];
            if (!Array.isArray(alerts)) {
                alerts = [alerts];
            }

            for (var j=0; j < alerts.length; j++) {
                show_alert(alerts[j], atype);
            }
        }
    }

    // Also check for any task update that may have resulted from this action.
    task_check();
}

// Pages may add extra callbacks to this list to enable additional features based on task results.
var TASK_ACTION_STACK = [set_tasks];

// How often, in seconds, to check for new or updated tasks.
var _TASK_CHECK_PERIOD = 1000;
var _TASK_CHECK_PERIOD_MIN = 1000;
var _TASK_CHECK_PERIOD_MAX = 8000;
var _TASK_CHECK_PERIOD_MULT = 2;
var _TASK_TIMEOUT_ID = null;
function task_check(reset_period) {

    if (reset_period) {
        _TASK_CHECK_PERIOD = _TASK_CHECK_PERIOD_MIN;
    }

    // If we were supposed to run this later, cancel it.
    if (_TASK_TIMEOUT_ID != null) {
        clearTimeout(_TASK_TIMEOUT_ID);
    }

    // TASK_URL and CSRF_TOKEN are expected to be defined in the master template.
    var data = 'csrfmiddlewaretoken=' + CSRF_TOKEN;
    $.ajax({
        method: 'GET',
        url: TASK_URL,
        data: data,
        success: TASK_ACTION_STACK});

}

var _TASKS_BY_ID = {};
var _TASK_ICONS = {
    'PENDING': 'fa-circle-o-notch fa-spin',
    'STARTED': 'fa-cog fa-spin',
    'RETRY':   'fa-refresh',
    'FAILURE': 'fa-exclamation-triangle',
    'SUCCESS': 'fa-check-circle',
    'other':   'fa-cog fa-spin'
};
var _TASK_ICONS_ALL = 'fa-circle-o-notch fa-spin fa-cog fa-refresh fa-exclamation-triangle' +
                      'fa-check-circle';
var _TASK_CLASS = {
    'PENDING': 'navbar-alert-info',
    'STARTED': 'navbar-alert-warning',
    'RETRY':   'navbar-alert-danger',
    'FAILURE': 'navbar-alert-danger',
    'SUCCESS': 'navbar-alert-success',
    'other':   'navbar-alert-warning'
};
var _TASK_CLASS_ALL = 'navbar-alert-info navbar-alert-warning navbar-alert-danger ' +
                      'navbar-alert-success';
function Task(data) {
    "use strict";

    // TODO - This isn't worth getting into now, but this really needs to be set up
    // TODO - so that all the old elements are reused rather than recreated at each update.

    // Create the basic element in which this task's contents will reside.
    this.box = $(document.createElement('li'));
    this.box.hide();
    this.div = $(document.createElement('div'));
    this.div.addClass('navbar-alert');
    this.box.append(this.div);

    // Needed for callback function.
    var task = this;

    this.icon = $(document.createElement('i'));
    this.icon.addClass('fa fa-lg fa-fw task-icon');
    this.icon.attr('title', 'Clear this task');
    this.icon.click(function() { task.clear(task) });
    this.div.append(this.icon);

    this.descr_p = $(document.createElement('strong'));
    this.div.append(this.descr_p);

    this.msg_p = $(document.createElement('p'));
    this.div.append(this.msg_p);

    this.update = function (data) {
        var new_status = undefined;

        if (this.id == undefined) this.id = data['task_id'];

        this.started = data['started'];
        this.descr = data['descr'];
        this.descr = data['descr'];
        if (data['task'] == undefined) {
            this.task = {};
        } else {
            this.task = data['task'];
            this.result = data['task']['result'];
            this.meta = data['task']['meta'];
            new_status = data['task']['status'];
        }
        if (this.result == null || this.result == undefined) this.result = {};
        if (this.meta == null || this.meta == undefined) this.meta = {};

        console.log(data);
        this.link = this.result['link'];
        this.msg = this.result['msg'];

        if (this.status == undefined || this.status != new_status) {
            this.status = new_status;
            if (_TASK_CLASS[this.status] != undefined) {
                this.div.removeClass(_TASK_CLASS_ALL).addClass(_TASK_CLASS[this.status]);
                this.icon.removeClass(_TASK_ICONS_ALL).addClass(_TASK_ICONS[this.status]);
            } else {
                this.div.removeClass(_TASK_CLASS_ALL).addClass(_TASK_CLASS['other']);
                this.icon.removeClass(_TASK_ICONS_ALL).addClass(_TASK_ICONS['other']);
            }
        }
        // These have to be set after the above if statement
        this.status = data['status'][0];
        this.progress = data['status'][1];

        this.descr_p.empty();
        var descr = this.descr.slice(0,30);
        if (this.progress != undefined && this.status != 'SUCCESS') {
            descr = '(' + this.progress + ') ' + descr;
        }
        this.descr_p.append(document.createTextNode(descr));
        this.descr_p.attr('title', '(' + this.status + ') ' + this.descr);

        if (this.link != undefined || this.msg != undefined) {
            var message_el;
            if (this.link == undefined) {
                message_el = $(document.createElement('span'));
            } else {
                message_el = $(document.createElement('a'));
                message_el.attr('href', this.link);
            }

            if (this.msg == undefined) {
                message_el.append(document.createTextNode('Results'));
            } else {
                message_el.append(document.createTextNode(this.msg));
            }

            this.msg_p.empty();
            this.msg_p.append(message_el);
        }
    };

    this.clear = function (task) {
        // Tell the server to stop showing notifications for this task.
        var data = 'csrfmiddlewaretoken=' + CSRF_TOKEN;
        data += '&task=' + task.id;
        $.ajax({
            method: 'POST',
            url: TASK_URL,
            data: data});

        // Remove the task from the page.
        task.delete();
    };

    this.delete = function () {
        // Fadout this task, then remove it from the DOM.
        var box = this.box;
        box.slideUp(400, function () {box.remove()});
    };

    this.insert = function (after) {
        var first_add = true;
        if (this.box.parent().length != 0) {
            // This is currently inserted.
            this.box.detach();
            first_add = false;
        }


        // Insert this task, and have it fade in.
        if (after == null) {
            $('#task-list').append(this.box);
        } else {
            this.box.insertAfter(after.box);
        }

        if (first_add == true) {
            this.box.fadeIn(1000);
        }
    };

    this.update(data);

    return this;
}

function get_task(data) {
    "use strict";
    // If the page already knows about this task, then just return the existing task.

    var old_task = _TASKS_BY_ID[data['task_id']];
    if (old_task != undefined) {
        if (old_task.status != 'SUCCESS' &&
                old_task.status != 'FAILURE' &&
                old_task.status != 'RETRY') {
            old_task.update(data);
        }
        return old_task;
    }

    return new Task(data);
}

function set_tasks(tasks, status, jqXHR) {
    var _new_tasks = {};
    var _new_tasks_ordered = [];
    var task, task_id, i;

    // Build a new dictionary of tasks.
    for (i=0; i < tasks.length; i++) {
        task = get_task(tasks[i]);
        _new_tasks[task.id] = task;
        _new_tasks_ordered.push(task);
    }

    // Remove any tasks that aren't in the new set.
    for (task_id in _TASKS_BY_ID) {
        if (_new_tasks[task_id] == undefined) {
            task = _TASKS_BY_ID[task_id];
            task.delete()
        }
    }

    _TASKS_BY_ID = _new_tasks;

    var has_running = false;
    var last = null;
    for (i=0; i < _new_tasks_ordered.length; i++) {
        // Add or move all the tasks in the task list so that they're in the order in which
        // they were given. (Which should be sorted by start time.)
        task = _new_tasks_ordered[i];
        task.insert(last);

        // Check for a task with any 'non-finished' state.
        switch (task.status) {
            case 'SUCCESS':
            case 'FAILURE':
            case 'RETRY':
                break;
            default:
                has_running = true;
        }
    }

    // If we find such a task, set the check timeout to the minimum.
    // Otherwise, apply the multiplyer.
    if (has_running) {
        _TASK_CHECK_PERIOD = _TASK_CHECK_PERIOD_MIN;
    } else {
        _TASK_CHECK_PERIOD *= _TASK_CHECK_PERIOD_MULT;
        if (_TASK_CHECK_PERIOD > _TASK_CHECK_PERIOD_MAX)
            _TASK_CHECK_PERIOD = _TASK_CHECK_PERIOD_MAX;
    }

    // Run our task check again later, depending on how frequently we'd been getting tasks.
    _TASK_TIMEOUT_ID = setTimeout(task_check, _TASK_CHECK_PERIOD);

}

function clear_tasks(event) {
    // Acknowledge and clear all the tasks.
    "use strict";
    event.stopPropagation();

    var data = 'csrfmiddlewaretoken=' + CSRF_TOKEN;

    for (var task_id in _TASKS_BY_ID) {
        if (!_TASKS_BY_ID.hasOwnProperty(task_id)) continue;
        var task = _TASKS_BY_ID[task_id];
        task.delete();
        data += '&task=' + task_id;
    }

    $.ajax({
        method: 'POST',
        url: TASK_URL,
        data: data});

    return false;
}