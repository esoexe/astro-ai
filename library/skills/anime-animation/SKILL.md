---
name: anime-animation
description: High-performance JavaScript animation engine skill based on anime.js (juliangarnier/anime). Craft fluid CSS transforms, SVG morphing, path tracing, DOM attribute tweening, staggered orchestrations, complex timelines, and canvas motion graphs.
---

# Anime.js — JavaScript Animation Engine & Motion Systems

anime.js is a lightweight, dependency-free JavaScript animation engine with a clean, chainable API. It works seamlessly with CSS properties, individual CSS transforms, SVG attributes, DOM attributes, and raw JavaScript objects.

## Core Capabilities

1. **Target Types & Properties**
   - **CSS Properties**: Animate any CSS property (`opacity`, `backgroundColor`, `borderRadius`, `clipPath`).
   - **Individual CSS Transforms**: Independent coordinate control without string parsing (`translateX`, `translateY`, `scale`, `rotate`, `skew`, `perspective`).
   - **SVG Attributes & Morphing**: Animate SVG attributes (`d`, `points`, `strokeDashoffset`, `viewBox`). Morph complex paths with equivalent vertex counts.
   - **DOM Attributes**: Animate standard attributes directly (for example, `value` on `<progress>` or `<input>`).
   - **JavaScript Objects**: Tween numeric values inside arbitrary object states and render to WebGL/Canvas contexts.

2. **Timing, Easing & Springs**
   - Built-in easing curves: `linear`, `easeInQuad` through `easeOutExpo`, `easeInOutElastic`, `cubicBezier(x1, y1, x2, y2)`.
   - Physics-based spring simulations: `spring(mass, stiffness, damping, velocity)` for natural gesture response.
   - Stepped motion: `steps(numberOfSteps)` for frame-by-frame sprite sheets.
   - Function-based values: dynamically resolve parameters based on target index, total count, or external state `(el, i, total) => i * 50`.

3. **Timelines & Orchestration**
   - Synchronize multiple animations using `anime.timeline({ easing, duration })`.
   - Position offsets: absolute timestamps (`1000`), relative shifts (`"-=400"`), and sequential chaining (`"+=200"`).
   - Staggering: `anime.stagger(value, { start, from, direction, grid, axis })` for cascading grids and lists.

4. **Path Following & SVG Tracing**
   - Motion path tracking: `anime.path('pathSelector')` calculates `x`, `y`, and `angle` rotation automatically.
   - Line drawing effects: animate `strokeDashoffset` using `anime.setDashoffset`.

5. **Playback Controls & Lifecycle Hooks**
   - Methods: `play()`, `pause()`, `restart()`, `reverse()`, `seek(progress)`.
   - Callbacks: `update`, `begin`, `complete`, `loopBegin`, `loopComplete`.
   - State promises: `animation.finished.then(() => ...)` for asynchronous control loops.

## Best Practices

- Prefer `transform` and `opacity` animations to stay off the main layout thread and avoid layout recalculation (reflow).
- Use `will-change: transform` on animated elements sparingly, removing it when animations conclude.
- For high-density staggered lists, batch target selections and stagger offsets to keep frame budgets at 60/120 FPS.
