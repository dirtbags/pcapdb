import logging
from traceback import format_exc

from celery.exceptions import TimeoutError
from django.contrib.auth.models import Group
from django.db import IntegrityError
from django.http import Http404
from rest_framework import serializers
from rest_framework.exceptions import PermissionDenied
from rest_framework.response import Response

from apps.capture_node_api.tasks import device as device_tasks
from apps.capture_node_api.tasks import interface as iface_tasks
from apps.capture_node_api.tasks import status as status_tasks
from apps.search_head_api.models import CaptureNode
from apps.search_head_api.serializers import CaptureNodeSerializer
from apps.search_head_api.views.base import SearchHeadAPIView, SearchHeadAPIAdminView
from apps.task_api.models import TaskTrack, ephemeral_task_cleanup
from libs.view_helpers import format_errors

log = logging.getLogger(__name__)


class CaptureNodeAdminView(SearchHeadAPIView):
    def initial(self, request, *args, **kwargs):
        """We have some special permissions checking to do that can't be done with
        standard permissions classes, since those don't get the URL args of the request."""

        log.info("Heya: {} {}".format(kwargs.get('capture_node'), 'bleh'))

        if 'capture_node' not in kwargs:
            log.error("Use capture node admin based auth on a view that doesn't get"
                      "a capture node kwarg.")
            raise RuntimeError

        try:
            capnode_id = int(kwargs['capture_node'])
            capnode = CaptureNode.objects.prefetch_related('site').get(id=capnode_id)
        except (CaptureNode.DoesNotExist, ValueError):
            # Raise a 404 for invalid capture nodes.
            raise Http404

        try:
            request.user.groups.get(id=capnode.site.admin_group_id)
        except Group.DoesNotExist:
            raise PermissionDenied

        super().initial(request, *args, **kwargs)


class CaptureNodesView(SearchHeadAPIAdminView):
    timeout = 3

    def get(self, request, capture_node=None):
        """Return the list of capture_nodes.
        :param Request request:
        URL Args:
            capture_node: The id of the status for a specific capture_node. Default (None) is all.
        """

        if capture_node is None:
            capture_nodes = CaptureNode.objects.all()
        else:
            try:
                capture_node = int(capture_node)
            except ValueError:
                return Response(data={"Invalid capture_node id: {}".format(capture_node)})

            try:
                capture_nodes = [CaptureNode.objects.get(id=capture_node)]
            except CaptureNode.DoesNotExist:
                return Response(status=404)

        capture_node_info = []
        tasks = {}
        for capnode in capture_nodes:
            tasks[capnode.hostname] = status_tasks.get_status.apply_async(queue=capnode.hostname)

        for capnode in capture_nodes:
            ser_data = CaptureNodeSerializer(capnode).data
            data = tasks[capnode.hostname].get(timeout=self.timeout).get('data')
            if hasattr(data, 'get'):
                ser_data['state'] = data.get('state')
            else:
                ser_data['state'] = None
            ser_data['is_admin'] = request.user.extra.is_admin_for(capnode)
            ephemeral_task_cleanup(tasks[capnode.hostname])
            capture_node_info.append(ser_data)

        return Response(status=200,
                        data={'data': capture_node_info})

    # TODO: Move this to a separate view controlled by site admin
    def post(self, request, capture_node=None):
        """Change the current running state of the capture_node with the given capture_node.
        URL Args:
          - capture_node: The id of the effected capture_node. If None, change the state of all capture_nodes.
        """

        if capture_node is None:
            capture_nodes = CaptureNode.objects.all()
        else:
            try:
                capture_node = int(capture_node)
            except ValueError:
                return Response(data={"Invalid capture_node id: {}".format(capture_node)})

            try:
                capture_nodes = [CaptureNode.objects.get(id=capture_node)]
            except CaptureNode.DoesNotExist:
                return Response(status=404)

        state = request.data.getlist('state')
        if len(state) == 0 or state[0] not in status_tasks.CAPTURE_STATES:
            return Response(data={'warning': 'Invalid capture state given.'})
        state = state[0]
        log.info(state)

        tasks = {}
        for idx in capture_nodes:
            tasks[idx.hostname] = status_tasks.set_capture_state\
                                              .apply_async([state], queue=idx.hostname)

        context = {}
        for idx in capture_nodes:
            result = tasks[idx.hostname].get(timeout=self.timeout)
            for level in 'success', 'info', 'warning', 'danger':
                if level in result:
                    msgs = context.get(level, [])
                    new_msgs = result.get(level)
                    if type(new_msgs) not in (list, tuple):
                        new_msgs = [new_msgs]
                    msgs.extend(new_msgs)
                    context[level] = msgs
            ephemeral_task_cleanup(tasks[idx.hostname])

        return Response(data=context)


class CaptureNodeAddView(SearchHeadAPIAdminView):
    """Add a new capture node."""

    PostSerializer = CaptureNodeSerializer

    def post(self, request):
        """Attempt to add an capture_node to our list of capture_nodes. The capture_node being added must be
        contactable and have its ident view available.
        """

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        try:
            ser.save()
        except IntegrityError:
            return Response("Database error when adding new capture node.")

        return Response(data={'success': 'CaptureNode {} added successfully.'
                                         .format(ser.data['hostname'])})


class CaptureNodeRemoveView(SearchHeadAPIAdminView):
    def post(self, request, capture_node):
        """Remove the capture_node with the given id from our list of capture_nodes.
        URL Param:
          - capture_node: The id of the capture_node to be removed."""

        try:
            capture_node = int(capture_node)
        except ValueError:
            return Response(data={'warning': "Invalid CaptureNode id."})

        try:
            idx = CaptureNode.objects.get(id=capture_node)
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        hostname = idx.hostname
        idx.delete()

        return Response(data={'success': 'CaptureNode {} removed.'.format(hostname)})


class CaptureNodeSettingsView(CaptureNodeAdminView):
    """Set any of the given general settings values on the capture_node. To see what the settings
    are currently set to, use the capture_node state view.
    URL args:
        - capture_node: An capture_node id
    POST data:
        <Insert settings here>
    """
    timeout = 5

    def post(self, request, capture_node):
        try:
            capture_node = CaptureNode.objects.get(id=capture_node).hostname
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        result = status_tasks.set_capture_settings.apply_async([request.data], queue=capture_node)

        _classname = type(self).__name__

        # This should be quick, wait for the result to finish.
        try:
            result_data = result.get(timeout=self.timeout)
            ephemeral_task_cleanup(result)
        except TimeoutError:
            return Response(data={'warning': "{} timed out.".format(_classname)})
        except Exception:
            log.error("{} ({}). {}".format(_classname, capture_node, format_exc()))
            return Response(data={'warning': "{} encountered an error.".format(_classname)})

        return Response(data=result_data)


class CaptureNodeDataView(CaptureNodeAdminView):
    doc = None
    task = None
    timeout = 10

    def get(self, request, capture_node):
        try:
            capture_node = CaptureNode.objects.get(id=capture_node).hostname
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        result = self.task.apply_async(queue=capture_node)

        _classname = type(self).__name__

        log.info("data view {}: {}".format(_classname, repr(self.task)))

        # This should be quick, wait for the result to finish.
        try:
            result_data = result.get(timeout=self.timeout)
            ephemeral_task_cleanup(result)
        except TimeoutError:
            return Response(data={'warning': "{} timed out.".format(_classname)})
        except Exception:
            log.error("{} ({}). {}".format(_classname, capture_node, format_exc()))
            return Response(data={'warning': "{} encountered an error.".format(_classname)})

        return Response(data=result_data)


class DeviceListView(CaptureNodeDataView):
    """"Get the list of storage devices on the given capture_node. Three lists of devices are
returned: regular devices, devices initialized by the capture system, and empty storage slots.
    URL args:
        - capture_node: An capture_node id
    """

    task = device_tasks.list


class InterfaceListView(CaptureNodeDataView):
    """Get the list of interfaces on the given capture_node.
    URL args:
        - capture_node: An capture_node id
    """
    task = iface_tasks.interface_list


class InterfaceToggleView(CaptureNodeAdminView):
    """Toggle this interface in or out of 'capturing' mode. Capture must be restarted for this to
    take effect.
    URL args:
        - capture_node: An capture_node id
        - iface: An interface name"""
    timeout = 5

    class PostSerializer(serializers.Serializer):
        iface = serializers.CharField(min_length=1, max_length=20)

    def post(self, request, capture_node):

        try:
            capture_node = CaptureNode.objects.get(id=capture_node).hostname
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        result = iface_tasks.interface_toggle.apply_async([ser.validated_data['iface']],
                                                          queue=capture_node)

        _classname = type(self).__name__

        # This should be quick, wait for the result to finish.
        try:
            result_data = result.get(timeout=self.timeout)
            ephemeral_task_cleanup(result)
        except TimeoutError:
            return Response(data={'warning': "{} timed out.".format(_classname)})
        except:
            log.error("{} ({}). {}".format(_classname, capture_node, format_exc()))
            return Response(data={'warning': "{} encountered an error.".format(_classname)})

        return Response(data={'info': result_data.get('info'),
                              'data': result_data.get('data')})


class InterfaceSetQueuesView(CaptureNodeAdminView):

    timeout = 5

    def post(self, request, capture_node):

        try:
            capture_node = CaptureNode.objects.get(id=capture_node).hostname
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        ifaces = request.data.getlist('iface')
        if len(ifaces) != 1:
            return Response({'warning': "One interface name must be specified."})
        iface = ifaces[0]

        queues = request.data.getlist('queues')
        bad_queues = False
        if len(queues) != 1:
            bad_queues = True
        queues = queues[0]

        try:
            queues = int(queues)
        except ValueError:
            bad_queues = True

        if bad_queues:
            log.error("Invalid Queue Count: {}".format(queues))
            return Response({'warning': "Invalid queue count."})

        result = iface_tasks.interface_set_queues.apply_async([iface, queues], queue=capture_node)

        _classname = type(self).__name__

        # This should be quick, wait for the result to finish.
        try:
            result_data = result.get(timeout=self.timeout)
            ephemeral_task_cleanup(result)
        except TimeoutError:
            return Response(data={'warning': "{} timed out.".format(_classname)})
        except:
            log.error("{} ({}). {}".format(_classname, capture_node, format_exc()))
            return Response(data={'warning': "{} encountered an error.".format(_classname)})

        return Response(data={'info': '{} queues updated.'.format(iface)})


class DeviceCmdView(CaptureNodeAdminView):
    # The task to run on the capture_node
    task = None
    # If we don't track the task, how long to wait for completion.
    timeout = 10
    # Extra arguments to pass the task
    extra_args = []
    # The maximum number of devices allowed
    device_limit = None
    # Whether we use TaskTrack to track the task. If not, we delete the results immediately.
    track = False
    # The task_track description
    track_desc = ""
    # The context data to return on task start.
    track_start_context = {}

    class PostSerializer(serializers.Serializer):
        devices = serializers.ListField(child=serializers.CharField(max_length=10, min_length=1))

    def post(self, request, capture_node):

        log.error('data? {}'.format(request.data,))
        try:
            capture_node = CaptureNode.objects.get(id=capture_node).hostname
        except CaptureNode.DoesNotExist:
            return Response(status=404)

        ser = self.PostSerializer(data=request.data)
        if not ser.is_valid():
            return Response(data={'warning': format_errors(ser.errors)})

        log.error('val_data? {}'.format(list(ser.validated_data.keys())))
        devices = ser.validated_data['devices']

        if self.device_limit is not None and len(devices) > self.device_limit:
            return Response(data={'warning': 'The maximum number of devices for this command is {}.'
                                             .format(self.device_limit)})

        result = self.task.apply_async([devices] + self.extra_args, queue=capture_node)

        _class_name = type(self).__name__

        if self.track:
            # Track the task in our augmented task tracking system
            TaskTrack.track(result, self.track_desc, request)

            return Response(data=self.track_start_context)
        else:
            # This should be quick, wait for the result to finish.
            try:
                result_data = result.get(timeout=self.timeout)
                ephemeral_task_cleanup(result)
            except TimeoutError:
                return Response(data={'warning': "{} timed out.".format(_class_name)})
            except:
                log.error("{} ({}). {}".format(_class_name, capture_node, format_exc()))
                return Response(data={'warning': "{} encountered an error.".format(_class_name)})

            return Response(data=result_data)


class DeviceToggleLocateView(DeviceCmdView):
    """Toggle the locator light on a given device (if the light control exists)
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device nodes for which to toggle the locator light.
    """
    task = device_tasks.disk_locate_toggle


class DeviceSpareSetView(DeviceCmdView):
    """Set the given devices as spares.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node names to change.
    """
    task = device_tasks.spare_set


class DeviceSpareRemoveView(DeviceCmdView):
    """Remove the given devices as spares.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node names to change.
    """
    task = device_tasks.spare_remove


class DeviceActivateView(DeviceCmdView):
    """Activate the given device for capture.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node names to activate.
    """
    task = device_tasks.activation
    extra_args = ['activate']
    device_limit = 1


class DeviceDeactivateView(DeviceCmdView):
    """Deactivate the given device for capture.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node names to activate.
    """
    task = device_tasks.activation
    extra_args = ['deactivate']
    device_limit = 1


class CreateRAIDView(DeviceCmdView):
    """View for creating a RAID5 from a series of disks on a given capture_node.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node names (without the full path) to add to the RAID5.
                 At least four are expected.
    """
    task = device_tasks.make_raid5
    track = True
    task_desc = "Creating a RAID 5 array."
    task_start_context = {'info': 'Raid creation task created.'}


class DestroyRAIDView(DeviceCmdView):
    """Destroy the Raid on the given device, freeing the original components.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: The device node name to destroy. Can be a single name or a list of a single
      name.
    """
    task = device_tasks.destroy_raid
    device_limit = 1


class MakeIndexDevice(DeviceCmdView):
    """Create an index initialization task on the given capture_node.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: A list of one or two device node names to use to make the index disk.
                 If two nodes are given, the index device is created as a RAID 1.
    """
    task = device_tasks.make_index_device
    track = True
    task_desc = 'Creating index device.'
    task_start_context = {'info': 'Index device initialization started.'}
    device_limit = 2


class InitCaptureDeviceView(DeviceCmdView):
    """Initializes a device for capture. This formats the disk and pre-allocates capture
    nodes. The disk and nodes are added to the capture_node DB to be used by PcapDB. The device
    must be set ACTIVE before it is used for capture.
    URL args:
      - capture_node: An capture_node id
    POST data:
      - devices: A single device node name to initialize.
    """
    task = device_tasks.init_capture_device
    track = True
    track_desc = "Initializing {} for capture"
    track_start_context = {'info': 'Device initialization has started.'}
    device_limit = 1

