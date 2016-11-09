from django.contrib.auth import authenticate, login
from rest_framework.views import APIView
from rest_framework.response import Response
from ..serializers import LoginSerializer
from rest_framework.permissions import AllowAny

import datetime
import pytz

import logging
log = logging.getLogger(__name__)

class LoginView(APIView):
    permission_classes = (AllowAny, )   # allow anonymous
    authentication_classes = () # don't auth, allow any
    serializer_class = LoginSerializer

    def post(self, request, format=None):
        # use request.DATA for POST, PUT and PATCH verbs.
        login_form = self.serializer_class(data=request.data)
        data = {}
        log.error(login_form.is_valid())
        log.error(login_form.data)
        if login_form.is_valid():
            # We can access login_form.data now (a dictionary). The data is guaranteed to have
            # been validated using its serializer at this point.
            username = login_form.data['username']
            password = login_form.data['password']
            user = authenticate(username=username, password=password)
            if user is not None and user.is_active:
                login(request, user)
                now = pytz.UTC.localize(datetime.datetime.now())
                user.extra.last_login = now
                data['data'] = "OK"
            else:
                data['error'] = "Invalid login information provided."
        else:
            data['error'] = "Invalid login information provided(2)."

        return Response(data, status=200)