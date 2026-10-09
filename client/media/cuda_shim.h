#pragma once
// Minimal CUDA driver API declarations for FrameBeam's GPU-direct encoding (ADR 0019). No CUDA SDK: the driver is
// loaded at runtime (cudadriver.cpp). Include this BEFORE <libavutil/hwcontext_cuda.h>. Media-internal: include it
// only from .cpp files and from testutil/fake_cuda.h.
#include <cstddef>
#include <cstdint>
#ifdef CUDA_VERSION
#error "cuda_shim.h must not be combined with the CUDA SDK's cuda.h (include the shim first, and only the shim)"
#endif
#define CUDA_VERSION 7050  // only stops <libavutil/hwcontext_cuda.h> from including <cuda.h> (ffnvcodec does the same)
static_assert(sizeof(void*) == 8, "the CUDA shim assumes a 64-bit build");
#ifdef _WIN32
#define FB_CUDAAPI __stdcall
#else
#define FB_CUDAAPI
#endif
typedef int CUresult;  // enum cudaError_enum in cuda.h (int-sized)
typedef int CUdevice;
typedef unsigned long long CUdeviceptr;
typedef struct CUctx_st* CUcontext;  // tags equal cuda.h, so AVCUDADeviceContext sees the same types
typedef struct CUstream_st* CUstream;
typedef struct CUarray_st* CUarray;
typedef struct CUgraphicsResource_st* CUgraphicsResource;

namespace framebeam::cuda {
constexpr CUresult kSuccess = 0;
constexpr unsigned kCtxSchedBlockingSync = 0x04;  // CU_CTX_SCHED_BLOCKING_SYNC
constexpr unsigned kStreamNonBlocking = 0x01;     // CU_STREAM_NON_BLOCKING
constexpr unsigned kRegisterReadOnly = 0x01;      // CU_GRAPHICS_REGISTER_FLAGS_READ_ONLY
constexpr int kGlDeviceListAll = 1;               // CU_GL_DEVICE_LIST_ALL
constexpr int kMemoryTypeDevice = 2, kMemoryTypeArray = 3;
constexpr unsigned kGlTexture2D = 0x0DE1;         // GL_TEXTURE_2D

struct Memcpy2D {  // CUDA_MEMCPY2D (v2 layout, used by cuMemcpy2DAsync_v2)
  size_t srcXInBytes, srcY; int srcMemoryType; const void* srcHost; CUdeviceptr srcDevice; CUarray srcArray;
  size_t srcPitch;
  size_t dstXInBytes, dstY; int dstMemoryType; void* dstHost; CUdeviceptr dstDevice; CUarray dstArray;
  size_t dstPitch;
  size_t WidthInBytes, Height;
};
static_assert(sizeof(Memcpy2D) == 128);
static_assert(offsetof(Memcpy2D, srcHost) == 24 && offsetof(Memcpy2D, srcDevice) == 32 &&
              offsetof(Memcpy2D, srcArray) == 40 && offsetof(Memcpy2D, dstXInBytes) == 56 &&
              offsetof(Memcpy2D, dstMemoryType) == 72 && offsetof(Memcpy2D, dstDevice) == 88 &&
              offsetof(Memcpy2D, dstPitch) == 104 && offsetof(Memcpy2D, WidthInBytes) == 112 &&
              offsetof(Memcpy2D, Height) == 120);

// X-macro: member name == exported symbol (the _v2 names are hard-coded on purpose; the unsuffixed
// cuMemcpy2DAsync takes the legacy struct and corrupts data silently). Order matters only for error messages.
#define FB_CUDA_SYMBOLS(X)                                                                                \
  X(cuInit, (unsigned flags))                                                                             \
  X(cuDriverGetVersion, (int* version))                                                                   \
  X(cuDeviceGetName, (char* name, int len, CUdevice dev))                                                 \
  X(cuGLGetDevices_v2, (unsigned* count, CUdevice* devices, unsigned deviceCount, int list))              \
  X(cuCtxCreate_v2, (CUcontext* ctx, unsigned flags, CUdevice dev))                                       \
  X(cuCtxDestroy_v2, (CUcontext ctx))                                                                     \
  X(cuCtxPushCurrent_v2, (CUcontext ctx))                                                                 \
  X(cuCtxPopCurrent_v2, (CUcontext* ctx))                                                                 \
  X(cuCtxSynchronize, ())                                                                                 \
  X(cuStreamCreate, (CUstream* stream, unsigned flags))                                                   \
  X(cuStreamDestroy_v2, (CUstream stream))                                                                \
  X(cuStreamSynchronize, (CUstream stream))                                                               \
  X(cuGraphicsGLRegisterImage, (CUgraphicsResource* res, unsigned image, unsigned target, unsigned flags)) \
  X(cuGraphicsUnregisterResource, (CUgraphicsResource res))                                               \
  X(cuGraphicsMapResources, (unsigned count, CUgraphicsResource* res, CUstream stream))                   \
  X(cuGraphicsUnmapResources, (unsigned count, CUgraphicsResource* res, CUstream stream))                 \
  X(cuGraphicsSubResourceGetMappedArray, (CUarray* array, CUgraphicsResource res, unsigned index, unsigned level)) \
  X(cuMemcpy2DAsync_v2, (const Memcpy2D* copy, CUstream stream))

struct Api {
#define FB_CUDA_MEMBER(name, params) CUresult(FB_CUDAAPI* name) params = nullptr;
  FB_CUDA_SYMBOLS(FB_CUDA_MEMBER)
#undef FB_CUDA_MEMBER
};
}  // namespace framebeam::cuda
