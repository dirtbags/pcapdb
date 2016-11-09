from django.views.generic.base import TemplateView
from django.conf import settings


class ForgotPasswordView(TemplateView):
    template_name = 'auth/forgot_password.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        context['splash_title'] = settings.SPLASH_TITLE
        context['splash_text'] = settings.SPLASH_TEXT

        return context
