# UE5 rendering learning labs

The project contains three controlled maps under `Content/LearningLabs/Maps`.
They use standard Default Lit materials so that the custom Kulla–Conty shading
model does not obscure the behavior of Nanite, Virtual Shadow Maps, or Lumen.

## L_NaniteLab

The orange and blue meshes are built from the same approximately 261k-triangle
sphere. The orange asset has Nanite disabled; the blue asset has Nanite enabled.
The level also contains repeated copies for observing instance count, clusters,
triangle selection, and overdraw.

Useful editor views:

- View Mode > Nanite Visualization > Overview
- Clusters
- Triangles
- Overdraw
- Material Complexity

Record `stat gpu` and `stat rhi` before drawing performance conclusions. Compare
at the same resolution, camera position, and editor scalability setting.

## L_VSMLab

The map contains thin geometry, boxes of increasing height, Nanite shadow
casters, contact-shadow spheres, a movable directional light, and a movable
point light named `L_Point_Movable_MoveMe`.

Experiments:

1. Move the point light and watch which shadow pages must be redrawn.
2. Change the directional light Source Angle to compare hard and soft penumbrae.
3. Inspect Virtual Shadow Map cache and page visualization modes.
4. Compare the small geometric detail of conventional and Nanite casters.

VSM produces direct-light shadows. It is separate from Lumen indirect lighting.

## L_LumenLab

The map is a Cornell-style room with red and green walls, two diffuse boxes, an
emissive panel, a movable point light, and five metallic spheres ranging from
roughness 0.02 to 0.95.

Experiments:

1. Move `L_Point_Primary_MoveMe` and observe dynamic color bleeding.
2. Toggle Lumen GI while keeping reflections enabled, then reverse the test.
3. Compare Lumen Overview, Lumen Scene, Surface Cache, and Reflection views.
4. Compare software and hardware ray tracing using identical camera/exposure.
5. After the systems are understood, replace one standard material with the
   Kulla–Conty material and inspect how rough reflection energy changes.

## Rebuilding

The source script is `Tools/Unreal/CreateRenderingLearningLabs.py`. Running it
reuses generated materials and meshes but rebuilds the three maps, so do not
keep manual map edits inside that folder unless the script has first been copied
or adjusted.
