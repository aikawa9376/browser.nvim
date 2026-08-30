(() => {
  "use strict";

  const send = (payload) => {
    if (typeof window.__browserNvimQuery !== "function") {
      return;
    }
    window.__browserNvimQuery({
      request: JSON.stringify(payload),
      persistent: false,
      onSuccess: () => {},
      onFailure: () => {},
    });
  };

  const api = Object.create(null);
  Object.defineProperty(api, "send", {
    value: send,
    enumerable: false,
    writable: false,
    configurable: false,
  });
  Object.defineProperty(window, "__nvimBrowser", {
    value: api,
    enumerable: false,
    writable: false,
    configurable: false,
  });
})();
