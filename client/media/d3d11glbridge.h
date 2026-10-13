#pragma once
// GL-D3D11 bridge (ADR 0027, design D3): our own D3D11 device on the display adapter plus the WGL_NV_DX_interop2
// device opened on it. The display ring (d3d11displayring.h) and, later, the D3D11 encode capture build on it.
// Windows only: other platforms compile every method and open() reports Unavailable("not Windows"). The header has
// no windows.h/d3d11.h: COM objects and WGL handles cross the API as void*.
//
// Thread rules (like CudaGlCapture):
//  - setTargetAdapter(), setUnavailable(), status(), reason(), adapterName(), vendorId(), adapterLuid() and the
//    constructor/destructor are safe on any thread (the constructor does no D3D/GL work).
//  - open(), close(), registerTexture(), unregister(), lock(), unlock() and every call through gl() run on ONE
//    thread, the emulation thread, with the game's GL context current.
//  - d3dDevice() is an ID3D11Device made multithread protected, so the encode worker may use it too; it is NEVER
//    Qt Quick's device (the interop driver issues work on the device it was opened with, from the GL thread).
//  - close() is called by HwRenderContext after the display and encode targets released their registrations.
#include <QString>
#include <QtGlobal>
#include <atomic>
#include <memory>
#include <mutex>

namespace framebeam {

// `GameSession` and the platform factory share the bridge as std::shared_ptr<void>; it IS a
// std::shared_ptr<GlD3d11Bridge> (std::static_pointer_cast<GlD3d11Bridge>(shared)) when the factory made it.
class GlD3d11Bridge {
 public:
  enum class Status { Ok, NotReady, Unavailable, Failed };

  // The few GL entry points the ring needs (opengl32 exports; everything else goes through HwRenderContext).
#ifdef _WIN32
#define FB_GLCALL __stdcall
#else
#define FB_GLCALL
#endif
  struct GlFunctions {
    void(FB_GLCALL* genTextures)(int n, unsigned* names) = nullptr;
    void(FB_GLCALL* deleteTextures)(int n, const unsigned* names) = nullptr;
    unsigned(FB_GLCALL* getError)() = nullptr;
    void(FB_GLCALL* flush)() = nullptr;
    // Framebuffer functions (GL 3.0; wglGetProcAddress), used by the D3D11 encode capture. Null when the driver lacks
    // them; the capture then reports Unavailable.
    void(FB_GLCALL* genFramebuffers)(int n, unsigned* names) = nullptr;
    void(FB_GLCALL* deleteFramebuffers)(int n, const unsigned* names) = nullptr;
    void(FB_GLCALL* bindFramebuffer)(unsigned target, unsigned name) = nullptr;
    void(FB_GLCALL* framebufferTexture2D)(unsigned target, unsigned attachment, unsigned texTarget, unsigned texture, int level) = nullptr;
    unsigned(FB_GLCALL* checkFramebufferStatus)(unsigned target) = nullptr;
    void(FB_GLCALL* blitFramebuffer)(int sx0, int sy0, int sx1, int sy1, int dx0, int dy0, int dx1, int dy1, unsigned mask,
                                     unsigned filter) = nullptr;
    void(FB_GLCALL* getIntegerv)(unsigned pname, int* params) = nullptr;
    void(FB_GLCALL* enable)(unsigned cap) = nullptr;
    void(FB_GLCALL* disable)(unsigned cap) = nullptr;
    unsigned char(FB_GLCALL* isEnabled)(unsigned cap) = nullptr;
  };
#undef FB_GLCALL

  GlD3d11Bridge();
  ~GlD3d11Bridge();
  GlD3d11Bridge(const GlD3d11Bridge&) = delete;
  GlD3d11Bridge& operator=(const GlD3d11Bridge&) = delete;

  // Any thread. The adapter (DXGI LUID: high << 32 | low) Qt Quick's D3D11 device runs on; 0 = unknown. open()
  // reports NotReady until it is set.
  void setTargetAdapter(quint64 luid);
  // Any thread. For a bridge without a display (no target adapter ever): open() takes the first hardware DXGI adapter
  // the game's GL context can open an interop device on, instead of waiting for setTargetAdapter(). A set target
  // LUID still wins.
  void setAnyAdapter(bool on);
  // Any thread. Qt Quick does not render with D3D11 etc.: open() now reports Unavailable(reason), sticky.
  void setUnavailable(const QString& reason);

  // Emulation thread, GL current. Idempotent: Ok once open; Unavailable/Failed are final (already logged).
  Status open();
  Status status() const;
  QString reason() const;       // why Unavailable/Failed
  QString adapterName() const;  // e.g. "AMD Radeon RX 7800 XT" once open, else empty
  quint32 vendorId() const;     // PCI vendor of the adapter once open (0x10DE NVIDIA, 0x1002 AMD, 0x8086 Intel), else 0
  quint64 adapterLuid() const;  // the adapter D3D device lives on once open, else the target LUID
  // Emulation thread. Unregisters nothing itself (owners do that first, with GL current): wglDXCloseDeviceNV and
  // releases the device. glCurrent == false: GL is gone, the interop device handle is leaked. Idempotent.
  void close(bool glCurrent);

  // Windows accessors for the media code; null/false elsewhere or before open() succeeded.
  void* d3dDevice() const;      // ID3D11Device*
  void* interopDevice() const;  // HANDLE of wglDXOpenDeviceNV
  const GlFunctions* gl() const;  // null until open() succeeded
  // wglDXRegisterObjectNV(GL_TEXTURE_2D or the access flag WRITE_DISCARD / READ_WRITE); null on failure (logged).
  void* registerTexture(void* d3dTexture, unsigned glName, bool writeDiscard);
  bool unregister(void* registration);
  bool lock(void* const* registrations, int n);    // wglDXLockObjectsNV
  bool unlock(void* const* registrations, int n);  // wglDXUnlockObjectsNV
  quint32 lastWin32Error() const { return lastError_.load(); }  // GetLastError() of the last failed lock/unlock/register

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
  std::atomic<quint32> lastError_{0};
};

}  // namespace framebeam
