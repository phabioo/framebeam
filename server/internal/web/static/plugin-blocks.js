// FrameBeam Hub: read-only renderer of plugin building blocks (docs/design/plugins.md "Building blocks",
// docs/architecture/12-plugins.md 16.8). The Player validates a board tree and publishes it to the Hub; this file
// draws the same catalog for the Runs page. No framework, no network access.
//
// Safety rules (the board is written by a plugin, never trust it):
//  - every plugin string reaches the page through textContent, never through HTML parsing or attributes that are parsed;
//  - an image is shown only for a data:image/png;base64 source, everything else is a neutral placeholder;
//  - interactive blocks (button, toggle, select, segmented, textfield) are rendered disabled, no action is ever sent;
//  - unknown block types, too deep or too large trees render a neutral placeholder instead of failing.
(function (root) {
  "use strict";

  var MAX_DEPTH = 24;      // same limits as the Player validator (client/plugins/plugin_ui.h)
  var MAX_NODES = 2000;
  var GAPS = [4, 8, 12, 16, 20];
  var DATA_PNG = /^data:image\/png;base64,[A-Za-z0-9+\/]*={0,2}$/;
  var TONES = { neutral: 1, info: 1, ok: 1, warning: 1, danger: 1 };
  var MAX_PLAYER_COLORS = 8;

  function has(o, k) { return Object.prototype.hasOwnProperty.call(o, k); }
  function isObj(v) { return v !== null && typeof v === "object" && !Array.isArray(v); }
  function str(v) {
    if (typeof v === "string") return v;
    if (typeof v === "number" || typeof v === "boolean") return String(v);
    return "";
  }
  function num(v, lo, hi, dflt) {
    if (typeof v !== "number" || !isFinite(v)) return dflt;
    return Math.max(lo, Math.min(hi, v));
  }
  function oneOf(v, list, dflt) { return typeof v === "string" && list.indexOf(v) >= 0 ? v : dflt; }
  function tone(v) { return typeof v === "string" && has(TONES, v) ? v : ""; }
  function list(v) { return Array.isArray(v) ? v : []; }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined && text !== null && text !== "") e.textContent = String(text);
    return e;
  }
  function add(parent, child) { parent.appendChild(child); return child; }

  function gapClass(v) {
    var g = GAPS[0];
    if (typeof v === "number" && isFinite(v)) {
      for (var i = 0; i < GAPS.length; i++) if (v >= GAPS[i]) g = GAPS[i];
    }
    return "pb-gap-" + g;
  }

  function playerClass(c) {
    var n = Math.floor(num(c, 1, MAX_PLAYER_COLORS, 1));
    return "pb-pc pb-pc-" + n;
  }

  function unsupported(type) {
    var e = el("div", "pb-unsupported");
    e.setAttribute("role", "note");
    var label = "Unsupported block";
    if (typeof type === "string" && type !== "") label += ": " + type.slice(0, 40);
    e.textContent = label;
    return e;
  }

  // The image source is the only attribute that carries plugin data into the DOM, and only after this check.
  function pngSource(src) {
    return typeof src === "string" && src.length < 2000000 && DATA_PNG.test(src) ? src : "";
  }

  function children(parent, node, ctx, depth) {
    var kids = list(node.children);
    for (var i = 0; i < kids.length; i++) add(parent, render(kids[i], ctx, depth + 1));
    return parent;
  }

  function image(src, sizeClass, state, label) {
    var wrap = el("span", "pb-image " + sizeClass + (state ? " pb-image-" + state : ""));
    var ok = pngSource(src);
    if (ok) {
      var img = el("img", "pb-image-img");
      img.src = ok;
      img.alt = label || "";
      img.decoding = "async";
      add(wrap, img);
    } else {
      wrap.classList.add("pb-image-none");
      wrap.setAttribute("role", "img");
      wrap.setAttribute("aria-label", label || "No image");
    }
    if (state === "dead") add(wrap, el("span", "pb-image-cross", "✕"));
    return wrap;
  }

  function disabled(e) {
    e.disabled = true;
    e.setAttribute("aria-disabled", "true");
    return e;
  }

  var R = {};

  // ---- layout
  R.column = function (n, ctx, d) {
    var e = el("div", "pb-column " + gapClass(n.gap) + " pb-align-" + oneOf(n.align, ["start", "center", "end", "stretch"], "stretch"));
    return children(e, n, ctx, d);
  };
  R.row = function (n, ctx, d) {
    var e = el("div", "pb-row " + gapClass(n.gap) + " pb-align-" + oneOf(n.align, ["start", "center", "end", "stretch"], "center"));
    return children(e, n, ctx, d);
  };
  R.grid = function (n, ctx, d) {
    var e = el("div", "pb-grid " + gapClass(n.gap));
    e.style.gridTemplateColumns = "repeat(" + Math.floor(num(n.columns, 1, 6, 1)) + ", minmax(0, 1fr))";
    return children(e, n, ctx, d);
  };
  R.tabs = function (n, ctx, d) {
    var e = el("div", "pb-tabs");
    var labels = list(n.tabs), kids = list(n.children);
    var bar = add(e, el("div", "pb-tabbar"));
    bar.setAttribute("role", "tablist");
    var panels = [], buttons = [];
    var selected = Math.floor(num(n.selected, 1, Math.max(1, labels.length), 1)) - 1;
    function show(i) {
      for (var k = 0; k < panels.length; k++) {
        panels[k].hidden = k !== i;
        buttons[k].classList.toggle("pb-active", k === i);
        buttons[k].setAttribute("aria-selected", k === i ? "true" : "false");
      }
    }
    for (var i = 0; i < labels.length; i++) {
      (function (idx) {
        // Switching tabs only changes what the page shows; no plugin action is triggered.
        var b = add(bar, el("button", "pb-tab", str(labels[idx])));
        b.type = "button";
        b.setAttribute("role", "tab");
        b.addEventListener("click", function () { show(idx); });
        buttons.push(b);
        var p = add(e, el("div", "pb-tabpanel"));
        p.setAttribute("role", "tabpanel");
        if (idx < kids.length) add(p, render(kids[idx], ctx, d + 1));
        panels.push(p);
      })(i);
    }
    show(Math.min(selected, Math.max(0, panels.length - 1)));
    return e;
  };
  R.scroll = function (n, ctx, d) {
    var e = el("div", "pb-scroll pb-scroll-" + oneOf(n.max_height, ["s", "m", "l"], "none"));
    e.tabIndex = 0;
    return children(e, n, ctx, d);
  };
  R.card = function (n, ctx, d) {
    var e = el("div", "pb-card");
    if (n.eyebrow || n.title || n.link_label) {
      var head = add(e, el("div", "pb-card-head"));
      var left = add(head, el("div", "pb-card-titles"));
      if (n.eyebrow) add(left, el("div", "pb-eyebrow", str(n.eyebrow)));
      if (n.title) add(left, el("div", "pb-card-title", str(n.title)));
      if (n.link_label) add(head, el("div", "pb-link", str(n.link_label) + " ›"));
    }
    children(add(e, el("div", "pb-card-body")), n, ctx, d);
    return e;
  };
  R.section = function (n, ctx, d) {
    var e = el("div", "pb-section");
    var head = add(e, el("div", "pb-section-head"));
    add(head, el("div", "pb-eyebrow", str(n.title)));
    if (n.link_label) add(head, el("div", "pb-link", str(n.link_label) + " ›"));
    children(e, n, ctx, d);
    return e;
  };
  R.location = function (n, ctx, d) {
    var status = oneOf(n.status, ["open", "encounter", "caught", "missed", "none"], "none");
    var t = status === "open" || status === "encounter" ? "info" : status === "caught" ? "ok" : "neutral";
    var labels = { open: "Open", encounter: "Encounter now", caught: "Caught", missed: "Missed", none: "No route" };
    var e = el("div", "pb-card pb-location pb-line-" + t);
    var head = add(e, el("div", "pb-card-head"));
    var left = add(head, el("div", "pb-card-titles"));
    add(left, el("div", "pb-card-title", str(n.title)));
    if (n.sub) add(left, el("div", "pb-meta", str(n.sub)));
    add(head, el("span", "pb-badge pb-tone-" + t, labels[status]));
    children(add(e, el("div", "pb-card-body")), n, ctx, d);
    return e;
  };
  R.empty = function (n) {
    var e = el("div", "pb-empty" + (n.tone === "danger" ? " pb-tone-danger-text" : ""));
    add(e, el("div", "pb-empty-title", str(n.title)));
    if (n.text) add(e, el("div", "pb-meta", str(n.text)));
    if (n.action_label) {
      var b = add(e, disabled(el("button", "pb-button pb-button-" + (n.tone === "danger" ? "secondary" : "primary"), str(n.action_label))));
      b.type = "button";
    }
    return e;
  };

  // ---- content
  R.text = function (n) {
    var st = oneOf(n.style, ["title", "heading", "body", "meta", "eyebrow", "value"], "body");
    var t = tone(n.tone);
    return el("div", "pb-text pb-text-" + st + (t ? " pb-fg-" + t : "") + (n.wrap === false ? " pb-nowrap" : ""), str(n.text));
  };
  R.chip = function (n) {
    var t = tone(n.tone) || "neutral";
    return el("span", "pb-chip pb-line-" + t + (n.selected === true ? " pb-chip-selected" : ""), str(n.text));
  };
  R.badge = function (n) {
    return el("span", "pb-badge pb-tone-" + (tone(n.tone) || "neutral"), str(n.text));
  };
  R.avatar = function (n) {
    var initial = Array.from(str(n.initial))[0] || "";
    return el("span", "pb-avatar pb-avatar-" + oneOf(n.size, ["s", "m", "l"], "m") + " " + playerClass(n.color), initial.toUpperCase());
  };
  R.image = function (n) {
    var e = el("span", "pb-image-row");
    var wrap = add(e, image(n.src, "pb-image-" + oneOf(n.size, ["l", "m", "s"], "m"), oneOf(n.state, ["alive", "dead", "missed", "empty"], ""), str(n.label)));
    if (typeof n.owner === "number") add(wrap, el("span", "pb-owner-dot " + playerClass(n.owner)));
    if (n.label) add(e, el("span", "pb-meta", str(n.label)));
    return e;
  };
  R.bar = function (n) {
    var v = num(n.value, 0, 1, 0);
    var t = tone(n.tone) || (v > 0.5 ? "ok" : v >= 0.2 ? "warning" : "danger");
    var e = el("div", "pb-bar-wrap");
    if (n.label) add(e, el("div", "pb-meta", str(n.label)));
    var track = add(e, el("div", "pb-bar-track"));
    track.setAttribute("role", "progressbar");
    track.setAttribute("aria-valuemin", "0");
    track.setAttribute("aria-valuemax", "100");
    track.setAttribute("aria-valuenow", String(Math.round(v * 100)));
    var fill = add(track, el("div", "pb-bar-fill pb-bg-" + t));
    fill.style.width = (v * 100).toFixed(1) + "%";
    return e;
  };
  R.stat = function (n) {
    var e = el("div", "pb-stat");
    add(e, el("span", "pb-stat-label", str(n.label)));
    add(e, el("span", "pb-stat-value", str(n.value)));
    if (n.action) add(e, el("span", "pb-faint", "›"));
    return e;
  };
  R.list = function (n) {
    var ul = el("ul", "pb-list");
    var rows = list(n.rows);
    for (var i = 0; i < rows.length; i++) {
      var r = isObj(rows[i]) ? rows[i] : {};
      var picked = r.id !== undefined && n.selected !== undefined && String(r.id) === String(n.selected);
      var li = add(ul, el("li", "pb-list-row" + (picked ? " pb-list-selected" : "")));
      if (r.image) add(li, image(r.image, "pb-image-s", "", ""));
      var mid = add(li, el("div", "pb-list-text"));
      add(mid, el("div", "pb-list-title", str(r.title)));
      if (r.sub) add(mid, el("div", "pb-meta", str(r.sub)));
      if (r.meta) add(li, el("div", "pb-meta" + (tone(r.tone) ? " pb-fg-" + tone(r.tone) : ""), str(r.meta)));
    }
    return ul;
  };
  R.table = function (n) {
    var wrap = el("div", "pb-table-wrap");
    var t = add(wrap, el("table", "pb-table"));
    var cols = list(n.columns);
    var hr = add(add(t, el("thead")), el("tr"));
    for (var c = 0; c < cols.length; c++) add(hr, el("th", "", str(cols[c])));
    var body = add(t, el("tbody"));
    var rows = list(n.rows);
    for (var i = 0; i < rows.length; i++) {
      var tr = add(body, el("tr"));
      var cells = isObj(rows[i]) ? list(rows[i].cells) : [];
      for (var k = 0; k < cells.length; k++) add(tr, el("td", "", str(cells[k])));
    }
    return wrap;
  };

  // ---- interaction (shown, never usable)
  R.button = function (n) {
    var b = disabled(el("button", "pb-button pb-button-" + oneOf(n.variant, ["primary", "secondary", "quiet", "danger"], "secondary"), str(n.label)));
    b.type = "button";
    return b;
  };
  R.toggle = function (n) {
    var l = el("label", "pb-toggle");
    var i = add(l, disabled(el("input")));
    i.type = "checkbox";
    i.checked = n.checked === true;
    if (n.label) add(l, el("span", "", str(n.label)));
    return l;
  };
  R.select = function (n) {
    var s = disabled(el("select", "pb-select"));
    var opts = list(n.options);
    for (var i = 0; i < opts.length; i++) {
      var o = isObj(opts[i]) ? opts[i] : {};
      var op = add(s, el("option", "", str(o.label)));
      op.value = String(i);
      if (n.selected !== undefined && String(o.value) === String(n.selected)) op.selected = true;
    }
    return s;
  };
  R.segmented = function (n) {
    var e = el("div", "pb-segmented");
    e.setAttribute("role", "group");
    var opts = list(n.options);
    for (var i = 0; i < opts.length; i++) {
      var o = isObj(opts[i]) ? opts[i] : {};
      var b = add(e, disabled(el("button", "pb-seg" + (n.selected !== undefined && String(o.value) === String(n.selected) ? " pb-active" : ""), str(o.label))));
      b.type = "button";
    }
    return e;
  };
  R.textfield = function (n) {
    var i = disabled(el("input", "pb-textfield" + (n.error ? " pb-textfield-error" : "")));
    i.type = "text";
    i.readOnly = true;
    i.value = str(n.text);
    if (n.placeholder) i.placeholder = str(n.placeholder);
    if (!n.error) return i;
    var w = el("div", "pb-field");
    add(w, i);
    add(w, el("div", "pb-meta pb-fg-danger", str(n.error)));
    return w;
  };

  // ---- map: static picture of the markers; positions are in image pixels
  R.map = function (n) {
    var iw = Math.max(1, num(n.image_w, 1, 16384, 1)), ih = Math.max(1, num(n.image_h, 1, 16384, 1));
    var e = el("div", "pb-map-wrap");
    var map = add(e, el("div", "pb-map"));
    map.style.aspectRatio = iw + " / " + ih;
    var bg = pngSource(n.image);
    if (bg) {
      var img = add(map, el("img", "pb-map-image"));
      img.src = bg;
      img.alt = "";
    }
    function place(node, x, y) {
      node.style.left = (num(x, -1e6, 1e6, 0) / iw * 100).toFixed(3) + "%";
      node.style.top = (num(y, -1e6, 1e6, 0) / ih * 100).toFixed(3) + "%";
      return node;
    }
    var routes = list(n.routes);
    for (var i = 0; i < routes.length; i++) {
      var r = isObj(routes[i]) ? routes[i] : {};
      var m = place(add(map, el("div", "pb-route" + (r.done === true ? " pb-route-done" : ""))), r.x, r.y);
      if (typeof r.number === "number") add(m, el("span", "pb-route-number", String(Math.floor(r.number))));
      var icons = list(r.icons);
      for (var k = 0; k < icons.length && k < 8; k++) {
        var ic = isObj(icons[k]) ? icons[k] : {};
        var wrap = add(m, image(ic.src, "pb-image-xs", "", ""));
        if (typeof ic.owner === "number") add(wrap, el("span", "pb-owner-dot " + playerClass(ic.owner)));
      }
      if (r.done !== true && icons.length === 0) add(m, el("span", "pb-meta", "open"));
    }
    var players = list(n.players);
    for (var j = 0; j < players.length; j++) {
      var p = isObj(players[j]) ? players[j] : {};
      var pm = place(add(map, el("div", "pb-player " + playerClass(p.color), (Array.from(str(p.initial))[0] || "").toUpperCase())), p.x, p.y);
      if (p.label) pm.title = str(p.label);
    }
    if (n.legend !== false && players.length) {
      var leg = add(e, el("div", "pb-legend"));
      for (var q = 0; q < players.length; q++) {
        var lp = isObj(players[q]) ? players[q] : {};
        var item = add(leg, el("span", "pb-legend-item"));
        add(item, el("span", "pb-dot " + playerClass(lp.color)));
        add(item, el("span", "pb-meta", str(lp.label) || (Array.from(str(lp.initial))[0] || "")));
      }
    }
    return e;
  };

  function render(node, ctx, depth) {
    if (!isObj(node)) return unsupported("");
    var type = typeof node.type === "string" ? node.type : "";
    if (!has(R, type)) return unsupported(type);
    if (depth > MAX_DEPTH || ++ctx.nodes > MAX_NODES) {
      var e = el("div", "pb-unsupported", "Board too large to show");
      e.setAttribute("role", "note");
      return e;
    }
    try {
      return R[type](node, ctx, depth);
    } catch (err) {
      return unsupported(type);
    }
  }

  // renderBoard replaces the content of container with the drawn tree.
  function renderBoard(container, tree) {
    container.textContent = "";
    container.appendChild(render(tree, { nodes: 0 }, 0));
  }

  // The Runs page embeds each board as JSON in the data-tree attribute of .plugin-board (the template escapes it).
  function init(scope) {
    var boards = (scope || document).querySelectorAll(".plugin-board[data-tree]");
    for (var i = 0; i < boards.length; i++) {
      var b = boards[i];
      var raw = b.getAttribute("data-tree");
      if (b.fbDrawn === raw) continue; // unchanged since the last draw
      try {
        renderBoard(b, JSON.parse(raw));
      } catch (err) {
        b.textContent = "";
        b.appendChild(el("div", "pb-unsupported", "This board could not be shown."));
      }
      b.fbDrawn = raw;
    }
  }

  root.FBPluginBlocks = { render: renderBoard, init: init };
  if (typeof document !== "undefined") {
    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", function () { init(document); });
    else init(document);
    document.addEventListener("htmx:afterSettle", function () { init(document); });
  }
})(typeof window !== "undefined" ? window : this);
