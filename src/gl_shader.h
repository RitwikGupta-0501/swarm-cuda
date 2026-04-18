#pragma once

#include <glad/glad.h>

#include <string>
#include <unordered_map>

namespace swarm {

class ShaderProgram {
public:
  ShaderProgram() = default;
  ~ShaderProgram();

  ShaderProgram(const ShaderProgram&) = delete;
  ShaderProgram& operator=(const ShaderProgram&) = delete;
  ShaderProgram(ShaderProgram&&) noexcept;
  ShaderProgram& operator=(ShaderProgram&&) noexcept;

  bool loadFromFiles(const std::string& vsPath, const std::string& fsPath, std::string* outError);
  bool loadCompute(const std::string& csPath, std::string* outError);
  void use() const;
  GLuint id() const { return mProgram; }
  GLuint release();

  // Uniform helpers (cached locations).
  void setInt(const char* name, int v);
  void setFloat(const char* name, float v);
  void setVec4(const char* name, float x, float y, float z, float w);

private:
  GLuint mProgram = 0;
  std::unordered_map<std::string, GLint> mUniformLoc;

  GLint uniformLoc(const char* name);
};

} // namespace swarm

