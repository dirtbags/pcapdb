from django_auth_ldap.backend import populate_user
from django.dispatch import receiver

from django.conf import settings
from django.contrib.auth.models import Group
from django.core.exceptions import PermissionDenied

from apps.search_head_api.models.auth import UserExtraModel

@receiver(populate_user)
def check_groups(sender, **kwargs):
    """Make sure the user that just tried to log in is registered to the right group,
    whether that group was created via LDAP or other means."""

    if 'user' not in kwargs:
        raise PermissionError("No user given.")
    else:
        user = kwargs['user']

    # Get the base group
    base_group = Group.objects.get(name=settings.LDAP_REQUIRED_GROUP)

    if base_group not in user.groups.all():
        user.delete()
        raise PermissionDenied("User not in the required group: {}"
                               .format(settings.LDAP_REQUIRED_GROUP))

    # The user will end up getting saved twice. Oh well.
    extra = UserExtraModel(user=user, type=UserExtraModel.LDAP)
    extra.save()

