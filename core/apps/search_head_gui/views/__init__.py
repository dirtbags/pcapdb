from django.conf import settings
from django.views.generic.base import TemplateView
from django.http import Http404, HttpResponseRedirect
from django.core.urlresolvers import reverse
from apps.search_head_api.models import Site, CaptureNode, SearchInfo
from apps.stats_api.views import GroupsByStat
from apps.stats_api.models import Stats, StatsInterface
from braces.views import LoginRequiredMixin

import datetime
import pytz

__author__ = 'pflarr'


class UserView(LoginRequiredMixin, TemplateView):
    pass


class DashboardView(UserView):
    template_name = 'dashboard.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        # Get the capture nodes with stats in the db.
        context['sites'] = [site for site in Site.objects.all()]
        context['capture_nodes'] = [node for node in CaptureNode.objects.all()]
        context['interfaces'] = [(r.id, r.name) for r in StatsInterface.objects.all()]
        context['bystat_groups'] = GroupsByStat.GetSerializer.GROUPINGS.items()
        context['bystat_stat_types'] = GroupsByStat.GetSerializer.STAT_TYPES.items()

        return context


class MyTasksView(UserView):
    template_name = 'my_tasks.html'

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)

        return context


class SearchView(UserView):
    template_name = "search.html"

    def get_context_data(self, **kwargs):
        """Pass the task_id back to the template if one is given.
        :param kwargs:
        """

        context = super().get_context_data(**kwargs)

        context['search'] = None
        if 'search_id' in kwargs and kwargs['search_id'] is not None:
            try:
                search = SearchInfo.objects.get(id=kwargs['search_id'])
            except SearchInfo.DoesNotExist:
                raise Http404
        else:
            now = pytz.UTC.localize(datetime.datetime.utcnow())
            search = SearchInfo(start=now-datetime.timedelta(hours=1),
                                end=now,
                                proto=0)

        context['search'] = search

        context['search_protos'] = settings.SEARCH_TRANSPORT_PROTOS.items()

        return context


# TODO: The permissions on each search result need to be checked. Right now they're served
#       directly from /media, which makes anyone able to get to them. This is bad.
class SearchResultView(UserView):
    template_name = None

    def dispatch(self, request, *args, **kwargs):
        """Eventually this will give a page that displays a list of results for each site.
        For now, just redirect either to a Pcap file, or to a SearchView populated with
        the flow results (depending on the search type).
        """

        try:
            search = SearchInfo.objects.get(id=args[0])
        except SearchInfo.DoesNotExist:
            # No search, no page.
            raise Http404

        if search.type == search.T_FLOW:
            # Redirect to a flow result page.
            return HttpResponseRedirect(reverse('search_head_gui:search', [search.id]))
        elif search.type == search.T_PCAP:
            return HttpResponseRedirect(search.file.url())
        else:
            raise RuntimeError("Invalid search type.")
