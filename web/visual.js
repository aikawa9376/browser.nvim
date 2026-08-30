(() => {
  "use strict";

  const api = window.__nvimBrowser;
  if (!api) {
    return;
  }

  const alphabet = "asdfghjkl";
  let hintSession = null;
  let selectionState = null;

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

  const verticalMove = (direction) => {
    const current = caretRectangle(selectionState.focus);
    if (current.height <= 0) {
      return;
    }
    if (selectionState.preferredX === null) {
      selectionState.preferredX = current.left;
    }

    const choose = () => {
      const currentCenter = current.top + current.height / 2;
      const directional = renderedPositions().filter(({ rect }) => {
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
        Math.abs(left.rect.left - selectionState.preferredX) -
        Math.abs(right.rect.left - selectionState.preferredX));
      return nextLine[0]?.position || null;
    };

    let target = choose();
    if (!target) {
      scrollBy(0, direction * innerHeight * 0.75);
      target = choose();
    }
    if (target) {
      selectionState.focus = target;
      applySelection();
    }
  };

  const lineEdge = (end) => {
    const current = caretRectangle(selectionState.focus);
    const center = current.top + current.height / 2;
    const tolerance = Math.max(2, current.height * 0.6);
    const line = renderedPositions().filter(({ rect }) =>
      Math.abs(rect.top + rect.height / 2 - center) <= tolerance);
    if (line.length === 0) {
      return;
    }
    line.sort((left, right) => left.rect.left - right.rect.left);
    selectionState.focus = (end ? line[line.length - 1] : line[0]).position;
    selectionState.preferredX = null;
    applySelection();
  };

  const move = (operation) => {
    if (!selectionState) {
      return;
    }
    if (operation === "previous_grapheme" || operation === "next_grapheme") {
      selectionState.focus = nextGrapheme(
        selectionState.focus,
        operation === "next_grapheme" ? 1 : -1,
      );
      selectionState.preferredX = null;
      applySelection();
    } else if (operation === "previous_word" || operation === "next_word") {
      selectionState.focus = moveWord(
        selectionState.focus,
        operation === "next_word" ? 1 : -1,
      );
      selectionState.preferredX = null;
      applySelection();
    } else if (operation === "down" || operation === "up") {
      verticalMove(operation === "down" ? 1 : -1);
    } else if (operation === "line_start" || operation === "line_end") {
      lineEdge(operation === "line_end");
    } else if (operation === "swap") {
      const anchor = selectionState.anchor;
      selectionState.anchor = selectionState.focus;
      selectionState.focus = anchor;
      selectionState.preferredX = null;
      applySelection();
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
    hintInput,
    move,
    yank,
    cancel: clearSelection,
  });
})();
