Awesome, I'm glad that completely crushed the bug! It was an incredibly interesting interplay between different pieces of the rendering pipeline.

Here is a full summary of the root cause and how we untangled it:

### The Root Cause: Extreme Overdraw
The solid white screen and the drop to 2 FPS wasn't a memory crash or a shader syntax error—it was a **massive GPU fill-rate bottleneck** caused by your agents becoming infinitely large.

1. **Coordinate Space Shift:** Originally, the application used a literal pixel-based coordinate system (e.g., `-640` to `640` across your window). Because of this, the agent vertices (`aBase`) were hardcoded to `10.0f` units, making them perfectly 10 pixels wide.
2. **The Breaking Change:** Recently, `camera.cpp` was updated to use a modern, normalized aspect-ratio projection where the *entire height of the screen* is just `2.0` units. However, the raw geometry of the agents was never updated to match this new scale. This meant each individual agent was still `10.0f` units tall, making a single boid **five times taller than your entire monitor!**
3. **The "Threshold" Illusion:** You noticed the crash only happened when you zoomed in. This is because the `Renderer` has an optimization built in: when you zoom out far enough (`zoom < 0.2`), it skips drawing triangles entirely and draws cheap, 1-pixel `GL_POINTS`. This gave you smooth 150+ FPS at a distance. But the second you zoomed in past that `0.2` threshold, it switched to drawing the triangles.
4. **The White Screen:** When the pipeline swapped back to drawing triangles, it attempted to draw 10,000 agents that were 5x larger than your screen. Rasterizing 10,000 full-screen white polygons exactly on top of each other forced the GPU to process billions of fragment calculations every single frame, resulting in the pure white screen and a grinding halt to your FPS.

### How We Solved It
1. **Synchronizing the Bounding Boxes:** First, using your debug logs, we noticed the culling bounds (`[CULL]`) were in the thousands, while the view projection matrices (`[RENDER]`) were tiny. We updated the frustum-culling AABB logic (`cameraWorldAabb`) in `renderer.cpp` so it correctly understood the new `2.0` unit world space. 
2. **Scaling the Primary Geometry:** We jumped into `createAgentPipeline()` and applied a scale factor of `0.003f` to the raw geometry array, bringing the agents down from screen-crushing leviathans to properly sized boids in the normalized coordinate system.
3. **Patching the Hidden Duplicate Pipeline:** After the initial fix failed, we investigated the indirect drawing code. We discovered that the renderer actually maintains a **second, entirely independent geometry buffer** used strictly for the frustum-culling pass (`mCullAgentBaseVbo`). It had its own duplicate of the massive `10.0f` scale array! We applied the identical `0.003f` scaling factor to this secondary buffer, completely eliminating the bottleneck and restoring functionality.