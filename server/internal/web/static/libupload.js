// Uzume server library: resumable ROM upload. Sends the file in 8 MiB chunks to /library/uploads, shows progress and
// resumes at the server's offset when the same file is chosen again after a reload or a failure. "The same file" means
// the same SHA-256: it is computed in the browser first (streaming, 4 MiB at a time, so the file never has to fit
// into memory) and sent when the upload starts; the server verifies the received file against it.
// Without JavaScript (or fetch/Blob.slice) the form posts the whole file as before. No inline handlers (CSP script-src 'self').
(function () {
  "use strict";
  if (!window.fetch || !window.File || !File.prototype.slice || !Blob.prototype.arrayBuffer) return;

  // Streaming SHA-256 (FIPS 180-4). update() takes Uint8Array pieces of any length; hex() finishes.
  var K = new Uint32Array([
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2]);

  function Sha256() {
    this.h = new Uint32Array([0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19]);
    this.w = new Uint32Array(64);
    this.buf = new Uint8Array(64);
    this.fill = 0;
    this.len = 0;
  }
  Sha256.prototype.block = function (b, o) {
    var w = this.w, h = this.h, i, t1, t2, x, y;
    for (i = 0; i < 16; i++, o += 4) w[i] = (b[o] << 24) | (b[o + 1] << 16) | (b[o + 2] << 8) | b[o + 3];
    for (; i < 64; i++) {
      x = w[i - 15]; y = w[i - 2];
      w[i] = (w[i - 16] + (((x >>> 7) | (x << 25)) ^ ((x >>> 18) | (x << 14)) ^ (x >>> 3)) + w[i - 7] +
        (((y >>> 17) | (y << 15)) ^ ((y >>> 19) | (y << 13)) ^ (y >>> 10))) | 0;
    }
    var a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (i = 0; i < 64; i++) {
      t1 = (hh + (((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7))) + ((e & f) ^ (~e & g)) + K[i] + w[i]) | 0;
      t2 = ((((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10))) + ((a & bb) ^ (a & c) ^ (bb & c))) | 0;
      hh = g; g = f; f = e; e = (d + t1) | 0; d = c; c = bb; bb = a; a = (t1 + t2) | 0;
    }
    h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
  };
  Sha256.prototype.update = function (data) {
    var i = 0, n = data.length;
    this.len += n;
    if (this.fill > 0) {
      while (this.fill < 64 && i < n) this.buf[this.fill++] = data[i++];
      if (this.fill < 64) return;
      this.block(this.buf, 0);
      this.fill = 0;
    }
    for (; i + 64 <= n; i += 64) this.block(data, i);
    while (i < n) this.buf[this.fill++] = data[i++];
  };
  Sha256.prototype.hex = function () {
    var bits = this.len * 8, pad = new Uint8Array(((this.fill < 56 ? 56 : 120) - this.fill) + 8), n = pad.length, i;
    pad[0] = 0x80;
    var hi = Math.floor(bits / 4294967296), lo = bits >>> 0;
    pad[n - 8] = hi >>> 24; pad[n - 7] = hi >>> 16; pad[n - 6] = hi >>> 8; pad[n - 5] = hi;
    pad[n - 4] = lo >>> 24; pad[n - 3] = lo >>> 16; pad[n - 2] = lo >>> 8; pad[n - 1] = lo;
    var len = this.len;
    this.update(pad);
    this.len = len;
    var out = "";
    for (i = 0; i < 8; i++) out += ("00000000" + this.h[i].toString(16)).slice(-8);
    return out;
  };
  // hashFile reads the file in 4 MiB slices; progress(done) is called after each slice.
  function hashFile(file, progress) {
    var h = new Sha256(), step = 4194304, pos = 0;
    return new Promise(function (resolve, reject) {
      (function next() {
        if (pos >= file.size) { resolve(h.hex()); return; }
        file.slice(pos, Math.min(pos + step, file.size)).arrayBuffer().then(function (ab) {
          h.update(new Uint8Array(ab));
          pos += ab.byteLength;
          progress(pos);
          window.setTimeout(next, 0);
        }, reject);
      })();
    });
  }

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
    text.textContent = "Checking the file…";
    hashFile(file, function (done) {
      bar.value = file.size ? Math.floor(done * 100 / file.size) : 0;
      text.textContent = "Checking the file: " + mib(done) + " of " + mib(file.size);
    }).then(function (sha) {
      bar.value = 0;
      text.textContent = "Starting…";
      return api(form, "POST", "/library/uploads", { "Content-Type": "application/json" },
        JSON.stringify({ filename: file.name, size: file.size, sha256: sha, title: title, system: system }));
    }).then(function (s) {
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
          // Network error: ask the server where it stands and continue from there.
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
