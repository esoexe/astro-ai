# 3D Pack

13 skills for 3D on the web with three.js r186 and React Three Fiber 9: scenes, cameras and renderers, geometry and instancing, PBR materials, lighting and shadows, textures, animation, glTF loading, shaders and TSL, post-processing, interaction, performance and the glTF asset pipeline (Draco, meshopt, KTX2), on WebGL or WebGPU.

> **Read first.** The skills target three.js r186 (three@0.186), React Three Fiber 9.8 with drei 10.7 and gltf-transform 4.5. On another version, check the release notes; the agent should say which version it wrote for. 3D costs battery and data on phones: the Design Pack's design director keeps text in HTML, loads the canvas lazily and respects reduced motion.

Adapted from [threejs-skills](https://github.com/CloudAI-X/threejs-skills) by CloudAI-X (MIT). See NOTICE.md.

## Skills

| Skill | |
|---|---|
| `threejs-animation` | Keyframe, skeletal and morph-target animation, procedural motion and clip blending, on WebGL or with WebGPU and TSL. Use when animating objects, playing glTF animations, posing bones, driving morph targets, or… |
| `threejs-asset-pipeline` | Shrinking glTF assets for three.js with gltf-transform: inspect, validate and optimize commands, Draco, meshopt and quantization, KTX2, WebP and AVIF textures, the loader setup each output needs, and gltfjsx… |
| `threejs-fundamentals` | Three.js scene setup: scenes and fog, cameras, the renderer and tone mapping, the animation loop, object hierarchies, transforms, core math and cleanup, on WebGL or with WebGPU and TSL. Use when creating a… |
| `threejs-geometry` | Creating and shaping three.js geometry: built-in shapes, 3D text, vertex buffers and attributes, merging, instancing, batching, point clouds and lines, on WebGL or with WebGPU and TSL. Use when building 3D… |
| `threejs-interaction` | Picking with raycasting, camera controls, transform gizmos, drag, hover and box selection for three.js, identical under WebGL and WebGPU. Use when handling mouse, touch or keyboard input, detecting clicks on… |
| `threejs-lighting` | Light types and physical units, shadow maps, HDR environment lighting, light probes, and lighting performance, on WebGL or with WebGPU and TSL. Use when adding or tuning lights, fixing shadow acne, setting up… |
| `threejs-loaders` | Loading three.js assets: glTF with Draco, meshopt and KTX2 compression, other model formats, textures, HDR and EXR environments, progress, caching, cloning and disposal. Use when loading 3D models, textures or… |
| `threejs-materials` | Choosing and configuring three.js materials - unlit, diffuse, specular, PBR, toon, point and line materials - shared properties like blending and stencil, environment response, and the WebGPU node-material and… |
| `threejs-performance` | Measuring and optimizing three.js speed: render statistics, memory counters, fewer draw calls via merging, instancing, batching and LOD, fill rate, shadows, CPU work per frame, shader stalls and GPU compute… |
| `threejs-postprocessing` | Three.js post-processing: composer pass chains, built-in renderer effects, and the WebGPU/TSL node pipeline — bloom, depth of field, ambient occlusion, custom screen effects. Use when adding visual effects… |
| `threejs-react-three-fiber` | Building three.js scenes from React with React Three Fiber and drei: renderer setup, the scene graph as JSX, frame-loop, state and loading hooks, pointer events, and adaptive performance, on WebGL or with… |
| `threejs-shaders` | Custom GLSL vertex and fragment shaders: uniforms, varyings, shader chunks, extending built-in materials, instanced attributes, plus the same effects as node materials with TSL on WebGPU. Use when writing a… |
| `threejs-textures` | Three.js textures: image loading, color spaces, UV channels and transforms, texture types from canvas to HDR and KTX2, render targets, texture memory. Use when working with images on materials, UV coordinates… |

## Use

Ask for the 3D job in plain words ("add a rotating 3D product viewer to the hero", "make this scene run faster on phones"); the agent loads the matching skill. Text only: nothing to install in A.S.T.R.O AI; your project installs three or @react-three/fiber as usual.
