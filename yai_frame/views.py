from datetime import datetime
from io import StringIO
from queue import Queue
from typing import Any, Dict, Iterable, Iterator, List, Tuple, TypeAlias, cast

from django.contrib.auth.mixins import LoginRequiredMixin
from django.core.files.uploadedfile import InMemoryUploadedFile
from django.http import (
    HttpRequest,
    HttpResponse,
    HttpResponseBase,
    HttpResponseRedirect,
    StreamingHttpResponse,
)
from django.shortcuts import redirect
from django.template.loader import render_to_string
from django.urls import reverse_lazy
from django.views.generic import FormView, View
from markdown import markdown
from pandas import DataFrame, read_csv, to_datetime

import yai_chat_abi

from .forms import FrameForm

Record: TypeAlias = Tuple[str, str, DataFrame]


class FrameChatView(LoginRequiredMixin, FormView):

    template_name = "yai/frame/chat.html"
    form_class = FrameForm
    success_url = reverse_lazy("yai_frame:chat")

    def get_context_data(self, **kwargs):
        context = super().get_context_data(**kwargs)
        history = FrameScope.GetHistory(self.request)
        context["history"] = Apply(history)
        return context

    def get(self, request, *args, **kwargs) -> HttpResponse:
        if not FrameScope.HasDf(request):
            return redirect("yai_front:dashboard")
        return super().get(request, *args, **kwargs)

    def form_valid(self, form: FrameForm) -> HttpResponseRedirect:
        file = form.cleaned_data["file"]

        na_values = ["", "-", "--", "NA", "N/A", "n/a"]

        df = read_csv(
            StringIO(file.read().decode("utf-8")),
            dtype_backend="numpy_nullable",
            na_values=na_values,
            keep_default_na=True,
            na_filter=True,
        )

        for column in df.columns:
            if df[column].dtype == "object":
                converted = to_datetime(df[column], errors="coerce")
                if converted.notna().any():
                    df[column] = converted

        FrameScope.SetFile(self.request, file)
        FrameScope.SetDf(self.request, df)

        history = FrameScope.GetHistory(self.request)
        history.clear()
        history.append(("Inicio", f"name: {file.name}", df))

        return super().form_valid(form)


class FrameMessagingView(LoginRequiredMixin, View):

    def get(self, request: HttpRequest) -> HttpResponseBase:
        queue = FrameScope.FlushQueue(request)
        history = FrameScope.GetHistory(request)

        def content() -> Iterator[str]:
            while True:
                q = queue.get()

                if not q:
                    queue.task_done()
                    continue

                if q == "[DONE]":
                    yield "data: :\n\n"
                    break

                yai_chat_abi.ProcessFrame(history, q, Scope(request))

                data = (
                    render_to_string(
                        "yai/frame/item.html",
                        context={"history": Apply(history)},
                    )
                    .replace("\n", "")
                    .strip()
                )

                queue.task_done()

                yield f"data: {data}\n\n"

            yield "data: [DONE]\n\n"

        response = StreamingHttpResponse(
            content(), content_type="text/event-stream"
        )
        response["Cache-Control"] = "no-cache"
        return response

    def post(self, request: HttpRequest) -> HttpResponseBase:
        body = request.body
        if body:
            queue = FrameScope.GetQueue(request)
            queue.put(body.decode())
        return HttpResponse()

    @staticmethod
    def ProcessShow(
        request: HttpRequest, q: str, history: List[Record]
    ) -> None:
        df = FrameScope.GetDf(request)
        df_head = df.head(10)

        rows = ['<table class="table table-striped"><thead><tr>']
        rows.extend(f"<th>{col}</th>" for col in df_head.columns)
        rows.append("</tr></thead><tbody>")

        for _, row in df_head.iterrows():
            rows.append("<tr>")
            rows.extend(f"<td>{str(val)}</td>" for val in row)
            rows.append("</tr>")
        rows.append("</tbody></table>")

        a = "".join(rows)
        last = history[-1][2]
        history.append((q, a, last))


def Apply(history: List[Record]) -> Iterable[Tuple[str, str]]:
    return ((h[0], markdown(h[1])) for h in history)


def Scope(request: HttpRequest) -> Dict[str, str]:
    return {
        "username": cast(Any, request).user.username,
        "today": datetime.now().strftime("%Y-%m-%d"),
    }


class FrameScope:
    queues: Dict[str, Queue[str]] = {}
    histories: Dict[str, List[Record]] = {}
    files: Dict[str, InMemoryUploadedFile] = {}
    dfs: Dict[str, DataFrame] = {}

    @staticmethod
    def GetQueue(request: HttpRequest) -> Queue[str]:
        user = cast(Any, request).user
        name = f"{user.username}-queue"
        if name in FrameScope.queues:
            return FrameScope.queues[name]
        queue = Queue()
        FrameScope.queues[name] = queue
        return queue

    @staticmethod
    def FlushQueue(request: HttpRequest) -> Queue[str]:
        user = cast(Any, request).user
        name = f"{user.username}-queue"
        if name in FrameScope.queues:
            queue = FrameScope.queues[name]
            queue.put("[DONE]")
        queue = Queue()
        FrameScope.queues[name] = queue
        return queue

    @staticmethod
    def GetHistory(request: HttpRequest) -> List[Record]:
        user = cast(Any, request).user
        name = f"{user.username}-history"
        if name not in FrameScope.histories:
            FrameScope.histories[name] = []
        return FrameScope.histories[name]

    @staticmethod
    def GetFile(request: HttpRequest) -> InMemoryUploadedFile:
        user = cast(Any, request).user
        name = f"{user.username}-file"
        return FrameScope.files[name]

    @staticmethod
    def SetFile(request: HttpRequest, file: InMemoryUploadedFile) -> None:
        user = cast(Any, request).user
        name = f"{user.username}-file"
        FrameScope.files[name] = file

    @staticmethod
    def GetDf(request: HttpRequest) -> DataFrame:
        user = cast(Any, request).user
        name = f"{user.username}-df"
        return FrameScope.dfs[name]

    @staticmethod
    def SetDf(request: HttpRequest, df: DataFrame) -> None:
        user = cast(Any, request).user
        name = f"{user.username}-df"
        FrameScope.dfs[name] = df

    @staticmethod
    def HasDf(request: HttpRequest) -> bool:
        user = cast(Any, request).user
        name = f"{user.username}-df"
        return name in FrameScope.dfs
