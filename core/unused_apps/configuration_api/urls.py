from django.conf.urls import url
from .views import GetConfigurationData
from .views import UpdateConfig
from .views import AddConfig
from .views import DeleteConfig
from .views import GetConfigurationHistory

urlpatterns = [
    # url(r'^$', SOMETHING_ASVIEW, name="main"),
    url(r'^current/$', GetConfigurationData.as_view(), name="current"),
    url(r'^update/$', UpdateConfig.as_view(), name="update"),
    url(r'^add/$', AddConfig.as_view(), name="add"),
    url(r'^delete/$', DeleteConfig.as_view(), name="delete"),
    url(r'^history/$', GetConfigurationHistory.as_view(), name="history"),
]