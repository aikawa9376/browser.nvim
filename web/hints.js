(() => {
  "use strict";

  const api = window.__nvimBrowser;
  if (!api) {
    return;
  }

  const alphabet = "asdfghjkl";
  const selector = [
    "a",
    "button",
    "input",
    "textarea",
    "select",
    "[role=button]",
    "[onclick]",
    "[tabindex]",
  ].join(",");

  let session = null;

  const cleanup = () => {
    if (session?.root?.isConnected) {
      session.root.remove();
    }
    session = null;
  };

  const visibleRect = (element) => {
    if (!(element instanceof HTMLElement) || element.disabled) {
      return null;
    }
    if (element.hasAttribute("tabindex") && element.tabIndex < 0) {
      return null;
    }
    const style = getComputedStyle(element);
    if (style.display === "none" || style.visibility === "hidden" ||
        Number.parseFloat(style.opacity) === 0) {
      return null;
    }
    for (const rect of element.getClientRects()) {
      if (rect.width > 0 && rect.height > 0 && rect.bottom > 0 && rect.right > 0 &&
          rect.top < innerHeight && rect.left < innerWidth) {
        return rect;
      }
    }
    return null;
  };

  const labelWidth = (count) => {
    let width = 1;
    let capacity = alphabet.length;
    while (capacity < count) {
      width += 1;
      capacity *= alphabet.length;
    }
    return width;
  };

  const labelFor = (index, width) => {
    let label = "";
    for (let position = 0; position < width; position += 1) {
      label = alphabet[index % alphabet.length] + label;
      index = Math.floor(index / alphabet.length);
    }
    return label;
  };

  const overlay = (label, rect) => {
    const marker = document.createElement("span");
    marker.textContent = label.toUpperCase();
    marker.dataset.browserNvimHint = label;
    Object.assign(marker.style, {
      position: "fixed",
      left: `${Math.max(0, rect.left)}px`,
      top: `${Math.max(0, rect.top)}px`,
      padding: "1px 3px",
      border: "1px solid #111827",
      borderRadius: "2px",
      background: "#facc15",
      color: "#111827",
      font: "bold 12px/1.2 monospace",
      letterSpacing: "0",
      textTransform: "none",
      boxShadow: "0 1px 2px rgba(0,0,0,.45)",
    });
    return marker;
  };

  const start = () => {
    cleanup();
    const candidates = [];
    for (const element of document.querySelectorAll(selector)) {
      const rect = visibleRect(element);
      if (rect) {
        candidates.push({ element, rect });
      }
    }

    if (candidates.length === 0) {
      api.send({ kind: "hints_empty" });
      return;
    }

    const root = document.createElement("div");
    root.setAttribute("aria-hidden", "true");
    root.dataset.browserNvimHints = "";
    Object.assign(root.style, {
      position: "fixed",
      inset: "0",
      pointerEvents: "none",
      zIndex: "2147483647",
      contain: "strict",
    });

    const width = labelWidth(candidates.length);
    const entries = candidates.map((candidate, index) => {
      const label = labelFor(index, width);
      const marker = overlay(label, candidate.rect);
      root.append(marker);
      return { ...candidate, label, marker };
    });
    document.documentElement.append(root);
    session = { root, entries, prefix: "" };
    api.send({ kind: "hints_started", count: entries.length });
  };

  const activate = (entry) => {
    const element = entry.element;
    const tag = element.tagName.toLowerCase();
    const focusTarget = tag === "input" || tag === "textarea" || tag === "select";
    cleanup();
    if (focusTarget) {
      element.focus({ preventScroll: true });
      api.send({ kind: "hint_activated", action: "focus", tag });
      return;
    }
    api.send({ kind: "hint_activated", action: "click", tag });
    queueMicrotask(() => element.click());
  };

  const input = (key) => {
    if (!session || typeof key !== "string" || key.length !== 1 ||
        !alphabet.includes(key)) {
      cleanup();
      api.send({ kind: "hints_cancelled", reason: "invalid_key" });
      return;
    }
    session.prefix += key;
    const matches = session.entries.filter((entry) => entry.label.startsWith(session.prefix));
    for (const entry of session.entries) {
      entry.marker.style.opacity = matches.includes(entry) ? "1" : "0.18";
    }
    const exact = matches.find((entry) => entry.label === session.prefix);
    if (exact) {
      activate(exact);
    } else if (matches.length === 0) {
      cleanup();
      api.send({ kind: "hints_cancelled", reason: "no_match" });
    }
  };

  api.hints = Object.freeze({ start, input, cancel: cleanup });
})();
