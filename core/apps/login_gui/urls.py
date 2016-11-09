from django.conf.urls import url
from .views import LoginOrUnauthorizedForm, Logout

urlpatterns = [
    url(r'^unauthorized', LoginOrUnauthorizedForm.as_view(), name='unauthorized'),
    url(r'^logout', Logout.as_view(), name='logout'),
]
