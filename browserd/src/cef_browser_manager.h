#pragma once

#include "framebuffer.h"
#include "include/cef_browser.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"
#include "json.h"
#include "kitty_renderer.h"

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace browser {

class CefEventSink {
 public:
  virtual ~CefEventSink() = default;
  virtual void Emit(JsonValue event) = 0;
  virtual void OnBrowserCountChanged() = 0;
};

struct CefBrowserState {
  std::uint32_t browser_id = 0;
  std::uint32_t anchor_image_id = 0;
  std::uint32_t anchor_placement_id = 0;
  std::uint32_t browser_image_id = 0;
  std::uint32_t browser_placement_id = 0;
  std::string url;
  std::string title;
  std::string mode = "normal";
  int columns = 1;
  int rows = 1;
  int width = 10;
  int height = 20;
  int fps = 60;
  int max_visual_hints = 300;
  bool dirty_rects = true;
  int max_dirty_rects = 32;
  double full_frame_threshold = 0.5;
  bool attached = false;
  bool visible = true;
  bool focused = false;
  bool destroying = false;
  bool page_ready = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  int last_frame_width = 0;
  int last_frame_height = 0;
  std::vector<std::string> pending_navigation_commands;
  std::vector<std::string> pending_scripts;
  Framebuffer framebuffer;
  CefRefPtr<CefBrowser> browser;
  CefRefPtr<CefClient> client;
};

class CefBrowserManager {
 public:
  CefBrowserManager(KittyRenderer* renderer, CefEventSink* sink)
      : renderer_(renderer), sink_(sink) {}
  ~CefBrowserManager();

  bool Create(const JsonValue& message, std::string* error);
  bool Resize(const JsonValue& message, std::string* error);
  bool Attach(const JsonValue& message, std::string* error);
  bool Detach(std::uint32_t browser_id, std::string* error);
  bool Destroy(std::uint32_t browser_id, std::string* error);
  void DestroyAll();

  bool SetVisibility(std::uint32_t browser_id,
                     bool visible,
                     std::string* error);
  bool SetFocus(std::uint32_t browser_id, bool focused, std::string* error);
  bool Navigate(std::uint32_t browser_id,
                std::string url,
                std::string* error);
  bool NavigationCommand(std::uint32_t browser_id,
                         std::string_view command,
                         std::string* error);
  bool Scroll(std::uint32_t browser_id,
              int dx,
              int dy,
              std::string* error);
  bool ScrollTo(std::uint32_t browser_id,
                std::string_view edge,
                std::string* error);
  bool StartHints(std::uint32_t browser_id, std::string* error);
  bool HintInput(std::uint32_t browser_id,
                 std::string_view key,
                 std::string* error);
  bool CancelHints(std::uint32_t browser_id, std::string* error);
  bool NormalMove(std::uint32_t browser_id,
                  std::string_view operation,
                  std::string* error);
  bool ActivateAtCursor(std::uint32_t browser_id, std::string* error);
  bool StartVisualAtCursor(std::uint32_t browser_id, std::string* error);
  bool StartVisual(std::uint32_t browser_id,
                   int max_hints,
                   std::string* error);
  bool VisualHintInput(std::uint32_t browser_id,
                       std::string_view key,
                       std::string* error);
  bool VisualMove(std::uint32_t browser_id,
                  std::string_view operation,
                  std::string* error);
  bool VisualYank(std::uint32_t browser_id, std::string* error);
  bool CancelVisual(std::uint32_t browser_id, std::string* error);
  bool StartInputAtCursor(std::uint32_t browser_id, std::string* error);
  bool StartInput(std::uint32_t browser_id, std::string* error);
  bool InputText(std::uint32_t browser_id,
                 std::string_view text,
                 std::string* error);
  bool InputKey(std::uint32_t browser_id,
                std::string_view key,
                bool shift,
                bool control,
                bool alt,
                std::string* error);
  bool CancelInput(std::uint32_t browser_id, std::string* error);
  bool HandleBridgeQuery(std::uint32_t browser_id,
                         CefRefPtr<CefBrowser> browser,
                         CefRefPtr<CefFrame> frame,
                         std::string_view request,
                         std::string* response,
                         std::string* error);

  CefBrowserState* Find(std::uint32_t browser_id);
  const CefBrowserState* Find(std::uint32_t browser_id) const;
  [[nodiscard]] bool empty() const { return states_.empty(); }

  void GetViewRect(std::uint32_t browser_id, CefRect* rect) const;
  void GetScreenInfo(std::uint32_t browser_id, CefScreenInfo* info) const;
  void OnPopupShow(std::uint32_t browser_id, bool show);
  void OnPopupSize(std::uint32_t browser_id, const CefRect& rect);
  void OnPaint(std::uint32_t browser_id,
               CefRenderHandler::PaintElementType type,
               const CefRenderHandler::RectList& dirty_rects,
               const void* buffer,
               int width,
               int height);
  void OnAfterCreated(std::uint32_t browser_id,
                      CefRefPtr<CefBrowser> browser);
  void OnBeforeClose(std::uint32_t browser_id);
  void OnAddressChange(std::uint32_t browser_id, std::string url);
  void OnTitleChange(std::uint32_t browser_id, std::string title);
  void OnLoadingStateChange(std::uint32_t browser_id,
                            bool loading,
                            bool can_go_back,
                            bool can_go_forward);
  void OnMainFrameNavigationStarted(std::uint32_t browser_id);

 private:
  static bool ReadPositive(const JsonValue& message,
                           std::string_view key,
                           std::uint32_t* output,
                           std::string* error);
  static bool ReadDimension(const JsonValue& message,
                            std::string_view key,
                            int* output,
                            std::string* error);
  bool Render(CefBrowserState* state, std::string* error);
  bool RenderRegions(CefBrowserState* state,
                     std::span<const PixelRect> rects,
                     std::string* error);
  bool CleanupKitty(CefBrowserState* state, std::string* error);
  void ExecuteNavigationCommand(CefBrowserState* state,
                                std::string_view command);
  static void ExecuteScript(CefBrowserState* state, std::string script);
  void SetPageReady(CefBrowserState* state, bool ready);
  void SetMode(CefBrowserState* state, std::string mode);
  void EmitError(std::uint32_t browser_id, std::string message);

  KittyRenderer* renderer_;
  CefEventSink* sink_;
  std::map<std::uint32_t, CefBrowserState> states_;
};

}  // namespace browser
