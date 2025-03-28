from django.contrib.auth.mixins import LoginRequiredMixin
from django.contrib.auth.views import LoginView
from django.urls import reverse_lazy
from django.views.generic import TemplateView


class IndexView(LoginView):
    template_name = "yai/signin/index.html"
    success_url = reverse_lazy("yai_signin:dashboard")
    redirect_authenticated_user = True


class DashboardView(LoginRequiredMixin, TemplateView):
    template_name = "yai/signin/dashboard.html"
