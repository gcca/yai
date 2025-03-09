#include <filesystem>
#include <fstream>
#include <sstream>
#include <xai.hpp>

#include "yAi.hpp"

static yAi::ABISettings abi_settings;

static char *sinput_buffer = nullptr;
static std::string_view sinput_content;

static PyObject *ProcessMessage(PyObject *, PyObject *args) {
  if (!PyTuple_Check(args)) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple");
    return nullptr;
  }

  if (PyTuple_Size(args) != 3) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple of size 3");
    return nullptr;
  }

  PyObject *hist = PyTuple_GetItem(args, 0);

  if (!PyList_Check(hist)) {
    PyErr_SetString(PyExc_TypeError, "Expected first List[Tuple[str, str]]");
    return nullptr;
  }

  PyObject *new_q = PyTuple_GetItem(args, 1);

  if (!PyUnicode_Check(new_q)) {
    PyErr_SetString(PyExc_TypeError, "Expected second str");
    return nullptr;
  }

  PyObject *scope = PyTuple_GetItem(args, 2);

  if (!PyDict_Check(scope) and Py_None != scope) {
    PyErr_SetString(PyExc_TypeError, "Expected third Dict[str, str]");
    return nullptr;
  }

  std::unique_ptr<xai::Client> client =
      xai::Client::Make(abi_settings.xai_api_key);

  if (!client) {
    PyErr_SetString(PyExc_RuntimeError, "Error creating client");
    return nullptr;
  }

  std::unique_ptr<xai::Messages> messages =
      xai::Messages::Make(abi_settings.xai_model);

  std::ostringstream oss;
  oss << sinput_content << '\n';

  if (scope != Py_None) {
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(scope, &pos, &key, &value)) {
      if (!PyUnicode_Check(key)) {
        PyErr_SetString(PyExc_TypeError, "Expected a str key");
        return nullptr;
      }

      if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "Expected a str value");
        return nullptr;
      }

      const char *k_s = PyUnicode_AsUTF8(key);
      const char *v_s = PyUnicode_AsUTF8(value);

      if (!k_s) {
        PyErr_SetString(PyExc_TypeError, "Error getting key buffer");
        return nullptr;
      }

      if (!v_s) {
        PyErr_SetString(PyExc_TypeError, "Error getting value buffer");
        return nullptr;
      }

      oss << '\n' << k_s << ": " << v_s;
    }
  }

  messages->AddS(oss.str());

  Py_ssize_t size = PyList_Size(hist);
  for (Py_ssize_t i = 0; i < size; ++i) {
    PyObject *pair = PyList_GetItem(hist, i);

    if (!PyTuple_Check(pair)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[str, str]");
      return nullptr;
    }

    Py_ssize_t pair_size = PyTuple_Size(pair);
    if (pair_size != 2) {
      PyErr_SetString(PyExc_TypeError,
                      "Expected a Tuple[str, str] with two elements");
      return nullptr;
    }

    PyObject *pair_q = PyTuple_GetItem(pair, 0);
    PyObject *pair_a = PyTuple_GetItem(pair, 1);

    if (!PyUnicode_Check(pair_q)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[?, str] ");
      return nullptr;
    }

    if (!PyUnicode_Check(pair_a)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[str, ?] ");
      return nullptr;
    }

    const char *q_s = PyUnicode_AsUTF8(pair_q);
    const char *a_s = PyUnicode_AsUTF8(pair_a);

    if (!q_s) {
      PyErr_SetString(PyExc_TypeError, "Error getting Q buffer");
      return nullptr;
    }

    if (!a_s) {
      PyErr_SetString(PyExc_TypeError, "Error getting A buffer");
      return nullptr;
    }

    messages->AddU(q_s);
    messages->AddA(a_s);
  }

  const char *q_s = PyUnicode_AsUTF8(new_q);
  messages->AddU(q_s);

  std::unique_ptr<xai::Choices> choices = client->ChatCompletion(messages);

  if (!choices) {
    PyErr_SetString(PyExc_RuntimeError, "Error getting choices");
    return nullptr;
  }

  std::string_view view = choices->first();

  PyObject *res = PyDict_New();

  if (view.starts_with("---FIN---")) {
    std::size_t taskname_end = view.find('\n', 10);
    std::string_view taskname_sv = view.substr(10, taskname_end - 10);

    PyObject *taskname = PyUnicode_FromStringAndSize(
        taskname_sv.data(), static_cast<Py_ssize_t>(taskname_sv.size()));

    if (!taskname) {
      PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
      return nullptr;
    }

    PyDict_SetItemString(res, "taskname", taskname);

    std::size_t pos = taskname_end + 1;
    std::size_t args_end = view.find("---AWK---", pos);

    PyObject *pargs = PyDict_New();

    while (pos < args_end) {
      std::size_t argname_end = view.find(':', pos);
      std::string_view argname_sv = view.substr(pos, argname_end - pos);
      pos = argname_end + 1;

      std::size_t arg_end = view.find('\n', pos);
      std::string_view arg_sv = view.substr(pos, arg_end - pos);

      while (arg_sv.size() > 0 && arg_sv[0] == ' ') {
        arg_sv.remove_prefix(1);
      }

      PyObject *arg = PyUnicode_FromStringAndSize(
          arg_sv.data(), static_cast<Py_ssize_t>(arg_sv.size()));

      if (!arg) {
        PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
        return nullptr;
      }

      const_cast<char *>(argname_sv.data())[argname_sv.size()] = '\0';

      PyDict_SetItemString(pargs, argname_sv.data(), arg);

      pos = arg_end + 1;
    }

    PyDict_SetItemString(res, "args", pargs);

    pos = args_end + 10;
    view = view.substr(pos);
  }

  PyObject *new_a = PyUnicode_FromStringAndSize(
      view.data(), static_cast<Py_ssize_t>(view.size()));

  if (!new_a) {
    PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
    return nullptr;
  }

  PyObject *new_pair = PyTuple_Pack(2, new_q, new_a);
  PyList_Append(hist, new_pair);

  return res;
}

static PyObject *ProcessPartial(PyObject *, PyObject *args) {
  if (!PyTuple_Check(args)) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple");
    return nullptr;
  }

  if (PyTuple_Size(args) != 4) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple of size 3");
    return nullptr;
  }

  PyObject *hist = PyTuple_GetItem(args, 0);

  if (!PyList_Check(hist)) {
    PyErr_SetString(PyExc_TypeError, "Expected first List[Tuple[str, str]]");
    return nullptr;
  }

  PyObject *new_q = PyTuple_GetItem(args, 1);

  if (!PyUnicode_Check(new_q)) {
    PyErr_SetString(PyExc_TypeError, "Expected second str");
    return nullptr;
  }

  PyObject *scope = PyTuple_GetItem(args, 2);

  if (!PyDict_Check(scope) and Py_None != scope) {
    PyErr_SetString(PyExc_TypeError, "Expected third Dict[str, str]");
    return nullptr;
  }

  PyObject *call = PyTuple_GetItem(args, 3);

  if (!PyCallable_Check(call)) {
    PyErr_SetString(PyExc_TypeError, "Expected forth call");
    return nullptr;
  }

  std::unique_ptr<xai::Client> client =
      xai::Client::Make(abi_settings.xai_api_key);

  if (!client) {
    PyErr_SetString(PyExc_RuntimeError, "Error creating client");
    return nullptr;
  }

  std::unique_ptr<xai::Messages> messages =
      xai::Messages::Make(abi_settings.xai_model);

  std::ostringstream oss;
  oss << sinput_content << '\n';

  if (scope != Py_None) {
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    while (PyDict_Next(scope, &pos, &key, &value)) {
      if (!PyUnicode_Check(key)) {
        PyErr_SetString(PyExc_TypeError, "Expected a str key");
        return nullptr;
      }

      if (!PyUnicode_Check(value)) {
        PyErr_SetString(PyExc_TypeError, "Expected a str value");
        return nullptr;
      }

      const char *k_s = PyUnicode_AsUTF8(key);
      const char *v_s = PyUnicode_AsUTF8(value);

      if (!k_s) {
        PyErr_SetString(PyExc_TypeError, "Error getting key buffer");
        return nullptr;
      }

      if (!v_s) {
        PyErr_SetString(PyExc_TypeError, "Error getting value buffer");
        return nullptr;
      }

      oss << '\n' << k_s << ": " << v_s;
    }
  }

  messages->AddS(oss.str());

  Py_ssize_t size = PyList_Size(hist);
  for (Py_ssize_t i = 0; i < size; ++i) {
    PyObject *pair = PyList_GetItem(hist, i);

    if (!PyTuple_Check(pair)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[str, str]");
      return nullptr;
    }

    Py_ssize_t pair_size = PyTuple_Size(pair);
    if (pair_size != 2) {
      PyErr_SetString(PyExc_TypeError,
                      "Expected a Tuple[str, str] with two elements");
      return nullptr;
    }

    PyObject *pair_q = PyTuple_GetItem(pair, 0);
    PyObject *pair_a = PyTuple_GetItem(pair, 1);

    if (!PyUnicode_Check(pair_q)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[?, str] ");
      return nullptr;
    }

    if (!PyUnicode_Check(pair_a)) {
      PyErr_SetString(PyExc_TypeError, "Expected a Tuple[str, ?] ");
      return nullptr;
    }

    const char *q_s = PyUnicode_AsUTF8(pair_q);
    const char *a_s = PyUnicode_AsUTF8(pair_a);

    if (!q_s) {
      PyErr_SetString(PyExc_TypeError, "Error getting Q buffer");
      return nullptr;
    }

    if (!a_s) {
      PyErr_SetString(PyExc_TypeError, "Error getting A buffer");
      return nullptr;
    }

    messages->AddU(q_s);
    messages->AddA(a_s);
  }

  const char *q_s = PyUnicode_AsUTF8(new_q);
  messages->AddU(q_s);

  bool reserve = false;
  std::string acc;
  acc.reserve(128);

  client->ChatCompletion(messages, [&](std::unique_ptr<xai::Choices> choices) {
    if (!choices) {
      PyErr_SetString(PyExc_RuntimeError, "Error getting choices");
      return;
    }

    std::string_view rview = choices->first();

    acc.append(rview.data(), rview.size());

    if (acc.size() < 10) {
      return;
    }

    if (acc.starts_with("---FIN---")) {
      reserve = true;
    }

    if (reserve) {
      return;
    }

    std::string_view view = acc;

    PyObject *new_a = PyUnicode_FromStringAndSize(
        view.data(), static_cast<Py_ssize_t>(view.size()));

    if (!new_a) {
      PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
      return;
    }

    PyObject_CallFunctionObjArgs(call, new_a, nullptr);

    acc.clear();
  });

  PyObject *ref = Py_None;

  if (!acc.empty()) {
    PyObject *res = PyDict_New();

    std::string_view view = acc;

    if (view.starts_with("---FIN---")) {
      std::size_t taskname_end = view.find('\n', 10);
      std::string_view taskname_sv = view.substr(10, taskname_end - 10);

      PyObject *taskname = PyUnicode_FromStringAndSize(
          taskname_sv.data(), static_cast<Py_ssize_t>(taskname_sv.size()));

      if (!taskname) {
        PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
        return nullptr;
      }

      PyDict_SetItemString(res, "taskname", taskname);

      std::size_t pos = taskname_end + 1;
      std::size_t args_end = view.find("---AWK---", pos);

      PyObject *pargs = PyDict_New();

      while (pos < args_end) {
        std::size_t argname_end = view.find(':', pos);
        std::string_view argname_sv = view.substr(pos, argname_end - pos);
        pos = argname_end + 1;

        std::size_t arg_end = view.find('\n', pos);
        std::string_view arg_sv = view.substr(pos, arg_end - pos);

        while (arg_sv.size() > 0 && arg_sv[0] == ' ') {
          arg_sv.remove_prefix(1);
        }

        PyObject *arg = PyUnicode_FromStringAndSize(
            arg_sv.data(), static_cast<Py_ssize_t>(arg_sv.size()));

        if (!arg) {
          PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
          return nullptr;
        }

        const_cast<char *>(argname_sv.data())[argname_sv.size()] = '\0';

        PyDict_SetItemString(pargs, argname_sv.data(), arg);

        pos = arg_end + 1;
      }

      PyDict_SetItemString(res, "args", pargs);

      pos = args_end + 10;
      view = view.substr(pos);

      ref = res;
    }

    PyObject *new_a = PyUnicode_FromStringAndSize(
        view.data(), static_cast<Py_ssize_t>(view.size()));

    if (!new_a) {
      PyErr_SetString(PyExc_RuntimeError, "Error creating bytes");
      return nullptr;
    }

    PyObject_CallFunctionObjArgs(call, new_a, nullptr);
  }

  return ref;
}

static PyMethodDef m_methods[] = {
    {"ProcessMessage", ProcessMessage, METH_VARARGS, nullptr},
    {"ProcessPartial", ProcessPartial, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr}};

static struct PyModuleDef pymoduledef = {PyModuleDef_HEAD_INIT,
                                         "yai_chat_abi",
                                         "yAI Chat ABI Module",
                                         -1,
                                         m_methods,
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         nullptr};

inline static bool SetSInputContent() {
  const char *input_path = std::getenv("YAI_CHAT_INPUT_PATH");
  if (!input_path || input_path[0] == '\0') {
    PyErr_SetString(PyExc_RuntimeError,
                    "Environment variable YAI_CHAT_INPUT_PATH not set");
    return false;
  }

  std::ifstream sinput(input_path);
  if (!sinput.is_open()) {
    std::ostringstream oss;
    oss << "Error opening file specified by YAI_CHAT_INPUT_PATH: "
        << input_path;
    PyErr_SetString(PyExc_RuntimeError, oss.str().c_str());
    return false;
  }

  try {
    auto sinput_size = std::filesystem::file_size(input_path);
    sinput_buffer = new char[sinput_size + 1];
    sinput.read(sinput_buffer, static_cast<std::streamsize>(sinput_size));
    sinput_buffer[sinput_size] = '\0';
    sinput_content = std::string_view(sinput_buffer, sinput_size);
  } catch (const std::bad_alloc &) {
    delete[] sinput_buffer;
    sinput_buffer = nullptr;
    PyErr_SetString(PyExc_RuntimeError, "Failed to read sinput_content");
    sinput.close();
    return false;
  }

  sinput.close();

  return true;
}

PyMODINIT_FUNC PyInit_yai_chat_abi(void) {
  if (yAi::InitSettings(abi_settings))
    return nullptr;

  if (!SetSInputContent())
    return nullptr;

  return PyModule_Create(&pymoduledef);
}
