from django.contrib.auth.backends import ModelBackend
from apps.search_head_api.models.auth import PcapDBUserModel


class PcapDBModelBackend(ModelBackend):
    """A backend that replaces the standard user model with our proxy user model."""
    def get_user(self, user_id):
        UserModel = PcapDBUserModel
        try:
            user = UserModel._default_manager.get(pk=user_id)
        except UserModel.DoesNotExist:
            return None
        return user if self.user_can_authenticate(user) else None