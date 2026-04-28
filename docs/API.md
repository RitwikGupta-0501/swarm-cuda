# API Reference — Swarm Simulation

---

## ui.h / ui.cpp  _(Person 4)_

### `void renderUI(bool& paused, float& speed, float& separation, float& alignment, float& cohesion)`
Minimal legacy UI.  Used by the original `main.cpp`.  
Renders a single "Controls" ImGui window with five controls.

### `void renderFullUI(SimParams&, RenderOptions&, SimStats&, vector<Obstacle>&, bool& paused, bool& screenshotRequested, bool& recordingActive)`
Full-featured UI entry point.  Call once per frame **after** `ImGui::NewFrame()` and **before** `ImGui::Render()`.

**Parameters**

| Name | Direction | Description |
|------|-----------|-------------|
| `params` | in/out | All simulation tuning values. `reinitRequested` is set here. |
| `renderOpts` | in/out | Visual settings forwarded to the renderer. |
| `stats` | in | Read-only performance metrics shown in overlay. |
| `obstacles` | in/out | List modified by the obstacle editor. |
| `paused` | in/out | Toggled by Play/Pause button. |
| `screenshotRequested` | out | Set to `true` for one frame when button pressed. |
| `recordingActive` | in/out | Toggled by Record button. |

---

## presets.h / presets.cpp  _(Person 4)_

### `void loadPreset(int presetIndex, SimParams& params)`
Fills `params` from one of the five built-in presets.  
`presetIndex` is a value from `PresetIndex` enum (0–4).

### `void savePreset(const string& name, const SimParams& params)`
Appends a named JSON block to `config/presets.json`.

### `bool loadPresetByName(const string& name, SimParams& params)`
Scans `config/presets.json` for a block whose `"name"` field matches.  
Returns `true` on success; `params` is unchanged on failure.

---

## obstacles.h / obstacles.cpp  _(Person 4)_

### `void computeObstacleAvoidance(px, py, vx, vy, obstacles, avoidWeight, outAx, outAy)`
CPU reference implementation of ray-cast avoidance.  
Projects a look-ahead point along the velocity vector; for each obstacle that intersects the safety margin, adds a steering force pointing away from the surface.

**Intended use:** validate against the GPU version; also useful for small agent counts or CPU fallback.

| Parameter | Description |
|-----------|-------------|
| `px, py` | Agent world position |
| `vx, vy` | Agent velocity |
| `avoidWeight` | Force multiplier (try 2–5) |
| `outAx, outAy` | Accumulated avoidance acceleration (added to, not overwritten) |

### `void updateMovingObstacles(vector<Obstacle>& obstacles, float dt)`
Advances position of each obstacle whose `isMoving == true`.  
Bounces off the ±0.95 world boundary.  Call once per frame before the simulation step.

### `void saveObstacles(const vector<Obstacle>&, const char* path)`
Writes obstacle list to a JSON file.

### `bool loadObstacles(vector<Obstacle>&, const char* path)`
Reads obstacle list from a JSON file.  Returns `true` if any obstacles were loaded.

---

## scenarios.h / scenarios.cpp  _(Person 4)_

### `void startScenario(ScenarioID, SimParams&, vector<Obstacle>&, ScenarioState&)`
Configures params and obstacles for the chosen demo scenario.  
Sets `params.reinitRequested = true` to trigger agent respawn.

### `void updateScenario(ScenarioState&, SimParams&, vector<Obstacle>&, float dt)`
Per-frame tick for the active scenario (e.g. moves migration attractor, escalates predator behaviour).  
No-op when `state.running == false`.

### `void stopScenario(ScenarioState&, vector<Obstacle>&)`
Clears obstacles and resets the scenario timer.

### `const char* scenarioName(ScenarioID)`
Returns a human-readable name for display in UI.

---

## simulation.h  _(Person 2)_

### `void initSimulation(int agentCount)`
Allocates GPU memory and spawns agents at random positions.  
Must be called before any `stepSimulation` call; safe to call again to reinitialise.

### `void stepSimulation(float dt, float mouseX, float mouseY, float separation, float alignment, float cohesion, float speed)`
Runs one simulation tick on the GPU:
1. Builds spatial hash.
2. Launches boids kernel.
3. Synchronises and unmaps VBO.

### `void registerRenderBuffer(GLuint vbo)`
Registers an OpenGL VBO for CUDA–OpenGL interop.  Must be called after `initSimulation`.

### `void unregisterRenderBuffer()`
Releases the CUDA graphics resource.  Called automatically by `shutdownSimulation`.

### `void shutdownSimulation()`
Frees all GPU memory and destroys the spatial hash.

### `int getAgentCount()`
Returns the current number of agents.

---

## Integration Data Structures

### `SimParams`  _(ui.h)_
All fields are public floats/ints; safe to read from any thread after the frame boundary.

### `RenderOptions`  _(ui.h)_
Passed to `renderer.cpp` each frame; controls colour scheme, point size, trail length, FOV.

### `SimStats`  _(ui.h)_
Filled by the main loop (FPS, frame time) and by simulation (avg speed, agent type counts).  
Read-only from `renderFullUI`.

### `Obstacle`  _(ui.h)_
CPU struct.  Fields: `type` (OBS_CIRCLE / OBS_RECT / OBS_LINE), `x/y` (centre), `x2/y2` (half-extents or end-point), `radius`, `isMoving`, `moveSpeedX/Y`.
