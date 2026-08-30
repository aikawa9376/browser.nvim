#pragma once

#include "include/wrapper/cef_message_router.h"

namespace browser {

inline CefMessageRouterConfig BrowserMessageRouterConfig() {
  CefMessageRouterConfig config;
  config.js_query_function = "__browserNvimQuery";
  config.js_cancel_function = "__browserNvimQueryCancel";
  return config;
}

}  // namespace browser
