// Plugins page: the "Offer to Players" button stays disabled until every permission box is ticked. The server
// checks the same rule, this only makes it visible. Without JavaScript the button is enabled and the server answers.
(function () {
  function sync(form) {
    var boxes = form.querySelectorAll('input[type=checkbox][name=perm]');
    var btn = form.querySelector('[data-offer]');
    if (!btn) return;
    var all = true;
    boxes.forEach(function (b) { if (!b.checked) all = false; });
    btn.disabled = !all;
  }
  function init() {
    document.querySelectorAll('form[data-plugin-review]').forEach(function (form) {
      sync(form);
      form.addEventListener('change', function () { sync(form); });
    });
  }
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init); else init();
  document.addEventListener('htmx:afterSwap', init);
})();
