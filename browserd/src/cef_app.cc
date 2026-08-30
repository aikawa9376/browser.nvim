#include "cef_app.h"

#include "cef_bridge.h"
#include "embedded_scripts.h"
#include "include/wrapper/cef_helpers.h"

namespace browser {

void BrowserCefApp::OnWebKitInitialized() {
  CEF_REQUIRE_RENDERER_THREAD();
  renderer_router_ =
      CefMessageRouterRendererSide::Create(BrowserMessageRouterConfig());
}

void BrowserCefApp::OnContextCreated(CefRefPtr<CefBrowser> browser,
                                     CefRefPtr<CefFrame> frame,
                                     CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  renderer_router_->OnContextCreated(browser, frame, context);
  if (frame->IsMain()) {
    frame->ExecuteJavaScript(kBridgeScript, frame->GetURL(), 0);
  }
}

void BrowserCefApp::OnContextReleased(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefV8Context> context) {
  CEF_REQUIRE_RENDERER_THREAD();
  renderer_router_->OnContextReleased(browser, frame, context);
}

bool BrowserCefApp::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_RENDERER_THREAD();
  return renderer_router_->OnProcessMessageReceived(
      browser, frame, source_process, message);
}

}  // namespace browser
