from django.apps import AppConfig

class SearchHeadAPIConfig(AppConfig):
    name = 'apps.search_head_api'
    # To not conflict with the package name
    verbose_name = 'Search Head API'

    def ready(self):
        super().ready()

        # Make sure our signals get connected
        from . import signals
