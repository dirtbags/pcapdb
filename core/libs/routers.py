__author__ = 'pflarr'

def app_router(app_labels, dbname):
    """Create a Django database router that routes all database connections to the given dbname (
    which must be configured) for each of the apps in the app_labels sequence."""

    class _AppRouter:
        def db_for_read(self, model, **hints):
            if model._meta.app_label in app_labels:
                return dbname
        db_for_write = db_for_read

        def allow_relation(self, obj1, obj2, **hints):
            label1 = obj1._meta.app_label
            label2 = obj2._meta.app_label

            if not (label1 in app_labels or label2 in app_labels):
                # If neither relation is in our app set, we don't care how they relate.
                return None
            elif label1 in app_labels and label2 in app_labels:
                # If they're both in our app set, then relations are definitely allowed.
                return True
            else:
                # If one is, and the other isn't, then they aren't allowed.
                return False

        def allow_migrate(self, db, app_label, model=None, **hints):
            """The given apps should have their tables in the given db, and those tables should
            be the only tables migrated to the db."""

            if db == dbname and app_label in app_labels:
                return True
            if db == dbname or app_label in app_labels:
                return False

            return None

    return _AppRouter
