from django.conf.urls import include, url
from django.contrib import admin
from django.views.generic.base import RedirectView
from django.conf.urls.static import static

from django.conf import settings

from rest_framework_jwt.views import obtain_jwt_token

urlpatterns = []
# These should only be for the search head (even though the capture nodes
# don't even run a web server.)
if settings.IS_SEARCH_HEAD:
    urlpatterns.extend([
        url(r'^pcapdb/', include('apps.search_head_gui.urls', namespace='search_head_gui')),
        url(r'^api/', include('apps.search_head_api.urls', namespace='search_head_api')),
        url(r'^stats/', include('apps.stats_api.urls', namespace='stats_api')),
        url(r'^task/', include('apps.task_api.urls', namespace="task_api")),
        url(r'^auth/', include('apps.login_gui.urls', namespace="auth")),
        url(r'^$', RedirectView.as_view(url='/pcapdb/'), name='homepage'),
        url(r'^admin/', include(admin.site.urls)),
        # API Endpoints
        url(r'^api/v1.0/auth/', include('apps.login_api.urls', namespace='api_auth')),
        # Browsable API Login
        url(r'^api-auth/', include('rest_framework.urls', namespace='rest_framework')),
        # Programmatic Token Auth for scripts
        url(r'^api-token-auth/', obtain_jwt_token),
    ] + static(settings.STATIC_URL, document_root=settings.STATIC_ROOT))

    if settings.DEBUG:
        try:
            import debug_toolbar
            urlpatterns.extend([url(r'^__debug__/', include(debug_toolbar.urls))])
        except ImportError:
            debug_toolbar = None

