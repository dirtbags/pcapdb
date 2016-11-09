from libs.mixins import AuthenticatedTemplateView
from apps.configuration_api.views import GetConfigurationData
from apps.configuration_api.views import GetConfigurationHistory

from collections import OrderedDict


class ConfigurationManagement(AuthenticatedTemplateView):
    template_name = 'config_main.html'
    group_required = ['cpatreportpriv']

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        # Call the API directly with the same request
        current = GetConfigurationData().get(self.request).data

        # Generate the label counts here to avoid template weirdness. This is not generating
        # any new data just restructuring it for presentation.
        label_counts = {}
        for group in current:
            for category, configs in group.items():
                label_counts[category] = len(configs)

        label_counts = OrderedDict(sorted(label_counts.items()))

        context['current'] = current
        context['label_counts'] = label_counts

        # AppConfigHistory is not an API Endpoint because it didn't really make sense for
        # it to be one. This can be changed in the future
        context['history'] = GetConfigurationHistory().get(self.request).data

        return context