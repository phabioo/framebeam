// FrameBeam Hub Settings: "Copy" buttons and reconnecting after a restart. No inline handlers (CSP script-src 'self').
(function () {
  "use strict";
  if (window.__fbSettings) return; // loaded twice (page swap): handlers are delegated once
  window.__fbSettings = true;

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    return new Promise(function (resolve, reject) {
      var ta = document.createElement("textarea");
      ta.value = text;
      ta.setAttribute("readonly", "");
      ta.style.position = "fixed";
      ta.style.opacity = "0";
      document.body.appendChild(ta);
      ta.select();
      try { document.execCommand("copy") ? resolve() : reject(new Error("copy failed")); }
      catch (e) { reject(e); }
      finally { document.body.removeChild(ta); }
    });
  }

  document.addEventListener("click", function (ev) {
    var btn = ev.target.closest ? ev.target.closest("[data-copy]") : null;
    if (!btn) return;
    var label = btn.getAttribute("data-label") || btn.textContent;
    btn.setAttribute("data-label", label);
    copyText(btn.getAttribute("data-copy")).then(function () { btn.textContent = "Copied"; },
      function () { btn.textContent = "Copy failed"; });
    window.setTimeout(function () { btn.textContent = label; }, 1500);
  });

  // After "Restart hub now" with an unchanged port: wait until the hub went away, then reload as soon as it answers.
  var polling = false;
  function pollRestart(url) {
    if (polling) return;
    polling = true;
    var wasDown = false, tries = 0;
    (function tick() {
      tries++;
      fetch(url, { credentials: "same-origin", cache: "no-store", redirect: "manual" }).then(function () {
        if (wasDown || tries > 40) { window.location.reload(); return; }
        window.setTimeout(tick, 1000);
      }, function () {
        wasDown = true;
        window.setTimeout(tick, 1000);
      });
    })();
  }
  function scan() {
    var el = document.querySelector("[data-restart-poll]");
    if (el) pollRestart(el.getAttribute("data-restart-poll"));
  }
  document.addEventListener("htmx:afterSwap", scan);
  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", scan); else scan();
})();
