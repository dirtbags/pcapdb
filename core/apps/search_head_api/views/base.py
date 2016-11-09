from rest_framework.authentication import SessionAuthentication
from rest_framework.permissions import IsAuthenticated, BasePermission
from rest_framework.renderers import JSONRenderer, BrowsableAPIRenderer
from rest_framework.views import APIView
from rest_framework.request import Request

import logging
log = logging.getLogger(__name__)


class IsPcapDBAdmin(BasePermission):
    """Make sure the user is in the admin group."""
    def has_permission(self, request, view):
        """
        :param Request request:
        :param APIView view:
        """

        log.info("Checking for admin on page. {} {}".format(request.auth, request.user))
        if request.user is None:
            return False

        if not request.user.extra.is_pcapdb_admin:
            return False

        return True

class SearchHeadAPIView(APIView):
    """Base view for search head api views. Sets up default authentication and
    permissions."""
    renderer_classes = (JSONRenderer, BrowsableAPIRenderer)
    permission_classes = (IsAuthenticated,)
    authentication_classes = (SessionAuthentication,)


class SearchHeadAPIAdminView(APIView):
    renderer_classes = (JSONRenderer, BrowsableAPIRenderer)
    permission_classes = (IsAuthenticated, IsPcapDBAdmin)
    authentication_classes = (SessionAuthentication,)

