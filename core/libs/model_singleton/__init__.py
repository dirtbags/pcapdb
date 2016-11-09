from django.db import models


class SingletonModel(models.Model):
    """This helps us build a model with a single row. This is typically for configuration
    data."""
    class Meta:
        abstract = True

    def save(self, *args, **kwargs):
        self.__class__.objects.exclude(id=self.id).delete()
        super(SingletonModel, self).save(*args, **kwargs)

    @classmethod
    def load(cls):
        """:rtype cls"""
        try:
            return cls.objects.get()
        except cls.DoesNotExist:
            return cls()

