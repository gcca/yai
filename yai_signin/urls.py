from django.urls import path

from .views import DashboardView, IndexView

app_name = "yai_signin"

urlpatterns = (
    path("", IndexView.as_view(), name="index"),
    path("dashboard/", DashboardView.as_view(), name="dashboard"),
)
