#include "cef_browser_client.h"

#include "cef_bridge.h"
#include "cef_browser_manager.h"

namespace browser {

CefBrowserClient::CefBrowserClient(std::uint32_t browser_id,
                                   CefBrowserManager* manager)
    : browser_id_(browser_id), manager_(manager) {
  browser_router_ =
      CefMessageRouterBrowserSide::Create(BrowserMessageRouterConfig());
  handler_registered_ = browser_router_->AddHandler(this, false);
}

CefBrowserClient::~CefBrowserClient() {
  if (handler_registered_) {
    browser_router_->RemoveHandler(this);
  }
}

void CefBrowserClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  manager_->GetViewRect(browser_id_, &rect);
}

bool CefBrowserClient::GetScreenInfo(CefRefPtr<CefBrowser>,
                                     CefScreenInfo& screen_info) {
  manager_->GetScreenInfo(browser_id_, &screen_info);
  return true;
}

void CefBrowserClient::OnPopupShow(CefRefPtr<CefBrowser>, bool show) {
  manager_->OnPopupShow(browser_id_, show);
}

void CefBrowserClient::OnPopupSize(CefRefPtr<CefBrowser>,
                                   const CefRect& rect) {
  manager_->OnPopupSize(browser_id_, rect);
}

void CefBrowserClient::OnPaint(CefRefPtr<CefBrowser>,
                               PaintElementType type,
                               const RectList& dirty_rects,
                               const void* buffer,
                               int width,
                               int height) {
  manager_->OnPaint(browser_id_, type, dirty_rects, buffer, width, height);
}

void CefBrowserClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  manager_->OnAfterCreated(browser_id_, browser);
}

void CefBrowserClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  browser_router_->OnBeforeClose(browser);
  if (handler_registered_) {
    browser_router_->RemoveHandler(this);
    handler_registered_ = false;
  }
  manager_->OnBeforeClose(browser_id_);
}

void CefBrowserClient::OnAddressChange(CefRefPtr<CefBrowser>,
                                       CefRefPtr<CefFrame> frame,
                                       const CefString& url) {
  if (frame && frame->IsMain()) {
    manager_->OnAddressChange(browser_id_, url.ToString());
  }
}

void CefBrowserClient::OnTitleChange(CefRefPtr<CefBrowser>,
                                     const CefString& title) {
  manager_->OnTitleChange(browser_id_, title.ToString());
}

void CefBrowserClient::OnLoadingStateChange(CefRefPtr<CefBrowser>,
                                            bool is_loading,
                                            bool can_go_back,
                                            bool can_go_forward) {
  manager_->OnLoadingStateChange(browser_id_, is_loading, can_go_back,
                                 can_go_forward);
}

bool CefBrowserClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefRefPtr<CefRequest>,
                                      bool,
                                      bool) {
  browser_router_->OnBeforeBrowse(browser, frame);
  return false;
}

void CefBrowserClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                                 TerminationStatus,
                                                 int,
                                                 const CefString&) {
  browser_router_->OnRenderProcessTerminated(browser);
}

bool CefBrowserClient::OnProcessMessageReceived(
    CefRefPtr<CefBrowser> browser,
    CefRefPtr<CefFrame> frame,
    CefProcessId source_process,
    CefRefPtr<CefProcessMessage> message) {
  return browser_router_->OnProcessMessageReceived(
      browser, frame, source_process, message);
}

bool CefBrowserClient::OnQuery(CefRefPtr<CefBrowser> browser,
                               CefRefPtr<CefFrame> frame,
                               int64_t,
                               const CefString& request,
                               bool,
                               CefRefPtr<Callback> callback) {
  std::string response;
  std::string error;
  if (manager_->HandleBridgeQuery(browser_id_, browser, frame,
                                  request.ToString(), &response, &error)) {
    callback->Success(response);
  } else {
    callback->Failure(1, error);
  }
  return true;
}

}  // namespace browser
