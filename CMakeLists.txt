cmake_minimum_required(VERSION 3.18)
project(SwarmSim LANGUAGES C CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES native) # Adjust for your GPU

# Find OpenGL
find_package(OpenGL REQUIRED)

# GLFW
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(external/glfw)

# GLM (header-only)
include_directories(external/glm)

# GLAD
add_library(glad external/glad/src/glad.c)
target_include_directories(glad PUBLIC external/glad/include)

# ImGui
set(IMGUI_DIR external/imgui)
add_library(imgui
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    external/glfw/include
)

# Main executable
add_executable(swarm-sim
    src/main.cpp
    src/simulation.cu
    src/kernels.cu
    src/spatial_hash.cu
    src/ui.cpp
    src/presets.cpp
    src/obstacles.cpp     
    src/scenarios.cpp 
)

target_link_libraries(swarm-sim
    glfw
    glad
    imgui
    OpenGL::GL
)

target_include_directories(swarm-sim PRIVATE
    src
    external/glad/include
    external/glfw/include
    external/imgui
    external/imgui/backends
    external/glm
)
