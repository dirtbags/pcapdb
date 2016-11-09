from celery import Celery

from apps.capture_node_api.lib import disk_management as disk_man

app = Celery('disks')

@app.task(throws=RuntimeError)
def make_raid5(disks):
    disk_man.make_raid5(disks)
