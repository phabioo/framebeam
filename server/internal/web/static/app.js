// FrameBeam Hub web shell: live updates over Server-Sent Events.
// The server sends "event: <topic>" (clients, saves, library, systems, users, updates, badges);
// each is forwarded as an htmx trigger "fb:<topic>" on <body>. Regions refresh themselves with
// hx-get="<fragment url>" hx-trigger="fb:<topic> from:body" hx-swap="outerHTML".
(function () {
  "use strict";
  if (!document.getElementById("nav")) return; // login/setup pages have no shell
  if (typeof EventSource === "undefined") return;
  var topics = ["clients", "saves", "library", "systems", "users", "updates", "badges"];
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
