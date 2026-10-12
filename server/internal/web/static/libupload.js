// FrameBeam Hub library: resumable ROM upload. Sends the file in 8 MiB chunks to /library/uploads, shows progress and
// resumes at the Hub's offset when the same file (file name and size) is chosen again after a reload or a failure.
// Without JavaScript (or fetch/Blob.slice) the form posts the whole file as before. No inline handlers (CSP script-src 'self').
(function () {
  "use strict";
  if (!window.fetch || !window.File || !File.prototype.slice) return;

  function api(form, method, path, headers, body) {
    var h = headers || {};
    h["X-CSRF-Token"] = form.getAttribute("data-csrf");
    return fetch(path, { method: method, headers: h, body: body, credentials: "same-origin" }).then(function (res) {
      return res.json().catch(function () { return {}; }).then(function (j) {
        if (!res.ok) { var e = new Error(j.error || "Upload failed (HTTP " + res.status + ")."); e.status = res.status; throw e; }
        return j;
      });
    });
  }

  function mib(n) { return (n / 1048576).toFixed(n > 10485760 ? 0 : 1) + " MiB"; }

  function run(form) {
    var file = form.querySelector('input[type="file"]').files[0];
    if (!file) return;
    var chunk = parseInt(form.getAttribute("data-chunk"), 10) || 8388608;
    var box = form.querySelector("[data-upload-status]");
    var bar = box.querySelector("progress");
    var text = box.querySelector("[data-upload-text]");
    var btn = form.querySelector('button[type="submit"]');
    box.hidden = false;
    btn.disabled = true;
    function show(offset) {
      bar.value = file.size ? Math.floor(offset * 100 / file.size) : 0;
      text.textContent = mib(offset) + " of " + mib(file.size);
    }
    function fail(err) {
      btn.disabled = false;
      text.textContent = err.message + " Choose the same file again to resume.";
    }
    var title = form.querySelector('input[name="title"]').value;
    var system = form.querySelector('select[name="system"]').value;
    text.textContent = "Starting…";
    api(form, "POST", "/library/uploads", { "Content-Type": "application/json" },
      JSON.stringify({ filename: file.name, size: file.size, title: title, system: system })).then(function (s) {
      var id = s.upload_id;
      function next(offset, retries) {
        show(offset);
        var end = Math.min(offset + chunk, file.size);
        api(form, "POST", "/library/uploads/" + id + "/chunk", {
          "Content-Type": "application/octet-stream",
          "Content-Range": "bytes " + offset + "-" + (end - 1) + "/" + file.size
        }, file.slice(offset, end)).then(function (r) {
          if (r.done) { show(file.size); window.location.assign("/library?ok=uploaded"); return; }
          next(r.offset, 3);
        }, function (err) {
          if ((err.status && err.status !== 409) || retries <= 0) { fail(err); return; }
          // Network error: ask the Hub where it stands and continue from there.
          window.setTimeout(function () {
            api(form, "GET", "/library/uploads/" + id).then(function (st) { next(st.offset, retries - 1); }, fail);
          }, 1000);
        });
      }
      next(s.offset, 3);
    }, fail);
  }

  document.addEventListener("submit", function (ev) {
    var form = ev.target;
    if (!form.matches || !form.matches("form[data-resumable-upload]")) return;
    ev.preventDefault();
    run(form);
  });
})();
