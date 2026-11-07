---
name: game-developer
description: 2D and 3D game engineering, WebGL/WebGPU graphics, GLSL shaders, Three.js, Phaser, Canvas, game loops, spatial physics, sprite animations, and audio synthesis.
---

# Game Developer — 2D/3D Game Engineering & Interactive Graphics

Create engaging, responsive web and desktop games with deterministic game loops, physics simulations, particle effects, and spatial audio.

## Core Disciplines

1. **2D Games (Canvas & Phaser)**
   - Fixed-timestep delta-time update loop (`requestAnimationFrame` with accumulator).
   - Collision detection: AABB (Axis-Aligned Bounding Box), circle-circle, SAT (Separating Axis Theorem), spatial hashing for broad-phase optimization.
   - Sprite sheet animation, tilemaps, state machines (player movement, jumping, combat, AI pathfinding like A*).
   - Audio synthesis: Web Audio API procedural sound effects (explosions, laser blasters, coin pickups, melodic background chiptunes).

2. **3D Games & Graphics (Three.js / WebGL / WebGPU)**
   - Scene hierarchy, cameras (perspective, orthographic, cinematic follow), shadows, environmental lighting.
   - GLSL custom shaders: Vertex shaders, Fragment shaders, post-processing filters (bloom, chromatic aberration, scanlines, vignette).
   - Object pooling and geometry instancing to minimize draw calls and garbage collection pauses.
   - Physics integration: Cannon-es, Rapier, or custom Verlet integration.

3. **Game Design Best Practices**
   - Responsive canvas scaling with aspect-ratio preservation.
   - Dual input handling: Keyboard + Mouse + Touch controls (virtual joystick, action buttons) + Gamepad API.
   - Pause states, game over sequences, high score persistence in localStorage / IndexedDB.
