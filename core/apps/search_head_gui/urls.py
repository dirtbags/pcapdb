from django.conf.urls import url
from .views import DashboardView, MyTasksView, SearchView
from .views.conf import CaptureNodesView, CaptureNodeDiskView, CaptureNodeIfacesView, UserManagementView
from .views.conf import PasswordResetView, SiteManagementView
from .views.login import ForgotPasswordView

urlpatterns = [
    url(r'^$', DashboardView.as_view(), name='main'),
    url(r'^forget_password$', ForgotPasswordView.as_view(), name='forgot_password'),
    url(r'^search(?:/(?P<search_id>[0-9]+))?$', SearchView.as_view(), name='search'),
    url(r'^mytasks$', MyTasksView.as_view(), name='mytasks'),
    url(r'^conf/users', UserManagementView.as_view(), name='conf_users'),
    url(r'^conf/sites', SiteManagementView.as_view(), name='conf_sites'),
    url(r'^conf/reset/(?P<token>[0-9a-f]{32})', PasswordResetView.as_view(),
        name='conf_password_reset'),
    url(r'^conf/capture_nodes', CaptureNodesView.as_view(), name='conf_capture_nodes'),
    url(r'^conf/cn/(?P<capnode>\d+)/disks', CaptureNodeDiskView.as_view(),
        name='conf_capture_node_disks'),
    url(r'^conf/cn/(?P<capnode>\d+)/ifaces', CaptureNodeIfacesView.as_view(),
        name='conf_capture_node_ifaces'),
]
