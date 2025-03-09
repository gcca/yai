from django.urls import path

from .views import MessageDailyView, MessageView, PartialDailyView, PartialView

app_name = "yai_daily"

urlpatterns = (
    path("message/", MessageView.as_view(), name="message"),
    path("partial/", PartialView.as_view(), name="partial"),
    path("", PartialDailyView.as_view(), name="index"),
    path("i/message/", MessageDailyView.as_view(), name="message-chat"),
)
