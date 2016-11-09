from django.core.urlresolvers import resolve
from django.db import models, transaction
from django.db.utils import IntegrityError
from djcelery.models import TaskMeta
from django.db.models.fields.related_descriptors import ReverseOneToOneDescriptor
from django.contrib.auth.models import User

__author__ = 'pflarr'


class ReverseOneToOneDescriptorReturnsNone(ReverseOneToOneDescriptor):
    def __get__(self, instance, instance_type=None):
        try:
            return super(ReverseOneToOneDescriptorReturnsNone, self).\
                        __get__(instance=instance, instance_type=instance_type)
        except models.ObjectDoesNotExist:
            return None


class OneToOneOrNoneField(models.OneToOneField):
    """A OneToOneField that returns None if the related object doesn't exist"""
    related_accessor_class = ReverseOneToOneDescriptorReturnsNone


def ephemeral_task_cleanup(task_result):
    """This task doesn't need a long lasting record. Delete the results from the DB."""

    task = TaskMeta.objects.get(task_id=task_result.id)
    task.delete()


class TaskTrack(models.Model):
    task = OneToOneOrNoneField(TaskMeta,
                               # When we create these, it's very likely the cooresponding TaskMeta
                               # row won't exist just yet.
                               db_constraint=False,
                               to_field='task_id',
                               help_text="The associated task metadata in djcelery.")
    user = models.ForeignKey(User, null=True,
                             help_text="The user that started this task.")
    view = models.CharField(max_length=100, null=True,
                            help_text="The view this task is attached to.")
    descr = models.CharField(max_length=100, null=True,
                             help_text="Helpful description of this task.")
    started = models.DateTimeField(auto_now_add=True,
                                   help_text="When this task was started.")
    cleared = models.BooleanField(default=False, null=False,
                                  help_text="Whether this task has been cleared by the user.")

    @staticmethod
    @transaction.atomic
    def track(result, descr, request=None, child_weight=1):
        """
        Track the given task, assigning extra metadata we can use to group tasks for the interface.
        :param celery.result.AsyncResult result: The result for the task we intend to track.
        :param rest_framework.request.Request request: The request that caused this
        task to start.
        :param str descr: A description of this task.
        :param float child_weight: How much the children tasks weigh on the final progress
                                   numbers. When calculating progress, the total progress
                                   is (P + c1*w + c2*w + ...)/(1 + num_children*w). P is the
                                   parent's progress, cN is child N's progress, and w is this
                                   weight number.
        :return:
        """

        if request is not None:
            user = request.user
            view = resolve(request.path_info).view_name
        else:
            user = None
            view = None

        # It should be noted that we're setting the task directly here, rather than with an
        # instance of TaskMeta. It's very likely that the TaskMeta instance won't exist yet.
        tracker = TaskTrack(task_id=result.task_id, user=user, view=view, descr=descr)
        tracker.save()

        # We need to check if the task has any children. There are two ways that this can
        # be the case. First, the task may simply know about children via its 'children' element.
        # Secondly, it may be a callback (as a chord) for some abstract task. Our task has the
        # abstract task as a parent, but in the case of a chord the abstract classes children
        # will not include our starting task. It will, however, have the group of tasks that are
        # the prerequisite for our starting task.
        children = []
        if result.children is not None:
            for child in result.children:
                children.append(child)

        if result.parent is not None and result.parent.children is not None:
            for child in result.parent.children:
                # Just making sure that we never somehow end up with this task as its own child.
                if child.task_id != result.task_id:
                    children.append(child)

        for child in children:
            child_tracker = TaskChildTrack(parent=tracker,
                                           task_id=child.task_id,
                                           weight=child_weight)
            child_tracker.save()

    @transaction.atomic
    def save(self, *args, **kwargs):
        """Our save method needs to make sure the corresponding TaskMeta row exists."""

        # Do the standard save.
        super(type(self), self).save(*args, **kwargs)

        # Check if there is a cooresponding TaskMeta row. If not, try to create one.
        try:
            self.task
        except TaskMeta.DoesNotExist:
            try:
                tm = TaskMeta(task_id=self.task_id,
                              status='PENDING')
                tm.save()
                self.task = tm
            except IntegrityError:
                pass

    def status(self):
        """Returns the child-aware status and progress of this task.
        :return (str,str): status, progress_percent
        """

        # Try to access the task object associated with this item.
        try:
            self.task
        except TaskMeta.DoesNotExist:
            return 'FAILURE', ''

        status = self.task.status

        result = self.task.result
        if issubclass(type(result), dict):
            try:
                total_progress = int(result.get('progress', 0))
            except ValueError:
                total_progress = 0
        else:
            total_progress = 0

        if status == 'SUCCESS':
            total_progress = 100

        has_finished_child = False
        has_started_child = False
        total_weight = 1
        for child in self.children.all():
            total_weight += child.weight
            try:
                if child.task.status == 'SUCCESS':
                    total_progress += 100*child.weight
                    has_finished_child = True
                elif child.task.status != 'PENDING':
                    has_started_child = True
                    if issubclass(type(child.task.result), dict):
                        try:
                            total_progress += int(child.task.result.get('progress', 0))*child.weight
                        except ValueError:
                            # Don't add this child's progress on error
                            pass
            except TaskMeta.DoesNotExist:
                pass

        progress = '{:2.0f}%'.format(total_progress/total_weight)
        if status == 'PENDING':
            if has_finished_child:
                status = 'WORKING'
            elif has_started_child:
                status = 'STARTED'

        return status, progress


class TaskChildTrack(models.Model):
    parent = models.ForeignKey(TaskTrack,
                               related_name='children',
                               help_text="The task responsible for the final results.")
    task = OneToOneOrNoneField(TaskMeta,
                               # The task meta row probably won't exist yet.
                               db_constraint=False,
                               to_field='task_id',
                               help_text='The djcelery task metadata entry.')
    weight = models.FloatField(default=1.0,
                               help_text="How much this task's status weighs on the total.")

    # Steal the save method from TaskTrack. That was a great idea!
    save = TaskTrack.save

