from django.forms import FileField, Form


class FrameForm(Form):
    file = FileField()
