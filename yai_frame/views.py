from datetime import datetime
from queue import Queue
from threading import Thread
from typing import Any, Dict, Iterator, List, Optional, Tuple, cast

from django.contrib.auth.mixins import LoginRequiredMixin
from django.core.cache import cache
from django.http import (
    HttpRequest,
    HttpResponse,
    HttpResponseBase,
    StreamingHttpResponse,
)
from django.template.loader import render_to_string
from django.views.generic import TemplateView, View
from markdown import markdown

import yai_chat_abi


class FrameChatView(LoginRequiredMixin, TemplateView):

    template_name = "yai/frame/chat.html"

    def get_context_data(self, **_: Any) -> Any:
        history_cache = HistoryCache(self.request)
        history = history_cache.Get()
        return {"history": Apply(history)}


class FrameMessagingView(LoginRequiredMixin, View):

    def get(self, request: HttpRequest) -> HttpResponseBase:
        arg_cache = ArgCache(request)
        question: Optional[str] = arg_cache.Get()

        if not question:
            return HttpResponse()

        arg_cache.Delete()
        queue = Queue[str]()
        history_cache = HistoryCache(request)
        history = history_cache.Get()

        def process() -> None:
            try:
                yai_chat_abi.ProcessFrameChat(
                    history, question, Scope(request), queue.put
                )
            except Exception as error:
                history.append((question, f"Error: {error}"))
            queue.put("data: [DONE]")

        thread = Thread(target=process)

        def content() -> Iterator[str]:
            thread.start()
            history.append((question, ""))

            try:
                while True:
                    part: str = queue.get()

                    if part == "data: [DONE]":
                        queue.task_done()
                        yield "data: [DONE]\n\n"
                        break

                    q, a = history[-1]
                    history[-1] = (q, a + part)

                    s = (
                        render_to_string(
                            "yai/frame/item.html",
                            {"history": Apply(history)},
                        )
                        .replace("\n", "")
                        .strip()
                    )

                    queue.task_done()
                    yield f"data: {s}\n\n"

            except (BrokenPipeError, ConnectionResetError) as error:
                print(f"Error: {error}")
            finally:
                thread.join()
                history_cache.Put(history)

            yield ":\n\n"

        response = StreamingHttpResponse(
            content(), content_type="text/event-stream"
        )
        response["Cache-Control"] = "no-cache"
        return response

    def post(self, request: HttpRequest) -> HttpResponseBase:
        arg_cache = ArgCache(request)
        arg: Optional[bytes] = request.body
        if arg:
            arg_cache.Put(arg.decode())
        return HttpResponse(content_type="text/event-stream")


def Apply(history: List[Tuple[str, str]]) -> List[Tuple[str, str]]:
    return [(q, markdown(a)) for q, a in history]


def Scope(request: HttpRequest) -> Dict[str, str]:
    return {
        "username": cast(Any, request).user.username,
        "today": datetime.now().strftime("%Y-%m-%d"),
    }


class HistoryCache:
    def __init__(self, request: HttpRequest) -> None:
        username = cast(Any, request).user.username
        self._key = f"{username}-frame-history"

    def Get(self) -> List[Tuple[str, str]]:
        history: Optional[List[Tuple[str, str]]] = cache.get(self._key)
        if not history:
            history = []
        return history

    def Put(self, history: List[Tuple[str, str]]) -> None:
        cache.set(self._key, history)

    def Delete(self) -> None:
        cache.delete(self._key)


class ArgCache:
    def __init__(self, request: HttpRequest) -> None:
        username = cast(Any, request).user.username
        self._key = f"{username}-frame-arg"

    def Get(self) -> Optional[str]:
        return cache.get(self._key)

    def Put(self, arg: str) -> None:
        cache.set(self._key, arg)

    def Delete(self) -> None:
        cache.delete(self._key)
