from libs.routers import app_router

__author__ = 'pflarr'

# Make a generic router for this application's database tables.
CaptureNodeRouter = app_router(['capture_node_api'], 'capture_node')
