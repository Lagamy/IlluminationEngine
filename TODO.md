# ToDo

Open work that is not started. Add an item when it is decided; remove it when done (the result goes in the engine docs).

- **Skybox: fullscreen triangle** (user, 2026-10-22; decided, not started, no design file).
  - **Decision (user):** the engine draws its sky with an oversized fullscreen triangle (clip-space corners (-1,-3), (-1,1), (3,1); 3 vertices, no vertex buffer), not a cube or dome mesh.
  - **Technique (external, Godot master, read through summaries of `servers/rendering/renderer_rd/environment/sky.cpp`, `shaders/environment/sky.glsl` and `drivers/gles3/shaders/sky.glsl`; not verified line by line):** the fragment shader rebuilds each pixel's view direction from the projection terms and the camera rotation (translation ignored, so the sky looks infinitely far), then samples the sky data (cubemap, panorama or procedural function). It is drawn after the opaque geometry at a constant depth (reversed-Z aware), blending off.
  - **Open (unknown, not decided):** the sky data format (cubemap, equirect panorama, octahedral or procedural); where the pass goes in the existing render order and which depth convention applies; where the shader lives (`IluminationEngine/Shaders/`); how a game supplies its sky.
  - **Caveat (hypothesis, not checked in the decompiles):** the original games' skies are authored models. The triangle cannot reproduce a decoded sky mesh, so a faithful original sky would be that model drawn centred on the camera. This item is for the engine's own sky.
  - **Known artifacts of the method (general knowledge):** equirect panorama wrap line at the atan2 seam and pinching at the poles, octmap edge seams, banding on gradients. No geometry seams or facets.
