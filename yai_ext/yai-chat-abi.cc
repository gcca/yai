#include <filesystem>
#include <fstream>
#include <iostream>
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

static PyObject *ProcessFrame(PyObject *, PyObject *args) {
  if (!PyTuple_Check(args)) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple");
    return nullptr;
  }

  if (PyTuple_Size(args) != 3) {
    PyErr_SetString(PyExc_TypeError, "Expected a Tuple of size 3");
    return nullptr;
  }

  PyObject *history = PyTuple_GetItem(args, 0);

  if (!PyList_Check(history)) {
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

  Py_ssize_t size = PyList_Size(history);
  PyObject *last_entry = PyList_GetItem(history, size - 1);
  PyObject *last_entry_df = PyTuple_GetItem(last_entry, 2);

  const char *q_s = PyUnicode_AsUTF8(new_q);
  if (!q_s) {
    PyErr_SetString(PyExc_TypeError, "Error getting Q buffer");
    return nullptr;
  }

  PyObject *new_a = nullptr;

  if (std::string_view(q_s).starts_with("\\d")) {
    PyObject *columns = PyObject_GetAttrString(last_entry_df, "columns");
    if (!columns) {
      PyErr_SetString(PyExc_AttributeError, "Failed to get columns attribute");
      return nullptr;
    }

    std::string oss;
    oss.reserve(4096);
    oss += "<table class=\"table table-striped\"><thead><tr>"
           "<th>Column</th>"
           "<th>Type</th>"
           "<th>Non-Null Count</th>"
           "<th>Sample</th>"
           "</tr></thead><tbody>";

    PyObject *columns_iter = PyObject_GetIter(columns);

    PyObject *column;
    while ((column = PyIter_Next(columns_iter))) {
      const char *column_name = PyUnicode_AsUTF8(column);
      if (!column_name) {
        PyErr_SetString(PyExc_ValueError,
                        "Failed to get column name as UTF-8 string");
        Py_DECREF(column);
        return nullptr;
      }

      PyObject *column_series = PyObject_GetItem(last_entry_df, column);
      if (!column_series) {
        PyErr_SetString(PyExc_KeyError,
                        "Failed to get column series from DataFrame");
        Py_DECREF(column);
        return nullptr;
      }

      PyObject *column_dtype = PyObject_GetAttrString(column_series, "dtype");
      if (!column_dtype) {
        PyErr_SetString(PyExc_AttributeError,
                        "Failed to get dtype attribute from series");
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_dtype_str = PyObject_Str(column_dtype);
      if (!column_dtype_str) {
        PyErr_SetString(PyExc_TypeError, "Failed to convert dtype to string");
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      const char *column_typename = PyUnicode_AsUTF8(column_dtype_str);
      if (!column_typename) {
        PyErr_SetString(PyExc_ValueError,
                        "Failed to get dtype name as UTF-8 string");
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }

      PyObject *column_notna =
          PyObject_CallMethod(column_series, "notna", nullptr);
      if (!column_notna) {
        PyErr_SetString(PyExc_AttributeError,
                        "Failed to call notna method on series");
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_sum = PyObject_CallMethod(column_notna, "sum", nullptr);
      if (!column_sum) {
        PyErr_SetString(PyExc_AttributeError,
                        "Failed to call sum method on notna result");
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_notna_str = PyObject_Str(column_sum);
      if (!column_notna_str) {
        PyErr_SetString(PyExc_TypeError,
                        "Failed to convert sum result to string");
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      const char *column_notna_count = PyUnicode_AsUTF8(column_notna_str);
      if (!column_notna_count) {
        PyErr_SetString(PyExc_ValueError,
                        "Failed to get notna count as UTF-8 string");
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }

      PyObject *column_dropna =
          PyObject_CallMethod(column_series, "dropna", nullptr);
      if (!column_dropna) {
        PyErr_SetString(PyExc_AttributeError,
                        "Failed to call dropna method on series");
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_iloc = PyObject_GetAttrString(column_dropna, "iloc");
      if (!column_iloc) {
        PyErr_SetString(PyExc_AttributeError,
                        "Failed to get iloc attribute from dropna result");
        Py_DECREF(column_dropna);
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_0item =
          PyObject_GetItem(column_iloc, PyLong_FromLong(0));
      if (!column_0item) {
        PyErr_SetString(PyExc_IndexError, "Failed to get first item from iloc");
        Py_DECREF(column_iloc);
        Py_DECREF(column_dropna);
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      PyObject *column_0item_str = PyObject_Str(column_0item);
      if (!column_0item_str) {
        PyErr_SetString(PyExc_TypeError,
                        "Failed to convert first item to string");
        Py_DECREF(column_0item);
        Py_DECREF(column_iloc);
        Py_DECREF(column_dropna);
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }
      const char *column_sample = PyUnicode_AsUTF8(column_0item_str);
      if (!column_sample) {
        PyErr_SetString(PyExc_ValueError,
                        "Failed to get sample value as UTF-8 string");
        Py_DECREF(column_0item_str);
        Py_DECREF(column_0item);
        Py_DECREF(column_iloc);
        Py_DECREF(column_dropna);
        Py_DECREF(column_notna_str);
        Py_DECREF(column_sum);
        Py_DECREF(column_notna);
        Py_DECREF(column_dtype_str);
        Py_DECREF(column_dtype);
        Py_DECREF(column_series);
        Py_DECREF(column);
        return nullptr;
      }

      oss += "<tr><td>";
      oss += column_name;
      oss += "</td><td>";
      oss += column_typename;
      oss += "</td><td>";
      oss += column_notna_count;
      oss += "</td><td>";
      oss += column_sample;
      oss += "</td></tr>";

      Py_DECREF(column_0item);
      Py_DECREF(column_0item_str);
      Py_DECREF(column_iloc);
      Py_DECREF(column_dropna);
      Py_DECREF(column_notna_str);
      Py_DECREF(column_sum);
      Py_DECREF(column_notna);
      Py_DECREF(column_dtype_str);
      Py_DECREF(column_dtype);
      Py_DECREF(column_series);
      Py_DECREF(column);
    }

    Py_DECREF(columns_iter);
    Py_DECREF(columns);

    oss += "</tbody></table>";

    new_a = PyUnicode_FromStringAndSize(oss.data(),
                                        static_cast<Py_ssize_t>(oss.size()));
  } else {
    new_a = PyUnicode_FromString("Error processing frame");
  }

  PyObject *new_history_entry = PyTuple_Pack(3, new_q, new_a, last_entry_df);
  PyList_Append(history, new_history_entry);

  return Py_None;
}

static PyMethodDef m_methods[] = {
    {"ProcessMessage", ProcessMessage, METH_VARARGS, nullptr},
    {"ProcessPartial", ProcessPartial, METH_VARARGS, nullptr},
    {"ProcessFrame", ProcessFrame, METH_VARARGS, nullptr},
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

inline static bool SetSInputContent(const char *env_name, char *&out_buffer,
                                    std::string_view &out_content) {
  const char *input_path = std::getenv(env_name);
  if (!input_path || input_path[0] == '\0') {
    std::cerr << "\033[31mEnvironment variable " << env_name
              << " not set\033[0m" << std::endl;
    return true;
  }

  std::ifstream sinput(input_path);
  if (!sinput.is_open()) {
    std::ostringstream oss;
    oss << "Error opening file specified by " << env_name << ": " << input_path;
    PyErr_SetString(PyExc_RuntimeError, oss.str().c_str());
    return false;
  }

  try {
    auto size = std::filesystem::file_size(input_path);
    out_buffer = new char[size + 1];
    sinput.read(out_buffer, static_cast<std::streamsize>(size));
    out_buffer[size] = '\0';
    out_content = std::string_view(out_buffer, size);
  } catch (const std::bad_alloc &) {
    delete[] out_buffer;
    out_buffer = nullptr;
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

  if (!SetSInputContent("YAI_CHAT_INPUT_PATH", sinput_buffer, sinput_content))
    return nullptr;

  return PyModule_Create(&pymoduledef);
}
