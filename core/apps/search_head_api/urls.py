from django.conf.urls import url

from .views import capturenode, search, tests, users, sites

urlpatterns = [
    url(r'^search$', search.Search.as_view(), name='search'),
    url(r'^parse-search$', search.ParseSearch.as_view(), name='parse-search'),
    url(r'^node-search-result/([0-9a-f]{8}-(?:[0-9a-f]{4}-){3}[0-9a-f]{12})$',
        search.PutNodeSearchResult.as_view(), name='node-search-result'),
    url(r'^result/(?P<search_id>\d+)$', search.ResultView.as_view(), name='result'),
    url(r'^flows/(?P<search_id>\d+)$', search.FlowResultView.as_view(), name='flows'),

    # User/group management API's
    url(r'^conf/users$', users.UserListView.as_view(), name='users'),
    url(r'^conf/users/add$', users.UserAddView.as_view(), name='user_add'),
    url(r'^conf/users/delete$', users.UserDeleteView.as_view(), name='user_delete'),
    url(r'^conf/users/add_group$', users.UserAddGroupView.as_view(), name='user_add_group'),
    url(r'^conf/users/remove_group$', users.UserRemoveGroupView.as_view(),
        name='user_remove_group'),
    url(r'^conf/users/reset$', users.UserPasswordResetView.as_view(), name='user_password_reset'),
    url(r'^conf/users/reset/(?P<token>[0-9a-f]{32})$', users.UserPasswordSetView.as_view(),
        name='user_password_set'),

    # Capture Site Management API's
    url(r'^conf/sites$', sites.SiteListView.as_view(), name='sites'),
    url(r'^conf/sites/add$', sites.SiteAddView.as_view(), name='site_add'),
    url(r'^conf/sites/delete$', sites.SiteDeleteView.as_view(), name='site_delete'),

    # CaptureNode Management API's
    url(r'^conf/cn(?:/(?P<capture_node>\d+))?$', capturenode.CaptureNodesView.as_view(),
        name="capture_nodes"),
    url(r'^conf/cn/add$', capturenode.CaptureNodeAddView.as_view(),
        name="capture_node_add"),
    url(r'^conf/cn/remove$', capturenode.CaptureNodeRemoveView.as_view(),
        name="capture_node_remove"),

    # Capture Node Configuration API's
    # These all work through celery queries to the capture nodes themselves.
    url(r'^conf/cn/(?P<capture_node>\d+)/settings$', capturenode.CaptureNodeSettingsView.as_view(),
        name="capnode_settings"),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev$', capturenode.DeviceListView.as_view(),
        name='capnode_devices'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/locate$', capturenode.DeviceToggleLocateView.as_view(),
        name='capnode_dev_locate'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/spare/set$', capturenode.DeviceSpareSetView.as_view(),
        name='capnode_dev_set_spare'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/spare/remove$', capturenode.DeviceSpareRemoveView.as_view(),
        name='capnode_dev_remove_spare'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/activate$', capturenode.DeviceActivateView.as_view(),
        name='capnode_dev_activate'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/deactivate$', capturenode.DeviceDeactivateView.as_view(),
        name='capnode_dev_deactivate'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/create_raid$', capturenode.CreateRAIDView.as_view(),
        name='capnode_dev_create_raid'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/destroy_raid$', capturenode.DestroyRAIDView.as_view(),
        name='capnode_dev_destroy_raid'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/create_index$', capturenode.MakeIndexDevice.as_view(),
        name='capnode_dev_create_index'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/init_dev$', capturenode.InitCaptureDeviceView.as_view(),
        name='capnode_dev_init'),
    url(r'^conf/cn/(?P<capture_node>\d+)/dev/make_index_device$',
        capturenode.MakeIndexDevice.as_view(), name='make_index_device'),

    url(r'^conf/cn/(?P<capture_node>\d+)/ifaces$', capturenode.InterfaceListView.as_view(),
        name='capnode_ifaces'),
    url(r'^conf/cn/(?P<capture_node>\d+)/iface/toggle$', capturenode.InterfaceToggleView().as_view(),
        name='capnode_iface_toggle'),
    url(r'^conf/cn/(?P<capture_node>\d+)/iface/queues$',
        capturenode.InterfaceSetQueuesView().as_view(),
        name='capnode_iface_queues'),

    url(r'^test/sleepy$', tests.SleepyTest.as_view(), name='sleepy_test'),
    url(r'^test/dist$', tests.DistStatusTest.as_view(), name='dist_test'),
]
