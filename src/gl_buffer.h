#pragma once

#include <glad/glad.h>

#include <cstddef>

namespace swarm {

struct GlBuffer {
  GLuint id = 0;
  GLenum target = GL_ARRAY_BUFFER;

  void create(GLenum tgt);
  void destroy();
  void allocate(GLsizeiptr bytes, const void* data, GLenum usage) const;
};

struct GlVertexArray {
  GLuint id = 0;
  void create();
  void destroy();
};

} // namespace swarm

