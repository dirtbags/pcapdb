import logging
log = logging.getLogger(__name__)

class CaptureNodeRouter:
    def __call__(self, name, task_args, task_kwargs, options, task=None, **kwargs):
        if name.startswith('capture_node_api.tasks.'):
            return {'exchange': 'capture_node',
                    'exchange_type': 'direct',
                    'queue': options['queue']}
        elif 'queue' in options['queue'] and options['queue'] == 'search_head':
            return {'exchange': 'search_head',
                    'exchange_type': 'direct',
                    'queue': 'search_head'}
        else:
            return {'exchange': 'celery',
                    'exchange_type': 'direct',
                    'queue': 'celery'}
