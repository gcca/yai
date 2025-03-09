from django.http import HttpResponse


async def index(_) -> HttpResponse:
    return HttpResponse("Welcome to Booking! 😊".encode())
