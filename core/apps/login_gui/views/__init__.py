from django.views.generic.base import TemplateView
from django.views.generic.base import RedirectView
from django.contrib.auth import logout
from django.core.urlresolvers import reverse
from django.conf import settings

import logging
log = logging.getLogger(__name__)

class LoginOrUnauthorizedForm(TemplateView):
    template_name = 'login_or_unauthorized.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        context['splash_title'] = settings.SPLASH_TITLE
        context['splash_text'] = settings.SPLASH_TEXT

        return context


class Logout(RedirectView):
    permanent = False

    def get_redirect_url(self, *args, **kwargs):
        logout(self.request)
        # return reverse('auth:unauthorized')
        return '/'
