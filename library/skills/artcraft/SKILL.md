---
name: artcraft
description: AI-powered 2D/3D compositing, scene blocking, 3D mesh generation, Gaussian Splat world reconstruction, character posing, mannequin identity transfer, and precision prompted canvas editing based on the open-source ArtCraft IDE (storytold/artcraft).
---

# ArtCraft — The IDE for Interactive AI Art & 3D Staging

ArtCraft turns text-to-image and image-to-video prompting into precise, repeatable visual crafting. Grounded in the open-source ArtCraft architecture, this skill gives Astro full creative direction capabilities across 2D compositing, 3D world staging, character posing, and canvas inpainting.

## Core Capabilities

1. **2D Canvas Compositing & Layering**
   - Multi-layer composition with z-ordering, alpha masks, opacity, and blend modes (screen, multiply, overlay, soft-light).
   - Background isolation, foreground placement, prop kitbashing, and canvas crop/extend.
   - SVG and Canvas 2D scene definitions with pixel-perfect coordinates.

2. **3D Scene Staging & Camera Blocking**
   - Three.js / WebGL 3D camera staging: perspective control, focal length (24mm, 35mm, 50mm, 85mm cinematic lenses), depth of field, and lighting setups (three-point, rim lighting, volumetric fog).
   - Kitbashing 3D asset meshes (glTF, OBJ, USDZ) for accurate spatial perspective before generation.

3. **Gaussian Splatting & World Generation**
   - 3D Gaussian Splat scene description and camera trajectories (pan, tilt, dolly, orbit).
   - Transforming 2D conceptual artwork into navigable 3D splat environments for shot planning and game cinematics.

4. **Character Posing & Identity Transfer**
   - Skeletal rigging and mannequin pose matrices (head rotation, spine curve, limb angles).
   - Consistent character identity preservation across multiple sequential shots, angles, and lighting conditions.

5. **Prompted & Canvas Inpainting**
   - Bounding-box and mask-guided targeted inpainting.
   - Lighting consistency matching between composite foregrounds and AI-generated backgrounds.

## Workflows

### 1. Cinematic Shot Staging
```json
{
  "shot": "cyberpunk_alley_confrontation",
  "camera": { "lens": "35mm", "fov": 54, "position": [0, 1.4, -3], "target": [0, 1.2, 0] },
  "layers": [
    { "type": "splat_world", "source": "rainy_neo_shinjuku_splat", "depth": 10.0 },
    { "type": "actor_mesh", "pose": "defensive_low_stance", "model": "cyber_operative_01", "position": [0.4, 0, 0] },
    { "type": "foreground_prop", "asset": "steaming_vent_exhaust", "position": [-1.2, 0, -1.0] }
  ],
  "lighting": { "key": "#00f0ff", "rim": "#ff0055", "ambient": "#0a0a1a" }
}
```

### 2. Character Consistency Pipeline
- Establish a canonical character model card (facial structure, attire, distinctive markings).
- Supply mannequin wireframes or depth maps to lock geometry.
- Apply high-denoise inpainting only to non-identity elements.
