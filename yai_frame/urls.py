from django.urls import path
from django.views.generic import RedirectView

from .views import FrameChatView, FrameMessagingView

app_name = "yai_frame"

urlpatterns = [
    path("messaging/", FrameMessagingView.as_view(), name="messaging"),
    path("chat/", FrameChatView.as_view(), name="chat"),
    path("", RedirectView.as_view(url="chat/"), name="index"),
]
