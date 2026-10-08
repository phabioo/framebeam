// FrameBeam Hub invites: "Copy link" on the Users page and the code display on /invite.
// The code is only ever in the URL fragment (never sent to the Hub). No inline handlers (CSP script-src 'self').
(function () {
  "use strict";

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    return Promise.reject(new Error("clipboard unavailable"));
  }

  document.addEventListener("click", function (ev) {
    var btn = ev.target.closest ? ev.target.closest("[data-copy-link]") : null;
    if (!btn) return;
    var label = btn.getAttribute("data-label") || btn.textContent;
    btn.setAttribute("data-label", label);
    var box = btn.parentNode.querySelector("[data-copy-fallback]");
    copyText(btn.getAttribute("data-copy-link")).then(function () {
      btn.textContent = "Copied";
    }, function () {
      // No clipboard API (for example plain HTTP): offer the link as selectable text.
      btn.textContent = "Copy manually";
      if (box) { box.hidden = false; box.focus(); box.select(); }
    });
    window.setTimeout(function () { btn.textContent = label; }, 2000);
  });

  var out = document.querySelector("[data-invite-code]");
  if (out) {
    var code = decodeURIComponent((window.location.hash || "").replace(/^#/, "")).trim().toUpperCase();
    if (/^[A-Z0-9 -]{4,40}$/.test(code)) {
      out.textContent = code;
    } else {
      var miss = document.getElementById("invite-missing");
      if (miss) miss.hidden = false;
    }
  }
})();
