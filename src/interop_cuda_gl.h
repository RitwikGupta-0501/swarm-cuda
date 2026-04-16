#pragma once

#include <glad/glad.h>

#include <string>

#if SWARM_ENABLE_CUDA
#include <cuda_runtime.h>
#include <cuda_gl_interop.h>
#endif

namespace swarm {

#if SWARM_ENABLE_CUDA
bool cudaRegisterAgentBuffer(GLuint glBuffer, cudaGraphicsResource** outResource, std::string* outError);
void cudaUnregisterAgentBuffer(cudaGraphicsResource* resource);

// Map/unmap for simulation side to write Agent[] directly.
bool cudaMapAgentBuffer(cudaGraphicsResource* resource, void** outDevicePtr, size_t* outBytes,
                        std::string* outError);
bool cudaUnmapAgentBuffer(cudaGraphicsResource* resource, std::string* outError);
#else
// Stubs for builds without CUDA toolkit.
inline bool cudaRegisterAgentBuffer(GLuint, void**, std::string*) { return true; }
inline void cudaUnregisterAgentBuffer(void*) {}
inline bool cudaMapAgentBuffer(void*, void**, size_t*, std::string*) { return false; }
inline bool cudaUnmapAgentBuffer(void*, std::string*) { return false; }
#endif

} // namespace swarm

