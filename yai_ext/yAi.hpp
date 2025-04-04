#define PY_SSIZE_T_CLEAN

#include <Python.h>

namespace yAi {

struct ABISettings {
  const char *xai_api_key, *xai_model, *conninfo;
};

bool InitSettings(ABISettings &abi_settings);

} // namespace yAi
