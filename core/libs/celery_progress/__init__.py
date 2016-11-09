__author__ = 'pflarr'

import time

class TaskProgress:
    """Tracks the progress of a task and limits database interaction to
    a reasonable rate."""

    # The minimum time between published status updates.
    UPDATE_PERIOD = 1.5

    def __init__(self, pieces, task):
        """
        :param pieces: The total number of update increments expected
        for this task.
        :param task: The task object to update
        :return:
        """

        if pieces == 0:
            pieces = 1

        self._inc_amount = 100.0/pieces
        self._last_update = time.time()
        self._meta = {'progress': 0}

        self._task = task

    def inc(self, count=1, **kwargs):
        """Increment the task progress, and send an update if it is time
        to do so.
        :param count: The number of increments to do.
        :param kwargs: Any additional meta attributes to set/update. These persist across calls.
        :return:
        """
        self._meta['progress'] += self._inc_amount * count
        if self._meta['progress'] > 100.0:
            self._meta['progress'] = 100.0

        self._meta.update(kwargs)

        now = time.time()
        if now - self._last_update > self.UPDATE_PERIOD:
            self._task.update_state(state='WORKING',
                                    meta=self._meta)
            self._last_update = now