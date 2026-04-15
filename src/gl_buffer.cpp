#include "gl_buffer.h"

namespace swarm {

void GlBuffer::create(GLenum tgt) {
  target = tgt;
  if (!id) glCreateBuffers(1, &id);
}

void GlBuffer::destroy() {
  if (id) glDeleteBuffers(1, &id);
  id = 0;
}

void GlBuffer::allocate(GLsizeiptr bytes, const void* data, GLenum usage) const {
  glNamedBufferData(id, bytes, data, usage);
}

void GlVertexArray::create() {
  if (!id) glCreateVertexArrays(1, &id);
}

void GlVertexArray::destroy() {
  if (id) glDeleteVertexArrays(1, &id);
  id = 0;
}

} // namespace swarm

