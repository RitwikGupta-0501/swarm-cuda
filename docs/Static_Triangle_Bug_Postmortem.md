# Bug Report & Resolution: Static Triangle Artifact in 2.5D & High-Zoom Views

## The Issue
When navigating the simulation in the `feature-renderer-integration` branch, zooming in past a specific threshold (zoom > `0.2`) or switching the camera to 2.5D mode caused the entire swarm to instantly disappear. In its place, a single, static triangle appeared perfectly centered on the screen, completely unresponsive to the camera's panning or the agents' actual world positions. 

## The Root Cause
The bug was a classic **C++ object lifecycle / OpenGL state mismatch** resulting in the complete deletion of the agent shader before the simulation even started rendering.

In `Renderer::render()`, the engine optimizes rendering by using a basic "points" shader when zoomed far out. When the camera zooms in (or switches to 2.5D), it flips the `usePoints` boolean to `false` and attempts to bind the full agent shader (`mAgentProgram`).

However, inside `Renderer::createAgentPipeline()`, the `ShaderProgram` class (which acts as a smart wrapper for OpenGL shader compilation) was used to load the agent shaders, but the resulting program ID was never transferred to the renderer:

```cpp
bool Renderer::createAgentPipeline(std::string* outError) {
  ShaderProgram agent;
  if (!agent.loadFromFiles("shaders/agent.vert", "shaders/agent.frag", outError)) {
    // ... error handling
  }
  
  // MISSING: mAgentProgram = agent.release();

  ShaderProgram pts;
  // ...
