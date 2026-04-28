# User Manual — Swarm Simulation

---

## Getting Started

After launching the executable you will see:

- **Black canvas** filled with white dots — these are your agents.
- **Controls panel** (top-left) — all simulation parameters.
- **Performance overlay** (top-right) — FPS, frame time, agent counts.

Move your mouse over the canvas to repel agents.  Press **F** for fullscreen.

---

## The Controls Panel

### Simulation Section

| Control | What it does |
|---------|-------------|
| Play / Pause | Freeze or resume simulation |
| Reset | Reinitialise all agents at random positions |
| Step | Advance exactly one frame (only when paused) |
| Agent Count | Drag to change population; triggers a reset |
| Time Scale | Values > 1 speed up; < 1 slow down |

### Boids Parameters

Adjust these live — changes take effect immediately:

- **Separation** — increase to spread agents apart; high values cause chaotic scattering.
- **Alignment** — increase for tighter heading synchronisation (more "flock-like").
- **Cohesion** — increase to pull agents into denser groups.
- **Perception Radius** — larger = each agent sees more neighbours (also affects spatial hash cell size).
- **Max Speed / Max Force** — cap velocity and steering; reduce for gentle flows.

### Presets

Five built-in configurations:

| Preset | Character |
|--------|-----------|
| Bird Flock | Loose, sweeping formation — classic boids |
| Fish School | Tight ball, rapid direction changes |
| Chaos | Explosive random motion |
| Orbiting | Slow vortex; agents orbit a common centre |
| Murmuration | Dense wave-like rippling (starling-inspired) |

Click **Apply** to load, **Save** to persist your own tuning to `config/presets.json`.

### Visual Settings

| Setting | Options / Range |
|---------|-----------------|
| Color Scheme | Uniform white / Velocity heat-map / Type (green=prey, red=predator) / Rainbow |
| Agent Size | Point size in pixels (1 – 8) |
| Trail Length | 0 = off; higher = longer motion blur trail |
| Show Grid | Overlay the spatial hash cell grid |
| Velocity Vectors | Draw heading lines on each agent |
| Camera FOV | Field of view for 2.5D perspective mode |

### Predator / Prey

- **Predator Ratio** — set before hitting Reset; predators are spawned red with higher speed.
- **Fear Weight** — how urgently prey flee; try 5–8 for dramatic scatter events.
- Buttons: **Add Predator** / **Add Prey** spawn a single agent at a random position.

### Obstacles

1. Choose obstacle **Type** (Circle / Rectangle / Line).
2. Click **Add** — a new obstacle appears at world origin.
3. Expand its tree node to drag its position, resize it, or enable **Moving**.
4. Moving obstacles bounce off world boundaries.
5. Click **Remove** to delete.

### Environment

- **Wind X/Y** — constant force applied to all agents (try ±0.2 for a gentle drift).
- **Attractor / Repulsor** — place a point that attracts (positive strength) or repels (negative). Agents within the radius steer toward/away.

### Export & Record

| Button | Action |
|--------|--------|
| Screenshot | Saves current frame as `screenshot_NNNN.png` |
| Start/Stop Recording | Saves a numbered frame sequence |
| Export State | Writes full simulation state to JSON |
| Load State | Restores a previously exported state |
| Export Params | Appends current parameters to `config/presets.json` |

---

## Tips for Interesting Patterns

### Tight Murmuration
```
Separation: 1.8   Alignment: 3.5   Cohesion: 1.2
Time Scale: 1.2   Agent Count: 15 000
```
Watch the wave-like ripple propagate through the group.

### Vortex / Orbit
```
Separation: 0.5   Alignment: 4.0   Cohesion: 4.0
Max Speed: 0.35   Time Scale: 0.8
```
The whole swarm slowly rotates as a single disc.

### Predator Drama
1. Load **Bird Flock** preset.
2. Set **Predator Ratio = 0.02**, **Fear Weight = 6**.
3. Hit Reset.
4. Watch prey school fragment and reform when predators are far.

### Obstacle Weaving
1. Add 6–8 circle obstacles spread across the canvas.
2. Load **Fish School** preset.
3. Enable **Moving** on each obstacle for a dynamic course.

---

## Parameter Tuning Guide

**If agents pass through each other too often** → increase Separation (try 2–3).

**If the flock breaks into many small fragments** → increase Cohesion or reduce Separation.

**If agents all move in one direction and never flock** → reduce Alignment and increase Cohesion.

**If simulation is unstable / agents fly to infinity** → reduce Max Speed and Max Force; check Time Scale ≤ 1.5.

**If predators never catch prey** → increase Predator Speed Mul or reduce Max Speed of prey.
