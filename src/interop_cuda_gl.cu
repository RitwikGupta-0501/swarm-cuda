#include "interop_cuda_gl.h"

#if SWARM_ENABLE_CUDA

#include <sstream>

namespace swarm {
namespace {

inline bool cudaOk(cudaError_t e, std::string* outError, const char* what, const char* file,
                   int line) {
  if (e == cudaSuccess) return true;
  if (outError) {
    std::ostringstream ss;
    ss << what << " failed at " << file << ":" << line << " -> " << cudaGetErrorString(e);
    *outError = ss.str();
  }
  return false;
}

#define CUDA_CHECK(EXPR, OUTERR) \
  cudaOk((EXPR), (OUTERR), #EXPR, __FILE__, __LINE__)

} // namespace

bool cudaRegisterAgentBuffer(GLuint glBuffer, cudaGraphicsResource** outResource,
                             std::string* outError) {
  if (!outResource) return false;
  *outResource = nullptr;
  return CUDA_CHECK(cudaGraphicsGLRegisterBuffer(outResource, glBuffer, cudaGraphicsRegisterFlagsNone),
                    outError);
}

void cudaUnregisterAgentBuffer(cudaGraphicsResource* resource) {
  if (!resource) return;
  (void)cudaGraphicsUnregisterResource(resource);
}

bool cudaMapAgentBuffer(cudaGraphicsResource* resource, void** outDevicePtr, size_t* outBytes,
                        std::string* outError) {
  if (!resource || !outDevicePtr || !outBytes) return false;
  *outDevicePtr = nullptr;
  *outBytes = 0;

  if (!CUDA_CHECK(cudaGraphicsMapResources(1, &resource, 0), outError))
    return false;

  return CUDA_CHECK(cudaGraphicsResourceGetMappedPointer(outDevicePtr, outBytes, resource), outError);
}

bool cudaUnmapAgentBuffer(cudaGraphicsResource* resource, std::string* outError) {
  if (!resource) return false;
  return CUDA_CHECK(cudaGraphicsUnmapResources(1, &resource, 0), outError);
}

} // namespace swarm

#endif

