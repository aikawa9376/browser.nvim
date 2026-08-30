(() => {
  "use strict";

  const api = window.__nvimBrowser;
  if (!api) {
    return;
  }

  const alphabet = "asdfghjkl";
  let hintSession = null;
  let selectionState = null;
  let cursorState = { x: 0, y: 0 };
  let cursorGrid = { width: 10, height: 20 };
  let cursorMarker = null;
  let normalMode = false;
  let cursorRefreshPending = false;

  const excludedParent = (node) => {
    const parent = node.parentElement;
    return !parent || parent.closest("script,style,noscript") !== null;
  };

  const nodeRectangles = (node) => {
    const range = document.createRange();
    range.selectNodeContents(node);
    return Array.from(range.getClientRects());
  };

  const visibleRectangle = (rect) => rect.width > 0 && rect.height > 0 &&
    rect.bottom > 0 && rect.right > 0 && rect.top < innerHeight && rect.left < innerWidth;

  const selectableNodes = (visibleOnly = false) => {
    const nodes = [];
    const walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
    for (let node = walker.nextNode(); node; node = walker.nextNode()) {
      if (excludedParent(node) || !node.data.trim()) {
        continue;
      }
      const parent = node.parentElement;
      const style = getComputedStyle(parent);
      if (style.display === "none" || style.visibility === "hidden" ||
          Number.parseFloat(style.opacity) === 0) {
        continue;
      }
      const rectangles = nodeRectangles(node);
      if (rectangles.length === 0 || (visibleOnly && !rectangles.some(visibleRectangle))) {
        continue;
      }
      nodes.push(node);
    }
    return nodes;
  };

  const isEditableElement = (element) => {
    if (!(element instanceof HTMLElement) || element.matches(":disabled") ||
        element.hasAttribute("readonly")) {
      return false;
    }
    if (element.isContentEditable || element.matches("textarea,select")) {
      return true;
    }
    if (!element.matches("input")) {
      return false;
    }
    return [
      "text", "search", "email", "url", "tel", "password", "number",
      "date", "time", "datetime-local", "month", "week",
    ].includes(element.type);
  };

  const positionOrder = (left, right) => {
    if (left.node === right.node) {
      return Math.sign(left.offset - right.offset);
    }
    const relation = left.node.compareDocumentPosition(right.node);
    return relation & Node.DOCUMENT_POSITION_FOLLOWING ? -1 : 1;
  };

  const fallbackGraphemeBoundaries = (text) => {
    const points = Array.from(text);
    const boundaries = [0];
    let offset = 0;
    const extend = (value) => /[\p{Mark}\uFE00-\uFE0F]/u.test(value) ||
      /[\u{1F3FB}-\u{1F3FF}]/u.test(value);
    const regional = (value) => /[\u{1F1E6}-\u{1F1FF}]/u.test(value);
    for (let index = 0; index < points.length;) {
      const first = points[index];
      offset += first.length;
      index += 1;
      if (regional(first) && index < points.length && regional(points[index])) {
        offset += points[index].length;
        index += 1;
      }
      while (index < points.length) {
        if (extend(points[index])) {
          offset += points[index].length;
          index += 1;
        } else if (points[index] === "\u200d" && index + 1 < points.length) {
          offset += points[index].length + points[index + 1].length;
          index += 2;
        } else {
          break;
        }
      }
      boundaries.push(offset);
    }
    return boundaries;
  };

  const graphemeBoundaries = (text) => {
    if (typeof Intl.Segmenter !== "function") {
      return fallbackGraphemeBoundaries(text);
    }
    const segmenter = new Intl.Segmenter(undefined, { granularity: "grapheme" });
    const boundaries = [0];
    for (const segment of segmenter.segment(text)) {
      const end = segment.index + segment.segment.length;
      if (end !== boundaries[boundaries.length - 1]) {
        boundaries.push(end);
      }
    }
    return boundaries;
  };

  const wordSegments = (text) => {
    if (typeof Intl.Segmenter !== "function") {
      return Array.from(text.matchAll(/\S+/gu), (match) => ({
        start: match.index,
        end: match.index + match[0].length,
      }));
    }
    const segmenter = new Intl.Segmenter(undefined, { granularity: "word" });
    const segments = [];
    for (const segment of segmenter.segment(text)) {
      const emoji = /\p{Extended_Pictographic}/u.test(segment.segment);
      if (segment.isWordLike || emoji) {
        segments.push({
          start: segment.index,
          end: segment.index + segment.segment.length,
        });
      }
    }
    return segments;
  };

  const nextGrapheme = (position, direction) => {
    const nodes = selectableNodes(false);
    const nodeIndex = nodes.indexOf(position.node);
    if (nodeIndex < 0) {
      return position;
    }
    const boundaries = graphemeBoundaries(position.node.data);
    if (direction > 0) {
      const next = boundaries.find((offset) => offset > position.offset);
      if (next !== undefined) {
        return { node: position.node, offset: next };
      }
      for (let index = nodeIndex + 1; index < nodes.length; index += 1) {
        if (nodes[index].data.length > 0) {
          return { node: nodes[index], offset: 0 };
        }
      }
    } else {
      for (let index = boundaries.length - 1; index >= 0; index -= 1) {
        if (boundaries[index] < position.offset) {
          return { node: position.node, offset: boundaries[index] };
        }
      }
      for (let index = nodeIndex - 1; index >= 0; index -= 1) {
        if (nodes[index].data.length > 0) {
          return { node: nodes[index], offset: nodes[index].data.length };
        }
      }
    }
    return position;
  };

  const moveWord = (position, direction) => {
    const candidates = [];
    for (const node of selectableNodes(false)) {
      for (const segment of wordSegments(node.data)) {
        candidates.push({ node, offset: segment.start });
      }
    }
    if (direction > 0) {
      return candidates.find((candidate) => positionOrder(candidate, position) > 0) || position;
    }
    for (let index = candidates.length - 1; index >= 0; index -= 1) {
      if (positionOrder(candidates[index], position) < 0) {
        return candidates[index];
      }
    }
    return position;
  };

  const caretRectangle = (position) => {
    if (position.editable) {
      return position.node.getBoundingClientRect();
    }
    const range = document.createRange();
    range.setStart(position.node, position.offset);
    range.collapse(true);
    let rect = range.getBoundingClientRect();
    if (rect.height > 0) {
      return rect;
    }
    const text = position.node.data;
    if (position.offset < text.length) {
      const next = nextGrapheme(position, 1);
      if (next.node === position.node) {
        range.setEnd(position.node, next.offset);
        rect = range.getBoundingClientRect();
        return {
          left: rect.left,
          right: rect.left,
          top: rect.top,
          bottom: rect.bottom,
          width: 0,
          height: rect.height,
        };
      }
    }
    if (position.offset > 0) {
      const previous = nextGrapheme(position, -1);
      if (previous.node === position.node) {
        range.setStart(position.node, previous.offset);
        rect = range.getBoundingClientRect();
        return {
          left: rect.right,
          right: rect.right,
          top: rect.top,
          bottom: rect.bottom,
          width: 0,
          height: rect.height,
        };
      }
    }
    return rect;
  };

  const renderedPositions = () => {
    const positions = [];
    for (const node of selectableNodes(true)) {
      for (const offset of graphemeBoundaries(node.data)) {
        const position = { node, offset };
        const rect = caretRectangle(position);
        if (rect.height > 0 && rect.bottom >= -2 && rect.top <= innerHeight + 2) {
          positions.push({ position, rect });
          if (positions.length >= 6000) {
            return positions;
          }
        }
      }
    }
    return positions;
  };

  const cursorRectangle = () => ({
    left: cursorState.x,
    right: cursorState.x + cursorGrid.width,
    top: cursorState.y,
    bottom: cursorState.y + cursorGrid.height,
    width: cursorGrid.width,
    height: cursorGrid.height,
  });

  const clampCursor = () => {
    const maximumX = Math.max(0,
      Math.floor((innerWidth - cursorGrid.width) / cursorGrid.width) * cursorGrid.width);
    const maximumY = Math.max(0,
      Math.floor((innerHeight - cursorGrid.height) / cursorGrid.height) * cursorGrid.height);
    cursorState.x = Math.max(0, Math.min(maximumX, cursorState.x));
    cursorState.y = Math.max(0, Math.min(maximumY, cursorState.y));
  };

  const elementsInCursor = () => {
    const rect = cursorRectangle();
    const insetX = Math.min(2, rect.width / 4);
    const insetY = Math.min(2, rect.height / 4);
    const points = [
      [rect.left + rect.width / 2, rect.top + rect.height / 2],
      [rect.left + insetX, rect.top + rect.height / 2],
      [rect.right - insetX, rect.top + rect.height / 2],
      [rect.left + rect.width / 2, rect.top + insetY],
      [rect.left + rect.width / 2, rect.bottom - insetY],
    ];
    const elements = [];
    const seen = new Set();
    for (const [x, y] of points) {
      for (const element of document.elementsFromPoint(x, y)) {
        if (!seen.has(element)) {
          seen.add(element);
          elements.push(element);
        }
      }
    }
    return elements;
  };

  const closestCursorElement = (selector, predicate = () => true) => {
    for (const hit of elementsInCursor()) {
      const element = hit.closest?.(selector);
      if (element && predicate(element)) {
        return element;
      }
    }
    return null;
  };

  const ensureCursorMarker = () => {
    if (cursorMarker?.isConnected) {
      return cursorMarker;
    }
    cursorMarker = document.createElement("div");
    cursorMarker.setAttribute("aria-hidden", "true");
    cursorMarker.dataset.nvimBrowserCursor = "true";
    Object.assign(cursorMarker.style, {
      position: "fixed",
      pointerEvents: "none",
      zIndex: "2147483647",
      boxSizing: "border-box",
      boxShadow: "0 0 0 1px rgba(0,0,0,.75)",
      opacity: "1",
    });
    document.documentElement.append(cursorMarker);
    return cursorMarker;
  };

  const renderCursor = () => {
    if (!normalMode) {
      if (cursorMarker) {
        cursorMarker.style.display = "none";
      }
      return;
    }
    clampCursor();
    const rect = cursorRectangle();
    const marker = ensureCursorMarker();
    const editable = closestCursorElement(
      "input,textarea,select,[contenteditable]", isEditableElement);
    const actionable = closestCursorElement(
      "a[href],button,summary,[role='button'],[role='link']," +
      "input[type='button'],input[type='submit'],input[type='reset']",
      (element) => !element.matches(":disabled"),
    );
    const color = actionable ? "#22d3ee" : "#facc15";
    Object.assign(marker.style, {
      display: "block",
      left: `${Math.max(0, rect.left)}px`,
      top: `${Math.max(0, rect.top)}px`,
      width: `${rect.width}px`,
      height: `${Math.max(2, rect.height)}px`,
      background: editable || actionable ? `${color}33` : "rgba(250,204,21,.58)",
      outline: `2px solid ${color}`,
      outlineOffset: editable || actionable ? "-2px" : "-1px",
    });
  };

  const setGrid = (width, height) => {
    if (!Number.isFinite(width) || !Number.isFinite(height) ||
        width <= 0 || height <= 0) {
      return;
    }
    const column = Math.round(cursorState.x / cursorGrid.width);
    const row = Math.round(cursorState.y / cursorGrid.height);
    cursorGrid = { width, height };
    cursorState = { x: column * width, y: row * height };
    renderCursor();
  };

  const scheduleCursorRefresh = () => {
    if (!normalMode || cursorRefreshPending) {
      return;
    }
    cursorRefreshPending = true;
    requestAnimationFrame(() => {
      cursorRefreshPending = false;
      renderCursor();
    });
  };

  const applySelection = () => {
    if (!selectionState) {
      return;
    }
    const selection = getSelection();
    selection.removeAllRanges();
    if (typeof selection.setBaseAndExtent === "function") {
      selection.setBaseAndExtent(
        selectionState.anchor.node,
        selectionState.anchor.offset,
        selectionState.focus.node,
        selectionState.focus.offset,
      );
    } else {
      const range = document.createRange();
      const forward = positionOrder(selectionState.anchor, selectionState.focus) <= 0;
      const start = forward ? selectionState.anchor : selectionState.focus;
      const end = forward ? selectionState.focus : selectionState.anchor;
      range.setStart(start.node, start.offset);
      range.setEnd(end.node, end.offset);
      selection.addRange(range);
    }
    const rect = caretRectangle(selectionState.focus);
    if (rect.top < 0) {
      scrollBy(0, rect.top - Math.max(12, rect.height));
    } else if (rect.bottom > innerHeight) {
      scrollBy(0, rect.bottom - innerHeight + Math.max(12, rect.height));
    }
  };

  const verticalMove = (state, direction, apply, positions = renderedPositions) => {
    const current = caretRectangle(state.focus);
    if (current.height <= 0) {
      return;
    }
    if (state.preferredX === null) {
      state.preferredX = current.left;
    }

    const choose = () => {
      const currentCenter = current.top + current.height / 2;
      const directional = positions().filter(({ rect }) => {
        const center = rect.top + rect.height / 2;
        return direction > 0 ? center > currentCenter + 1 : center < currentCenter - 1;
      });
      if (directional.length === 0) {
        return null;
      }
      let verticalDistance = Infinity;
      for (const candidate of directional) {
        const distance = Math.abs(
          candidate.rect.top + candidate.rect.height / 2 - currentCenter,
        );
        verticalDistance = Math.min(verticalDistance, distance);
      }
      const tolerance = Math.max(2, current.height * 0.6);
      const nextLine = directional.filter(({ rect }) => Math.abs(
        Math.abs(rect.top + rect.height / 2 - currentCenter) - verticalDistance,
      ) <= tolerance);
      nextLine.sort((left, right) =>
        Math.abs(left.rect.left - state.preferredX) -
        Math.abs(right.rect.left - state.preferredX));
      return nextLine[0]?.position || null;
    };

    let target = choose();
    if (!target) {
      scrollBy(0, direction * innerHeight * 0.75);
      target = choose();
    }
    if (target) {
      state.focus = target;
      apply();
    }
  };

  const lineEdge = (state, end, apply, positions = renderedPositions) => {
    const current = caretRectangle(state.focus);
    const center = current.top + current.height / 2;
    const tolerance = Math.max(2, current.height * 0.6);
    const line = positions().filter(({ rect }) =>
      Math.abs(rect.top + rect.height / 2 - center) <= tolerance);
    if (line.length === 0) {
      return;
    }
    line.sort((left, right) => left.rect.left - right.rect.left);
    state.focus = (end ? line[line.length - 1] : line[0]).position;
    state.preferredX = null;
    apply();
  };

  const moveState = (state, operation, apply) => {
    if (operation === "previous_grapheme" || operation === "next_grapheme") {
      state.focus = nextGrapheme(
        state.focus,
        operation === "next_grapheme" ? 1 : -1,
      );
      state.preferredX = null;
      apply();
    } else if (operation === "previous_word" || operation === "next_word") {
      state.focus = moveWord(
        state.focus,
        operation === "next_word" ? 1 : -1,
      );
      state.preferredX = null;
      apply();
    } else if (operation === "down" || operation === "up") {
      verticalMove(state, operation === "down" ? 1 : -1, apply);
    } else if (operation === "line_start" || operation === "line_end") {
      lineEdge(state, operation === "line_end", apply);
    } else if (operation === "swap" && state.anchor) {
      const anchor = state.anchor;
      state.anchor = state.focus;
      state.focus = anchor;
      state.preferredX = null;
      apply();
    }
  };

  const move = (operation) => {
    if (selectionState) {
      moveState(selectionState, operation, applySelection);
    }
  };

  const wordCursorPositions = () => {
    const positions = [];
    for (const node of selectableNodes(false)) {
      for (const segment of wordSegments(node.data)) {
        const position = { node, offset: segment.start };
        const rect = caretRectangle(position);
        positions.push({
          position,
          documentX: rect.left + scrollX,
          documentY: rect.top + scrollY,
        });
      }
    }
    for (const element of document.querySelectorAll(
      "input,textarea,select,[contenteditable],a[href],button,summary," +
      "[role='button'],[role='link']",
    )) {
      if (!(element instanceof HTMLElement) || element.matches(":disabled") ||
          element.textContent.trim()) {
        continue;
      }
      const rect = element.getBoundingClientRect();
      if (rect.width > 0 && rect.height > 0) {
        positions.push({
          element,
          documentX: rect.left + scrollX,
          documentY: rect.top + scrollY,
        });
      }
    }
    positions.sort((left, right) =>
      left.documentY - right.documentY || left.documentX - right.documentX);
    return positions;
  };

  const placeCursorAtTarget = (target) => {
    let rect = target.element
      ? target.element.getBoundingClientRect()
      : caretRectangle(target.position);
    if (rect.top < 0 || rect.bottom > innerHeight) {
      scrollBy(0, rect.top - Math.max(cursorGrid.height, innerHeight / 2));
      rect = target.element
        ? target.element.getBoundingClientRect()
        : caretRectangle(target.position);
    }
    if (rect.left < 0 || rect.right > innerWidth) {
      scrollBy(rect.left - Math.max(cursorGrid.width, innerWidth / 2), 0);
      rect = target.element
        ? target.element.getBoundingClientRect()
        : caretRectangle(target.position);
    }
    cursorState.x = Math.floor(Math.max(0, rect.left) / cursorGrid.width) *
      cursorGrid.width;
    cursorState.y = Math.floor(Math.max(0, rect.top) / cursorGrid.height) *
      cursorGrid.height;
    renderCursor();
  };

  const moveByCell = (dx, dy) => {
    const maximumX = Math.max(0,
      Math.floor((innerWidth - cursorGrid.width) / cursorGrid.width) * cursorGrid.width);
    const maximumY = Math.max(0,
      Math.floor((innerHeight - cursorGrid.height) / cursorGrid.height) * cursorGrid.height);
    const nextX = cursorState.x + dx * cursorGrid.width;
    const nextY = cursorState.y + dy * cursorGrid.height;
    if (nextX < 0) {
      scrollBy(-cursorGrid.width, 0);
    } else if (nextX > maximumX) {
      scrollBy(cursorGrid.width, 0);
    } else {
      cursorState.x = nextX;
    }
    if (nextY < 0) {
      scrollBy(0, -cursorGrid.height);
    } else if (nextY > maximumY) {
      scrollBy(0, cursorGrid.height);
    } else {
      cursorState.y = nextY;
    }
    renderCursor();
  };

  const normalMove = (operation) => {
    if (!normalMode) {
      return;
    }
    if (operation === "previous_grapheme") {
      moveByCell(-1, 0);
    } else if (operation === "next_grapheme") {
      moveByCell(1, 0);
    } else if (operation === "down") {
      moveByCell(0, 1);
    } else if (operation === "up") {
      moveByCell(0, -1);
    } else if (operation === "previous_word" || operation === "next_word") {
      const pointX = cursorState.x + cursorGrid.width / 2 + scrollX;
      const pointY = cursorState.y + cursorGrid.height / 2 + scrollY;
      const positions = wordCursorPositions();
      let target = null;
      if (operation === "next_word") {
        target = positions.find((candidate) =>
          candidate.documentY > pointY + cursorGrid.height / 2 ||
          (Math.abs(candidate.documentY - pointY) <= cursorGrid.height / 2 &&
            candidate.documentX > pointX + cursorGrid.width / 2)) || null;
      } else {
        for (let index = positions.length - 1; index >= 0; index -= 1) {
          const candidate = positions[index];
          if (candidate.documentY < pointY - cursorGrid.height / 2 ||
              (Math.abs(candidate.documentY - pointY) <= cursorGrid.height / 2 &&
                candidate.documentX < pointX - cursorGrid.width / 2)) {
            target = candidate;
            break;
          }
        }
      }
      if (target) {
        placeCursorAtTarget(target);
      }
    } else if (operation === "line_start" || operation === "line_end") {
      cursorState.x = operation === "line_start" ? 0 : Number.MAX_SAFE_INTEGER;
      clampCursor();
      renderCursor();
    }
  };

  const cleanupHints = () => {
    if (hintSession?.root?.isConnected) {
      hintSession.root.remove();
    }
    hintSession = null;
  };

  const clearSelection = () => {
    cleanupHints();
    selectionState = null;
    getSelection()?.removeAllRanges();
  };

  const setNormalMode = (enabled) => {
    normalMode = enabled;
    if (enabled) {
      clearSelection();
      scheduleCursorRefresh();
    } else if (cursorMarker) {
      cursorMarker.style.display = "none";
    }
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

  const hintMarker = (label, rect) => {
    const marker = document.createElement("span");
    marker.textContent = label.toUpperCase();
    Object.assign(marker.style, {
      position: "fixed",
      left: `${Math.max(0, rect.left)}px`,
      top: `${Math.max(0, rect.top)}px`,
      padding: "1px 3px",
      border: "1px solid #312e81",
      borderRadius: "2px",
      background: "#c4b5fd",
      color: "#1e1b4b",
      font: "bold 12px/1.2 monospace",
      boxShadow: "0 1px 2px rgba(0,0,0,.45)",
    });
    return marker;
  };

  const start = (maximumHints = 300) => {
    clearSelection();
    const candidates = [];
    for (const node of selectableNodes(true)) {
      for (const segment of wordSegments(node.data)) {
        const range = document.createRange();
        range.setStart(node, segment.start);
        range.setEnd(node, segment.end);
        const rect = Array.from(range.getClientRects()).find(visibleRectangle);
        if (rect) {
          candidates.push({ node, offset: segment.start, rect });
          if (candidates.length >= maximumHints) {
            break;
          }
        }
      }
      if (candidates.length >= maximumHints) {
        break;
      }
    }
    if (candidates.length === 0) {
      api.send({ kind: "visual_empty" });
      return;
    }

    const root = document.createElement("div");
    root.setAttribute("aria-hidden", "true");
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
      const marker = hintMarker(label, candidate.rect);
      root.append(marker);
      return { ...candidate, label, marker };
    });
    document.documentElement.append(root);
    hintSession = { root, entries, prefix: "" };
    api.send({ kind: "visual_hints_started", count: entries.length });
  };

  const startAtCursor = () => {
    const rect = cursorRectangle();
    const centerX = rect.left + rect.width / 2;
    const centerY = rect.top + rect.height / 2;
    const candidates = renderedPositions();
    candidates.sort((left, right) => {
      const leftX = left.rect.left;
      const leftY = left.rect.top + left.rect.height / 2;
      const rightX = right.rect.left;
      const rightY = right.rect.top + right.rect.height / 2;
      return Math.hypot(leftX - centerX, leftY - centerY) -
        Math.hypot(rightX - centerX, rightY - centerY);
    });
    const anchor = candidates[0]?.position || null;
    if (!anchor) {
      api.send({ kind: "visual_empty" });
      return;
    }
    selectionState = {
      anchor,
      focus: nextGrapheme(anchor, 1),
      preferredX: null,
    };
    normalMode = false;
    if (cursorMarker) {
      cursorMarker.style.display = "none";
    }
    applySelection();
    api.send({ kind: "visual_started" });
  };

  const focusCursorEditable = () => {
    const element = closestCursorElement(
      "input,textarea,select,[contenteditable]", isEditableElement);
    if (!element) {
      api.send({ kind: "cursor_input_unavailable" });
      return;
    }
    element.focus({ preventScroll: true });
    api.send({ kind: "cursor_input_focused", tag: element.tagName.toLowerCase() });
  };

  const activateCursor = () => {
    const element = closestCursorElement(
      "a[href],button,summary,[role='button'],[role='link']," +
      "input[type='button'],input[type='submit'],input[type='reset']",
      (candidate) => !candidate.matches(":disabled"),
    );
    if (!(element instanceof HTMLElement) || element.matches(":disabled")) {
      api.send({ kind: "cursor_activate_unavailable" });
      return;
    }
    element.click();
  };

  const hintInput = (key) => {
    if (!hintSession || typeof key !== "string" || key.length !== 1 ||
        !alphabet.includes(key)) {
      clearSelection();
      api.send({ kind: "visual_cancelled", reason: "invalid_key" });
      return;
    }
    hintSession.prefix += key;
    const matches = hintSession.entries.filter((entry) =>
      entry.label.startsWith(hintSession.prefix));
    for (const entry of hintSession.entries) {
      entry.marker.style.opacity = matches.includes(entry) ? "1" : "0.18";
    }
    const exact = matches.find((entry) => entry.label === hintSession.prefix);
    if (exact) {
      const anchor = { node: exact.node, offset: exact.offset };
      cleanupHints();
      selectionState = {
        anchor,
        focus: nextGrapheme(anchor, 1),
        preferredX: null,
      };
      applySelection();
      api.send({ kind: "visual_started" });
    } else if (matches.length === 0) {
      clearSelection();
      api.send({ kind: "visual_cancelled", reason: "no_match" });
    }
  };

  const yank = () => {
    if (!selectionState) {
      return;
    }
    const text = getSelection()?.toString() || "";
    clearSelection();
    api.send({ kind: "visual_yank", text });
  };

  api.visual = Object.freeze({
    start,
    startAtCursor,
    hintInput,
    normalMove,
    setGrid,
    focusCursorEditable,
    activateCursor,
    setNormalMode,
    move,
    yank,
    cancel: clearSelection,
  });

  addEventListener("scroll", scheduleCursorRefresh, { passive: true });
  addEventListener("resize", scheduleCursorRefresh, { passive: true });
})();
