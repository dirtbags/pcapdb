from django.conf.urls import url
from .views import ConfigurationManagement

urlpatterns = [
    url(r'^$', ConfigurationManagement.as_view(), name="main"),
]