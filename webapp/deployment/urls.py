from django.urls import path
from . import views

urlpatterns = [
    path('', views.index, name='index'),
    path('device/<int:device_id>/', views.detail, name='detail'),
    path('<slug:uid>', views.set_depth, name='depth'),
    path('get_depth/<slug:uid>', views.get_depth, name='get_depth'),
    path('delete/<int:deployment_id>', views.delete_deployment, name="delete_deployment"),
    path('upload/<slug:uid>', views.upload_deployment_data, name="upload_deployment_data"),
    path('create/<int:device_id>', views.create_deployment, name="create_deployment"),
    path('datetime/now', views.get_datetime, name="datetime_sync"),
    path('has_deployment/<int:device_id>', views.check_deployment, name="check_deployment")
]