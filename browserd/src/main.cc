#include "app.h"

#ifdef BROWSER_WITH_CEF
#include "cef_app.h"
#include "cef_daemon.h"
#include "include/cef_app.h"
#include "include/cef_version.h"
#include "include/internal/cef_types_wrappers.h"
#endif

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifdef BROWSER_WITH_CEF
#include <unistd.h>
#endif

#ifdef BROWSER_WITH_CEF
namespace {

std::filesystem::path ProfileDirectory() {
  if (const char* configured = std::getenv("BROWSER_PROFILE_DIR");
      configured && *configured != '\0') {
    return std::filesystem::absolute(configured);
  }
  return std::filesystem::path("/tmp") /
         ("browser.nvim-profile-" + std::to_string(getuid()));
}

bool EnabledEnvironment(const char* name) {
  const char* value = std::getenv(name);
  if (!value) {
    return false;
  }
  const std::string_view text(value);
  return text == "1" || text == "true" || text == "on";
}

int RunCef(int argc, char** argv) {
  CefMainArgs main_args(argc, argv);
  CefRefPtr<browser::BrowserCefApp> app = new browser::BrowserCefApp();
  const int subprocess_code = CefExecuteProcess(main_args, app, nullptr);
  if (subprocess_code >= 0) {
    return subprocess_code;
  }

  const std::filesystem::path profile = ProfileDirectory();
  std::error_code filesystem_error;
  std::filesystem::create_directories(profile, filesystem_error);
  if (filesystem_error) {
    std::cerr << "browserd: cannot create profile directory " << profile
              << ": " << filesystem_error.message() << '\n';
    return 1;
  }

  const std::filesystem::path cef_root(BROWSER_CEF_ROOT);
  CefSettings settings;
  settings.no_sandbox = EnabledEnvironment("BROWSER_NO_SANDBOX");
  settings.windowless_rendering_enabled = true;
  settings.persist_session_cookies = true;
  settings.background_color = CefColorSetARGB(255, 255, 255, 255);
  settings.log_severity = LOGSEVERITY_WARNING;
  CefString(&settings.cache_path) = profile.string();
  CefString(&settings.root_cache_path) = profile.string();
  CefString(&settings.log_file) = (profile / "browserd-cef.log").string();
  CefString(&settings.resources_dir_path) =
      (cef_root / "Resources").string();
  CefString(&settings.locales_dir_path) =
      (cef_root / "Resources" / "locales").string();

  if (!CefInitialize(main_args, settings, app, nullptr)) {
    std::cerr << "browserd: CefInitialize failed with code "
              << CefGetExitCode() << '\n';
    return 1;
  }

  int result = 0;
  {
    browser::CefDaemon daemon(std::cin, std::cout);
    result = daemon.Run();
  }
  CefShutdown();
  return result;
}

}  // namespace
#endif

int main(int argc, char** argv) {
  bool dry_run = false;
  bool show_version = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--dry-run") {
      dry_run = true;
    } else if (argument == "--version") {
      show_version = true;
    } else if (argument == "--help") {
      std::cerr << "usage: browserd [--dry-run]\n";
      return 0;
#ifdef BROWSER_WITH_CEF
    } else if (argument.starts_with("--type=") ||
               argument.starts_with("--")) {
      // Chromium-owned switches are consumed by CefExecuteProcess.
      continue;
#endif
    } else {
      std::cerr << "browserd: unknown argument: " << argument << '\n';
      return 2;
    }
  }

  if (show_version) {
#ifdef BROWSER_WITH_CEF
    std::cout << "browserd protocol=2 cef=" << CEF_VERSION << " chromium="
              << CHROME_VERSION_MAJOR << '.' << CHROME_VERSION_MINOR << '.'
              << CHROME_VERSION_BUILD << '.' << CHROME_VERSION_PATCH << '\n';
#else
    std::cout << "browserd protocol=2 phase1-gradient\n";
#endif
    return 0;
  }
  if (dry_run) {
    browser::App app(true, std::cin, std::cout);
    return app.Run();
  }
#ifdef BROWSER_WITH_CEF
  return RunCef(argc, argv);
#else
  browser::App app(false, std::cin, std::cout);
  return app.Run();
#endif
}
