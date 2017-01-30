from django.conf import settings

__author__ = 'pflarr'


class BaseRouter:
    """Restrict migrations for all database to their relevant host. Only search heads should
    migrate the search head db, and capture nodes their db's."""
    """We need to specify that the capture_nodes should never muck with the search head db. They
    can read and write from it, but they shouldn't make or push migrations for it."""

    # These apps are replicated on both the search head and capture nodes, and use the local
    # database
    UNIVERSAL_APPS = ['contenttypes', 'auth']
    # These apps should only be on the capture nodes
    CAPTURE_NODE_APPS = ['capture_node_api']
    # Anything that isn't universal or a capture node app is a search head app.

    def db_for_read(self, model, **hints):
        if model._meta.app_label in self.UNIVERSAL_APPS:
            if settings.IS_SEARCH_HEAD:
                return 'default'
            elif settings.IS_CAPTURE_NODE:
                return 'capture_node'
            else:
                raise ValueError("Must be either a search head or capture node.")
    db_for_write = db_for_read

    def allow_migrate(self, db, app_label, model=None, **hints):
        """If this is not the search head, then the answer is no."""

        if app_label in self.UNIVERSAL_APPS:
            # Universal apps are always migrated.
            return True
        elif db == 'default' and app_label not in self.CAPTURE_NODE_APPS:
            # Anything else that would use the default db is only migrated on the search head.
            return True
        elif db == 'capture_node' and app_label in self.CAPTURE_NODE_APPS:
            # The capture node api is only migrated on capture nodes
            return True
        else:
            return False
