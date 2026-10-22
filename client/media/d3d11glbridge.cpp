#include "d3d11glbridge.h"

#include <QElapsedTimer>
#include <QLoggingCategory>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// After windows.h
#include <d3d10_1.h>  // ID3D10Multithread; d3d10_1.h must come before any d3d10.h (SAL check in the Windows SDK)
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif

namespace uzume {

Q_LOGGING_CATEGORY(lcBridge, "uzume.gpubridge")

#ifdef _WIN32

namespace {

using Microsoft::WRL::ComPtr;

// WGL_NV_DX_interop(2): declared here, not taken from a wglext.h (not vendored).
constexpr unsigned kGlTexture2D = 0x0DE1;
constexpr unsigned kWglAccessReadWriteNv = 0x0001;
constexpr unsigned kWglAccessWriteDiscardNv = 0x0002;

using PfnGetExtensionsStringArb = const char*(WINAPI*)(HDC);
using PfnOpenDevice = HANDLE(WINAPI*)(void*);
using PfnCloseDevice = BOOL(WINAPI*)(HANDLE);
using PfnRegisterObject = HANDLE(WINAPI*)(HANDLE, void*, unsigned, unsigned, unsigned);
using PfnUnregisterObject = BOOL(WINAPI*)(HANDLE, HANDLE);
using PfnLockObjects = BOOL(WINAPI*)(HANDLE, int, HANDLE*);
using PfnUnlockObjects = BOOL(WINAPI*)(HANDLE, int, HANDLE*);

// wglGetProcAddress failure values: NULL and, on some drivers, 1, 2, 3, -1 (Qt treats them the same way).
void* resolveWgl(const char* name) {
  PROC p = wglGetProcAddress(name);
  const auto v = reinterpret_cast<quintptr>(p);
  if (!p || v == 1 || v == 2 || v == 3 || v == static_cast<quintptr>(-1)) {
    return nullptr;
  }
  return reinterpret_cast<void*>(p);
}

void* glExport(HMODULE module, const char* name) { return module ? reinterpret_cast<void*>(GetProcAddress(module, name)) : nullptr; }

template <class F>
F asFn(void* p) {
  return reinterpret_cast<F>(p);
}

bool hasExtension(const char* list, const char* name) {
  if (!list) {
    return false;
  }
  const QByteArray all(list);
  for (const QByteArray& token : all.split(' ')) {
    if (token == name) {
      return true;
    }
  }
  return false;
}

}  // namespace

struct GlD3d11Bridge::Impl {
  mutable std::mutex m;  // reason_, adapterName_
  std::atomic<int> status{static_cast<int>(Status::NotReady)};
  std::atomic<quint64> target{0};
  std::atomic<bool> anyAdapter{false};
  std::atomic<quint64> luid{0};
  std::atomic<quint32> vendor{0};
  std::atomic<bool> unavailableSet{false};
  QString unavailableReason;
  QString reason;
  QString adapterName;

  ComPtr<ID3D11Device> device;
  HANDLE interop = nullptr;
  PfnCloseDevice closeDevice = nullptr;
  PfnRegisterObject registerObject = nullptr;
  PfnUnregisterObject unregisterObject = nullptr;
  PfnLockObjects lockObjects = nullptr;
  PfnUnlockObjects unlockObjects = nullptr;
  GlFunctions gl;
  std::atomic<bool> glReady{false};
};

#else  // !_WIN32

struct GlD3d11Bridge::Impl {
  mutable std::mutex m;
  std::atomic<int> status{static_cast<int>(Status::NotReady)};
  std::atomic<quint64> target{0};
  std::atomic<bool> anyAdapter{false};
  QString reason;
  GlFunctions gl;
};

#endif

GlD3d11Bridge::GlD3d11Bridge() : d_(std::make_unique<Impl>()) {}

GlD3d11Bridge::~GlD3d11Bridge() {
  // The owner calls close() with GL current first; if it did not, only the COM device is released here.
  close(false);
}

namespace {
GlD3d11Bridge::Status toStatus(int v) { return static_cast<GlD3d11Bridge::Status>(v); }
}  // namespace

GlD3d11Bridge::Status GlD3d11Bridge::status() const { return toStatus(d_->status.load()); }

QString GlD3d11Bridge::reason() const {
  std::lock_guard lock(d_->m);
  return d_->reason;
}

#ifndef _WIN32

void GlD3d11Bridge::setTargetAdapter(quint64 luid) { d_->target = luid; }
void GlD3d11Bridge::setAnyAdapter(bool on) { d_->anyAdapter = on; }
void GlD3d11Bridge::setUnavailable(const QString& why) {
  std::lock_guard lock(d_->m);
  d_->reason = why;
  d_->status = static_cast<int>(Status::Unavailable);
}
GlD3d11Bridge::Status GlD3d11Bridge::open() {
  if (status() == Status::NotReady) {
    {
      std::lock_guard lock(d_->m);
      d_->reason = QStringLiteral("not Windows");
    }
    d_->status = static_cast<int>(Status::Unavailable);
  }
  return status();
}
QString GlD3d11Bridge::adapterName() const { return {}; }
quint32 GlD3d11Bridge::vendorId() const { return 0; }
quint64 GlD3d11Bridge::adapterLuid() const { return d_->target.load(); }
void GlD3d11Bridge::close(bool) {}
void* GlD3d11Bridge::d3dDevice() const { return nullptr; }
void* GlD3d11Bridge::interopDevice() const { return nullptr; }
const GlD3d11Bridge::GlFunctions* GlD3d11Bridge::gl() const { return nullptr; }
void* GlD3d11Bridge::registerTexture(void*, unsigned, bool) { return nullptr; }
bool GlD3d11Bridge::unregister(void*) { return false; }
bool GlD3d11Bridge::lock(void* const*, int) { return false; }
bool GlD3d11Bridge::unlock(void* const*, int) { return false; }

#else  // _WIN32

void GlD3d11Bridge::setTargetAdapter(quint64 luid) { d_->target = luid; }
void GlD3d11Bridge::setAnyAdapter(bool on) { d_->anyAdapter = on; }

void GlD3d11Bridge::setUnavailable(const QString& why) {
  std::lock_guard lock(d_->m);
  if (!d_->unavailableSet.load()) {
    d_->unavailableReason = why;
    d_->unavailableSet = true;
  }
}

QString GlD3d11Bridge::adapterName() const {
  std::lock_guard lock(d_->m);
  return d_->adapterName;
}

quint32 GlD3d11Bridge::vendorId() const { return d_->vendor.load(); }

quint64 GlD3d11Bridge::adapterLuid() const {
  const quint64 l = d_->luid.load();
  return l ? l : d_->target.load();
}

const GlD3d11Bridge::GlFunctions* GlD3d11Bridge::gl() const { return d_->glReady.load() ? &d_->gl : nullptr; }
void* GlD3d11Bridge::d3dDevice() const { return d_->device.Get(); }
void* GlD3d11Bridge::interopDevice() const { return d_->interop; }

GlD3d11Bridge::Status GlD3d11Bridge::open() {
  Impl& d = *d_;
  if (d.unavailableSet.load()) {
    if (status() != Status::Unavailable) {
      QString why;
      {
        std::lock_guard lock(d.m);
        why = d.unavailableReason;
        d.reason = why;
      }
      d.status = static_cast<int>(Status::Unavailable);
      qCInfo(lcBridge).noquote() << QStringLiteral("GL-D3D11 bridge unavailable: %1").arg(why);
    }
    return Status::Unavailable;
  }
  if (status() != Status::NotReady) {
    return status();
  }
  const quint64 target = d.target.load();
  if (target == 0 && !d.anyAdapter.load()) {
    return Status::NotReady;  // Qt Quick's adapter is not known yet
  }

  QElapsedTimer timer;
  timer.start();
  auto finish = [&](Status s, const QString& why) {
    {
      std::lock_guard lock(d.m);
      d.reason = why;
    }
    d.status = static_cast<int>(s);
    if (s == Status::Unavailable) {
      qCInfo(lcBridge).noquote() << QStringLiteral("GL-D3D11 bridge unavailable: %1").arg(why);
    } else {
      qCWarning(lcBridge).noquote() << QStringLiteral("GL-D3D11 bridge failed: %1").arg(why);
    }
    return s;
  };
  auto cleanup = [&]() {
    d.device.Reset();
    d.glReady = false;
  };

  // 1. OpenGL side: WGL_NV_DX_interop2 on the current context.
  HDC dc = wglGetCurrentDC();
  if (!wglGetCurrentContext() || !dc) {
    return finish(Status::Failed, QStringLiteral("no OpenGL context is current on the emulation thread"));
  }
  auto getExtensions = asFn<PfnGetExtensionsStringArb>(resolveWgl("wglGetExtensionsStringARB"));
  if (!getExtensions || !hasExtension(getExtensions(dc), "WGL_NV_DX_interop2")) {
    return finish(Status::Unavailable, QStringLiteral("the OpenGL driver has no WGL_NV_DX_interop2"));
  }
  auto openDevice = asFn<PfnOpenDevice>(resolveWgl("wglDXOpenDeviceNV"));
  d.closeDevice = asFn<PfnCloseDevice>(resolveWgl("wglDXCloseDeviceNV"));
  d.registerObject = asFn<PfnRegisterObject>(resolveWgl("wglDXRegisterObjectNV"));
  d.unregisterObject = asFn<PfnUnregisterObject>(resolveWgl("wglDXUnregisterObjectNV"));
  d.lockObjects = asFn<PfnLockObjects>(resolveWgl("wglDXLockObjectsNV"));
  d.unlockObjects = asFn<PfnUnlockObjects>(resolveWgl("wglDXUnlockObjectsNV"));
  if (!openDevice || !d.closeDevice || !d.registerObject || !d.unregisterObject || !d.lockObjects || !d.unlockObjects) {
    return finish(Status::Unavailable, QStringLiteral("the OpenGL driver does not export the wglDX* functions"));
  }
  HMODULE gl32 = GetModuleHandleW(L"opengl32.dll");
  d.gl.genTextures = asFn<void(__stdcall*)(int, unsigned*)>(gl32 ? reinterpret_cast<void*>(GetProcAddress(gl32, "glGenTextures")) : nullptr);
  d.gl.deleteTextures =
      asFn<void(__stdcall*)(int, const unsigned*)>(gl32 ? reinterpret_cast<void*>(GetProcAddress(gl32, "glDeleteTextures")) : nullptr);
  d.gl.getError = asFn<unsigned(__stdcall*)()>(gl32 ? reinterpret_cast<void*>(GetProcAddress(gl32, "glGetError")) : nullptr);
  d.gl.flush = asFn<void(__stdcall*)()>(gl32 ? reinterpret_cast<void*>(GetProcAddress(gl32, "glFlush")) : nullptr);
  if (!d.gl.genTextures || !d.gl.deleteTextures || !d.gl.getError || !d.gl.flush) {
    return finish(Status::Failed, QStringLiteral("opengl32.dll does not export the basic texture functions"));
  }
  d.gl.enable = asFn<void(__stdcall*)(unsigned)>(glExport(gl32, "glEnable"));
  d.gl.disable = asFn<void(__stdcall*)(unsigned)>(glExport(gl32, "glDisable"));
  d.gl.isEnabled = asFn<unsigned char(__stdcall*)(unsigned)>(glExport(gl32, "glIsEnabled"));
  d.gl.getIntegerv = asFn<void(__stdcall*)(unsigned, int*)>(glExport(gl32, "glGetIntegerv"));
  // GL 3.0 framebuffer functions: only through wglGetProcAddress. Missing ones are not fatal (display does not need them).
  d.gl.genFramebuffers = asFn<void(__stdcall*)(int, unsigned*)>(resolveWgl("glGenFramebuffers"));
  d.gl.deleteFramebuffers = asFn<void(__stdcall*)(int, const unsigned*)>(resolveWgl("glDeleteFramebuffers"));
  d.gl.bindFramebuffer = asFn<void(__stdcall*)(unsigned, unsigned)>(resolveWgl("glBindFramebuffer"));
  d.gl.framebufferTexture2D = asFn<void(__stdcall*)(unsigned, unsigned, unsigned, unsigned, int)>(resolveWgl("glFramebufferTexture2D"));
  d.gl.checkFramebufferStatus = asFn<unsigned(__stdcall*)(unsigned)>(resolveWgl("glCheckFramebufferStatus"));
  d.gl.blitFramebuffer =
      asFn<void(__stdcall*)(int, int, int, int, int, int, int, int, unsigned, unsigned)>(resolveWgl("glBlitFramebuffer"));

  // 2. Our own device on the adapter Qt Quick renders on (by LUID), or, without a display (setAnyAdapter), on the first
  //    hardware adapter the GL context can open an interop device on. 3. The interop device on it.
  ComPtr<IDXGIFactory1> factory;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return finish(Status::Failed, QStringLiteral("CreateDXGIFactory1 failed"));
  }
  DXGI_ADAPTER_DESC1 desc = {};
  enum class Try { Ok, DeviceFailed, InteropFailed };
  HRESULT deviceHr = S_OK;
  DWORD interopErr = 0;
  const auto tryAdapter = [&](IDXGIAdapter1* adapter) {
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0};
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                   D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, levels, 1, D3D11_SDK_VERSION,
                                   &d.device, nullptr, nullptr);
    if (FAILED(hr)) {  // display-only needs no video support
      hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, 1,
                             D3D11_SDK_VERSION, &d.device, nullptr, nullptr);
    }
    if (FAILED(hr)) {
      deviceHr = hr;
      return Try::DeviceFailed;
    }
    ComPtr<ID3D10Multithread> mt;
    if (SUCCEEDED(d.device.As(&mt))) {
      mt->SetMultithreadProtected(TRUE);
    }
    // NULL: the GL context is not on this adapter (Optimus/hybrid), ERROR_NOT_SUPPORTED per spec.
    d.interop = openDevice(d.device.Get());
    if (!d.interop) {
      interopErr = GetLastError();
      d.device.Reset();
      return Try::InteropFailed;
    }
    return Try::Ok;
  };

  if (target != 0) {
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0;; ++i) {
      ComPtr<IDXGIAdapter1> a;
      if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) {
        break;
      }
      DXGI_ADAPTER_DESC1 dd = {};
      if (SUCCEEDED(a->GetDesc1(&dd))) {
        const quint64 l = (static_cast<quint64>(static_cast<quint32>(dd.AdapterLuid.HighPart)) << 32) | dd.AdapterLuid.LowPart;
        if (l == target) {
          adapter = a;
          desc = dd;
          break;
        }
      }
    }
    if (!adapter) {
      return finish(Status::Unavailable, QStringLiteral("the display adapter (LUID 0x%1) was not found").arg(target, 0, 16));
    }
    switch (tryAdapter(adapter.Get())) {
      case Try::Ok:
        break;
      case Try::DeviceFailed:
        cleanup();
        return finish(Status::Failed, QStringLiteral("D3D11CreateDevice failed (0x%1)").arg(static_cast<quint32>(deviceHr), 8, 16, QLatin1Char('0')));
      case Try::InteropFailed:
        cleanup();
        return finish(Status::Unavailable,
                      QStringLiteral("the OpenGL context is not on the display adapter (wglDXOpenDeviceNV failed, error %1)").arg(interopErr));
    }
  } else {
    bool opened = false;
    int tried = 0;
    for (UINT i = 0; !opened; ++i) {
      ComPtr<IDXGIAdapter1> a;
      if (factory->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) {
        break;
      }
      DXGI_ADAPTER_DESC1 dd = {};
      if (FAILED(a->GetDesc1(&dd)) || (dd.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
        continue;  // WARP / Microsoft Basic Render Driver
      }
      ++tried;
      if (tryAdapter(a.Get()) == Try::Ok) {
        desc = dd;
        opened = true;
      }
    }
    if (!opened) {
      cleanup();
      return finish(Status::Unavailable, tried == 0 ? QStringLiteral("no hardware DXGI adapter")
                                                     : QStringLiteral("the OpenGL context cannot be shared with any of the %1 DXGI adapters").arg(tried));
    }
  }
  const quint64 luidValue = (static_cast<quint64>(static_cast<quint32>(desc.AdapterLuid.HighPart)) << 32) | desc.AdapterLuid.LowPart;

  const QString name = QString::fromWCharArray(desc.Description);
  {
    std::lock_guard lock(d.m);
    d.adapterName = name;
    d.reason.clear();
  }
  d.vendor = desc.VendorId;
  d.luid = luidValue;
  d.glReady = true;
  d.status = static_cast<int>(Status::Ok);
  qCInfo(lcBridge).noquote() << QStringLiteral("GL-D3D11 bridge on \"%1\" (vendor 0x%2) ready in %3 ms")
                                    .arg(name, QString::number(desc.VendorId, 16).rightJustified(4, QLatin1Char('0')))
                                    .arg(timer.elapsed());
  return Status::Ok;
}

void GlD3d11Bridge::close(bool glCurrent) {
  Impl& d = *d_;
  d.glReady = false;
  if (d.interop) {
    if (glCurrent && d.closeDevice) {
      if (!d.closeDevice(d.interop)) {
        qCWarning(lcBridge) << "GL-D3D11 bridge: wglDXCloseDeviceNV failed";
      }
    } else {
      qCWarning(lcBridge) << "GL-D3D11 bridge: GL context lost; leaking the interop device";
    }
    d.interop = nullptr;
  }
  d.device.Reset();
  if (status() == Status::Ok) {
    {
      std::lock_guard lock(d.m);
      d.reason = QStringLiteral("closed");
    }
    d.status = static_cast<int>(Status::Unavailable);
  }
}

void* GlD3d11Bridge::registerTexture(void* d3dTexture, unsigned glName, bool writeDiscard) {
  Impl& d = *d_;
  if (!d.interop || !d.registerObject) {
    return nullptr;
  }
  HANDLE h = d.registerObject(d.interop, d3dTexture, glName, kGlTexture2D, writeDiscard ? kWglAccessWriteDiscardNv : kWglAccessReadWriteNv);
  if (!h) {
    lastError_ = GetLastError();
  }
  return h;
}

bool GlD3d11Bridge::unregister(void* registration) {
  Impl& d = *d_;
  if (!d.interop || !d.unregisterObject || !registration) {
    return false;
  }
  const bool ok = d.unregisterObject(d.interop, registration) != FALSE;
  if (!ok) {
    lastError_ = GetLastError();
  }
  return ok;
}

bool GlD3d11Bridge::lock(void* const* registrations, int n) {
  Impl& d = *d_;
  if (!d.interop || !d.lockObjects) {
    return false;
  }
  const bool ok = d.lockObjects(d.interop, n, reinterpret_cast<HANDLE*>(const_cast<void**>(registrations))) != FALSE;
  if (!ok) {
    lastError_ = GetLastError();
  }
  return ok;
}

bool GlD3d11Bridge::unlock(void* const* registrations, int n) {
  Impl& d = *d_;
  if (!d.interop || !d.unlockObjects) {
    return false;
  }
  const bool ok = d.unlockObjects(d.interop, n, reinterpret_cast<HANDLE*>(const_cast<void**>(registrations))) != FALSE;
  if (!ok) {
    lastError_ = GetLastError();
  }
  return ok;
}

#endif  // _WIN32

}  // namespace uzume
