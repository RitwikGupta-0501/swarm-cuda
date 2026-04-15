#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>

#include "simulation.h"

// 🔥 SHADERS
const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec2 aPos;
void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

const char* fragmentShaderSource = R"(
#version 330 core
out vec4 FragColor;

uniform vec3 uColor;

void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

int main() {
    int agentCount = 10000;

    glfwInit();

    // ✅ WINDOWED MODE (default)
    GLFWwindow* window = glfwCreateWindow(
        1280, 800,
        "Swarm Simulation",
        NULL,
        NULL
    );

    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);  // Disable VSync

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to init GLAD\n";
        return -1;
    }

    // 🔥 SHADER SETUP
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertexShaderSource, NULL);
    glCompileShader(vs);

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragmentShaderSource, NULL);
    glCompileShader(fs);

    GLuint shader = glCreateProgram();
    glAttachShader(shader, vs);
    glAttachShader(shader, fs);
    glLinkProgram(shader);

    glDeleteShader(vs);
    glDeleteShader(fs);

    GLuint colorLoc = glGetUniformLocation(shader, "uColor");

    // 🔥 VAO + VBO
    GLuint VAO, VBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, agentCount * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    initSimulation(agentCount);
    registerRenderBuffer(VBO);

    GLuint mouseVAO, mouseVBO;
    glGenVertexArrays(1, &mouseVAO);
    glGenBuffers(1, &mouseVBO);

    glBindVertexArray(mouseVAO);
    glBindBuffer(GL_ARRAY_BUFFER, mouseVBO);
    glBufferData(GL_ARRAY_BUFFER, 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // 🔥 FULLSCREEN TOGGLE VARIABLES
    bool isFullscreen = false;
    int windowedX = 100, windowedY = 100;
    int windowedW = 1280, windowedH = 800;

    bool fPressedLastFrame = false; // prevents spam toggling

    // 🔁 LOOP
    while (!glfwWindowShouldClose(window)) {
        float dt = 0.016f;

        // 🔥 FULLSCREEN TOGGLE (press F)
        bool fPressedNow = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;

        if (fPressedNow && !fPressedLastFrame) {
            isFullscreen = !isFullscreen;

            if (isFullscreen) {
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);

                glfwGetWindowPos(window, &windowedX, &windowedY);
                glfwGetWindowSize(window, &windowedW, &windowedH);

                glfwSetWindowMonitor(window, monitor,
                    0, 0,
                    mode->width, mode->height,
                    mode->refreshRate);
            } else {
                glfwSetWindowMonitor(window, NULL,
                    windowedX, windowedY,
                    windowedW, windowedH,
                    0);
            }
        }

        fPressedLastFrame = fPressedNow;

        // 🔥 DYNAMIC SIZE (important)
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);

        // 🔥 MOUSE
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);

        float mouseX = (mx / width) * 2.0f - 1.0f;
        float mouseY = 1.0f - (my / height) * 2.0f;

        // 🔥 SIMULATION
        stepSimulation(dt, mouseX, mouseY);

        int count = getAgentCount();

        // 🧼 CLEAR
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        // ⚪ DRAW SWARM
        glUseProgram(shader);
        glUniform3f(colorLoc, 1.0f, 1.0f, 1.0f);

        glBindVertexArray(VAO);
        glPointSize(3.0f);
        glDrawArrays(GL_POINTS, 0, count);

        // 🔴 DRAW MOUSE
        float mousePoint[2] = {mouseX, mouseY};

        glBindBuffer(GL_ARRAY_BUFFER, mouseVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(mousePoint), mousePoint, GL_DYNAMIC_DRAW);

        glUniform3f(colorLoc, 1.0f, 0.0f, 0.0f);
        glPointSize(12.0f);
        glBindVertexArray(mouseVAO);
        glDrawArrays(GL_POINTS, 0, 1);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    shutdownSimulation();
    glDeleteBuffers(1, &mouseVBO);
    glDeleteVertexArrays(1, &mouseVAO);
    glDeleteBuffers(1, &VBO);
    glDeleteVertexArrays(1, &VAO);
    glfwTerminate();
    return 0;
}
