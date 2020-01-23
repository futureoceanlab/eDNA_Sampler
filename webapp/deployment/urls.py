from django.urls import path
from . import views

##
# these are the url paths to access different functions in the views.py
# all of these paths start with 'deployment/X/Y/Z'
#
urlpatterns = [
    path('', views.index, name='index'),
    path('logs', views.handle_logs, name='log_files'),
    path('<slug:uid>', views.detail, name='detail'),
    path('data/<slug:uid>', views.get_data, name="data"),
    path('get_log/<slug:log_name>', views.get_log, name="get_log"),
    path('get_config/<slug:uid>', views.get_config, name='get_config'),
    path('delete/<slug:uid>', views.delete_deployment, name="delete_deployment"),
    path('upload/<slug:uid>', views.upload_deployment_data, name="upload_deployment_data"),
    path('upload-log/<slug:uid>', views.upload_log, name="upload_log"),
    path('create/<int:device_id>', views.create_deployment, name="create_deployment"),
    path('datetime/now', views.get_datetime, name="datetime_sync"),
    path('has_deployment/<int:device_id>', views.check_deployment, name="check_deployment")
]