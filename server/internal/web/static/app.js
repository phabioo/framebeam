// FrameBeam Hub web shell: live updates over Server-Sent Events, the collapsible navigation and the row menus.
// The server sends "event: <topic>" (clients, saves, library, systems, users, updates, runs, badges);
// each is forwarded as an htmx trigger "fb:<topic>" on <body>. Regions refresh themselves with
// hx-get="<fragment url>" hx-trigger="fb:<topic> from:body" hx-swap="outerHTML".
// Row menus ("⋯" details.menu) close on an outside click or Escape; the ☰ button opens the sidebar below 1024 px.
(function () {
  "use strict";
  function closeMenus(except) {
    var open = document.querySelectorAll("details.menu[open]");
    for (var i = 0; i < open.length; i++) if (open[i] !== except) open[i].open = false;
  }
  document.addEventListener("click", function (ev) {
    var el = ev.target && ev.target.closest ? ev.target : null;
    if (!el) return;
    var shell = document.getElementById("shell");
    var toggle = el.closest(".nav-toggle");
    if (toggle && shell) {
      toggle.setAttribute("aria-expanded", shell.classList.toggle("nav-open") ? "true" : "false");
      return;
    }
    if (shell && el.closest("#nav a")) {
      shell.classList.remove("nav-open");
      var b = shell.querySelector(".nav-toggle");
      if (b) b.setAttribute("aria-expanded", "false");
    }
    closeMenus(el.closest("details.menu"));
  });
  document.addEventListener("keydown", function (ev) {
    if (ev.key !== "Escape") return;
    var open = document.querySelector("details.menu[open]");
    if (open) {
      open.open = false;
      var sum = open.querySelector("summary");
      if (sum) sum.focus();
    }
  });
})();

(function () {
  "use strict";
  if (!document.getElementById("nav")) return; // login/setup pages have no shell
  if (typeof EventSource === "undefined") return;
  var topics = ["clients", "saves", "library", "systems", "users", "updates", "runs", "badges"];
  var src = null, delay = 5000, timer = null;
  function connect() {
    src = new EventSource("/events");
    src.onopen = function () { delay = 5000; };
    // Network errors: the browser retries itself (server hint: 3 s). After an HTTP error the
    // EventSource is CLOSED for good, so reconnect manually with a growing delay (5 s .. 60 s).
    src.onerror = function () {
      if (src.readyState !== 2) return;
      src.close();
      timer = setTimeout(connect, delay);
      delay = Math.min(delay * 2, 60000);
    };
    topics.forEach(function (t) {
      src.addEventListener(t, function () {
        if (window.htmx) window.htmx.trigger(document.body, "fb:" + t);
      });
    });
  }
  connect();
  window.addEventListener("pagehide", function () {
    clearTimeout(timer);
    if (src) src.close();
  });
})();
