# 3D Dynamic Nature Simulation

A modern 3D nature simulation built with **OpenGL 3.3 Core**, **GLEW**, **GLFW**, and **GLM**, designed and configured to build and run seamlessly in **Code::Blocks** on Windows.

---

## 🚀 How to Run in Code::Blocks

1. Open **Code::Blocks**.
2. Go to **File -> Open...** and select `nature3d.cbp` (or double-click `nature3d.cbp` in Windows Explorer).
3. In the toolbar, ensure the target is set to **Debug** or **Release**.
4. Press **F9** (or click **Build and run** ▶).
5. The application will compile cleanly with 0 errors and run immediately!

---

## 🎮 Controls

| Action | Control |
|---|---|
| **Orbit Camera** | Left Mouse Button + Drag |
| **Pan Camera Target** | Right Mouse Button + Drag / WASD Keys |
| **Zoom In / Out** | Mouse Scroll Wheel |
| **Toggle Airplane** | `P` (Show / Hide flying airplane) |
| **Toggle Aurora Borealis** | `A` (Northern lights active at night) |
| **Toggle Birds** | `B` (Flock of birds soaring during day) |
| **Toggle Fireflies** | `F` (Pulsing forest lights active at night) |
| **Reset Camera View** | `R` |
| **Pause / Resume Time** | `Spacebar` |
| **Adjust Time Speed** | `Up Arrow` (Faster) / `Down Arrow` (Slower) |
| **Manual Time Jump** | `[` (1 hr back) / `]` (1 hr forward) |
| **Weather: Clear Sky** | `1` or `Numpad 1` |
| **Weather: Overcast / Clouds** | `2` or `Numpad 2` |
| **Weather: Rain** | `3` or `Numpad 3` |
| **Weather: Thunderstorm** | `4` or `Numpad 4` (Dark storm clouds, heavy rain, lightning flashes) |
| **Weather: Dense Fog** | `5` or `Numpad 5` |
| **Weather: Snow** | `6` or `Numpad 6` (Drifting snowflakes, snow-capped trees & terrain) |
| **Exit** | `Escape` |

---

## 🌟 Key Features & Improvements

1. **Subdivided Water Wave Surface**:
   - Subdivided 50x50 dynamic mesh grid with multi-harmonic sinusoidal wave ripples.
   - Depth-based aquatic coloring (from tropical cyan shallows to deep azure waters).
   - Sun and Moon specular reflections + Fresnel reflectance.

2. **Synchronized Terrain & Natural Lake Basin**:
   - Continuous heightfield with an organic lake basin.
   - Analytical normal calculation ensuring lighting on mountain peaks and lake shores is smooth and realistic.
   - Wetness darkening during rain and snow coverage during winter weather.

3. **Dense Pine Forest & Vegetation**:
   - Densely populated pine forest (360 trees) naturally distributed across hills and valleys while preserving lake basins.
   - Multi-tier conifer geometry with wind-sway animation responding in real-time to weather wind intensity.
   - Individual tree color variation and dynamic snow accumulation during winter weather.

4. **Atmospheric Wildlife & Objects**:
   - **Flying 3D Airplane (`P`)**: Twin-engine passenger jet cruising across the sky with banking turns, engine cowlings, and FAA navigation lights (red/green wingtips and strobe beacon).
   - **Flock of Birds (`B`)**: Soaring birds with sinusoidal flapping wings gliding in formation across the day sky.
   - **Night Aurora Borealis (`A`)**: Undulating green-to-violet curtains of northern lights waving across the midnight sky.
   - **Fireflies (`F`)**: 120 warm glowing fireflies hovering and drifting gracefully above the forest floor after sunset.

5. **Dynamic Celestial Cycle (Day / Night)**:
   - Atmospheric color gradients across Dawn, Morning, Midday, Sunset, Twilight, and Night.
   - Glowing Sun with coronal halo during the day.
   - Crescent Moon with lunar maria / craters and ambient nocturnal glow.
   - Twinkling stars visible at night that naturally fade with cloud cover.

5. **Weather & Storm Systems**:
   - Smooth interpolation between Clear, Cloudy, Rain, Thunderstorm, Fog, and Snow.
   - Procedural drifting FBM volumetric-style clouds that darken during storms.
   - Thunderstorm lightning system that flashes across the clouds, terrain, trees, and lake surface.
   - 2,400-particle precipitation system switching dynamically between fast rain streaks and swirling snowflakes.

6. **Self-Contained Code::Blocks Build**:
   - Configured with `-std=c++11` and `-DGLEW_STATIC`.
   - Native integration of GLEW and compatibility layer for MinGW runtime.
   - No missing external DLL dependencies (runs directly with Windows native runtime).
