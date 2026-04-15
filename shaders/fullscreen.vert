#version 460 core

// Full-screen triangle driven by gl_VertexID — no VBO needed.
// Call with glDrawArrays(GL_TRIANGLES, 0, 3) + mDummyVao bound.
out vec2 vUv;

void main() {
  // Produces a triangle that covers the entire clip-space [-1,1]x[-1,1]:
  //   VertexID 0 → (-1,-1)  UV (0,0)
  //   VertexID 1 → ( 3,-1)  UV (2,0)
  //   VertexID 2 → (-1, 3)  UV (0,2)
  vec2 pos = vec2(
    float((gl_VertexID << 1) & 2),
    float( gl_VertexID       & 2)
  ) * 2.0 - 1.0;

  vUv         = pos * 0.5 + 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
