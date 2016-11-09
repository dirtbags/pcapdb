from django.conf.urls import url

from apps.stats_api.views import StatsByGroup, GroupsByStat

urlpatterns = [
    url(r'^by-group$', StatsByGroup.as_view(), name='by-group'),
    url(r'^by-stat$', GroupsByStat.as_view(), name='by-stat')
]
