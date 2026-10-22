// Uzume server invites: "Copy link" on the Users page and the code display on /invite.
// The code is only ever in the URL fragment (never sent to the server). No inline handlers (CSP script-src 'self').
(function () {
  "use strict";

  function copyText(text) {
    if (navigator.clipboard && window.isSecureContext) {
      return navigator.clipboard.writeText(text);
    }
    // Plain HTTP (for example a LAN address in dev mode): fall back to a hidden textarea and execCommand.
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
    var code = "";
    try {
      code = decodeURIComponent((window.location.hash || "").replace(/^#/, "")).trim().toUpperCase();
    } catch (e) { /* truncated link such as #UZ-AB%2: treat as missing */ }
    if (/^[A-Z0-9 -]{4,40}$/.test(code)) {
      out.textContent = code;
    } else {
      var miss = document.getElementById("invite-missing");
      if (miss) miss.hidden = false;
    }
  }
})();
