cmake_minimum_required(VERSION 3.25)

project(SwarmVisualizer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
  set(CMAKE_CUDA_ARCHITECTURES all)
endif()

option(SWARM_ENABLE_CUDA "Enable CUDA-OpenGL interop module" ON)
option(SWARM_RUNTIME_VALIDATION "Enable runtime validation checks (debug-oriented)." ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(glad CONFIG REQUIRED)

if(SWARM_ENABLE_CUDA)
  include(CheckLanguage)
  check_language(CUDA)
  if(CMAKE_CUDA_COMPILER)
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
  else()
    message(WARNING "CUDA compiler not found. Falling back to non-CUDA build.")
    set(SWARM_ENABLE_CUDA OFF CACHE BOOL "" FORCE)
  endif()
endif()

add_executable(swarm_visualizer
  src/main.cpp
  src/renderer.cpp
  src/camera.cpp
  src/gl_shader.cpp
  src/gl_buffer.cpp
  src/debug_draw.cpp
  src/error.cpp
  src/time_stats.cpp
)

target_include_directories(swarm_visualizer PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(swarm_visualizer PRIVATE
  glfw
  glad::glad
  glm::glm
)

target_compile_definitions(swarm_visualizer PRIVATE
  GLFW_INCLUDE_NONE
)

if(SWARM_RUNTIME_VALIDATION)
  target_compile_definitions(swarm_visualizer PRIVATE
    $<$<CONFIG:Debug>:SWARM_RUNTIME_VALIDATION=1>
    $<$<NOT:$<CONFIG:Debug>>:SWARM_RUNTIME_VALIDATION=0>
  )
else()
  target_compile_definitions(swarm_visualizer PRIVATE
    SWARM_RUNTIME_VALIDATION=0
  )
endif()

if(MSVC)
  target_compile_options(swarm_visualizer PRIVATE /W4 /permissive-)
else()
  target_compile_options(swarm_visualizer PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(SWARM_ENABLE_CUDA)
  target_sources(swarm_visualizer PRIVATE
    src/interop_cuda_gl.cu
  )

  set_target_properties(swarm_visualizer PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
  )

  target_link_libraries(swarm_visualizer PRIVATE
    CUDA::cudart
  )

  target_compile_definitions(swarm_visualizer PRIVATE
    SWARM_ENABLE_CUDA=1
  )
else()
  target_compile_definitions(swarm_visualizer PRIVATE
    SWARM_ENABLE_CUDA=0
  )
endif()

# Ensure shaders are always available next to the executable
add_custom_command(TARGET swarm_visualizer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    "$<TARGET_FILE_DIR:swarm_visualizer>/shaders"
    COMMENT "Copying shaders to build directory..."
)


