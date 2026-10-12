// FrameBeam Hub file inputs: dashed drop zones (form[data-drop]) show the chosen file name and accept dropped files;
// inputs with data-autosubmit send their form as soon as a file is chosen. No inline handlers (CSP script-src 'self').
(function () {
  "use strict";

  function nameOf(form) {
    var input = form.querySelector('input[type="file"]');
    var out = form.querySelector(".drop-name");
    if (input && out) out.textContent = input.files && input.files.length ? input.files[0].name : "";
  }

  document.addEventListener("change", function (ev) {
    var input = ev.target;
    if (!input || input.type !== "file" || !input.form) return;
    nameOf(input.form);
    if (input.hasAttribute("data-autosubmit") && input.files && input.files.length) {
      if (input.form.requestSubmit) input.form.requestSubmit(); else input.form.submit();
    }
  });

  function zone(ev) {
    return ev.target.closest ? ev.target.closest("form[data-drop]") : null;
  }
  document.addEventListener("dragover", function (ev) {
    var z = zone(ev);
    if (!z) return;
    ev.preventDefault();
    z.classList.add("over");
  });
  document.addEventListener("dragleave", function (ev) {
    var z = zone(ev);
    if (z) z.classList.remove("over");
  });
  document.addEventListener("drop", function (ev) {
    var z = zone(ev);
    if (!z) return;
    ev.preventDefault();
    z.classList.remove("over");
    var input = z.querySelector('input[type="file"]');
    if (input && ev.dataTransfer && ev.dataTransfer.files && ev.dataTransfer.files.length) {
      input.files = ev.dataTransfer.files;
      nameOf(z);
    }
  });
})();
