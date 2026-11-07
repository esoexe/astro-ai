---
name: cardboard
description: Open-source visual design and canvas layout skill based on Cardboard (goerll/cardboard & Cardboard-Inc). Agent-native canvas co-editing, real HTML/CSS layers, timeline adjustments, responsive layout auditing, and bidirectional visual edits.
---

# Cardboard — Collaborative Visual Canvas & Design System

Cardboard bridges AI coding agents and visual canvas manipulation: every layer is real HTML and CSS, and agent actions modify the exact same visual hierarchy and timeline.

## Core Capabilities

1. **Agent Canvas Co-Editing**
   - Read and write layout trees with CSS Flexbox, Grid, nested containers, gradients, box shadows, and responsive constraints.
   - Bidirectional updates: manipulate design components via JSON commands, and immediately preview rendered Blink DOM layout.
   - Unified undo/redo stack across human mouse interactions and automated agent operations.

2. **Component & Token Design System**
   - Color palettes: neutral ramps, accent primary/secondary, semantic tokens (success, warning, destructive, info).
   - Typography hierarchies: fluid type scales with clamp(), letter-spacing, line-height ratios.
   - Spacing scales: consistent 4px / 8px grid intervals.

3. **Timeline & Motion Crafting**
   - Keyframe animations, cubic-bezier transition curves, staggered entrance delays.
   - Frame-by-frame visual verification to prevent clipping, overflow, and layout shifts (CLS).
