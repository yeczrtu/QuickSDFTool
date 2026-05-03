# Material Setup

QuickSDFTool outputs a threshold texture intended to drive stylized light-to-shadow transitions. The exact shader graph can vary by project, but the core idea is stable: compare a light-angle value against the generated texture value, then use the result to choose lit or shadow color.

## Basic Shader Concept

```text
light direction / artist control value
    compared with
SDF threshold map sample
    produces
toon shadow mask
```

Use the included `Content/Materials/M_SDFToon.uasset` as the first reference material. It is meant to show the expected data flow rather than dictate a final production shader.

## Texture Expectations

- Generated maps are saved under `/Game/QuickSDF_GENERATED/` by default.
- The output is designed for precision, so keep compression and color-space settings appropriate for data textures.
- Treat the map as threshold data, not as a color/albedo texture.
- If the shadow appears inverted, verify the material comparison direction and the light-angle value first. The built-in Generated SDF preview uses the same exported threshold-map layout as the saved texture.

## Original Shading Bake Parameters

`M_OriginalShading` receives these dynamic parameters during the original-shading bake:

- `Angle`: authored light angle in degrees.
- `BakeForwardAngleOffset`: forward-axis offset in degrees. Static Mesh uses `0`; Skeletal Mesh uses `90` because the visual forward axis is treated as component local +Y.

For actor-rotation-independent output, build the bake direction from `Angle + BakeForwardAngleOffset` and compare it with normalized `PixelNormalWS.rg` in the material baking space. QuickSDF neutralizes the primitive transform for this bake, so do not transform `PixelNormalWS` from world to local in `M_OriginalShading`.

## Mesh Preview Material Parameters

QuickSDF loads two mesh preview material interfaces:

- Opaque replacement: `/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat`
- SceneColor multiply overlay: `/QuickSDFTool/Materials/M_PreviewSceneColorOverlay.M_PreviewSceneColorOverlay`

The opaque material is used for replacement preview and for the upper-left 2D texture preview. The overlay material is used only for the original-material overlay preview. Blend mode is not changed dynamically from C++; `M_PreviewSceneColorOverlay` must be a real material asset whose Blend Mode is Translucent. Do not make it a material instance of opaque `M_PreviewMat`, because opaque parents cannot use `SceneColor`.

Preview modes:

- `Original + Painted`: restores the original material slots and applies the translucent preview with `UMeshComponent::SetOverlayMaterial()`.
- `Painted Texture`: applies the opaque preview as a replacement material.
- `Painted + UV`: applies the opaque preview as a replacement material.
- `Painted + Shadow`: applies the opaque preview as a replacement material.

The default mesh preview mode is `Original + Painted`.

Both dynamic material instances receive these parameters:

- Texture `BaseColor`: active mask render target.
- Scalar `PreviewMode`: `0 = Painted Texture`, `1 = Painted + UV`, `2 = Painted + Shadow`.
- Scalar `UVChannel`: active UV channel index.
- Scalar `Angle`: active timeline angle in degrees. This matches `M_OriginalShading`.
- Scalar `BakeForwardAngleOffset`: forward-axis offset in degrees. This matches `M_OriginalShading`.

For `Painted + Shadow`, use the same `Angle + BakeForwardAngleOffset` direction calculation as `M_OriginalShading` so the mesh preview matches the baked mask. Only `M_PreviewSceneColorOverlay` should be translucent. `M_PreviewMat` should stay opaque and should not contain a `SceneColor` node.

For `Original + Painted`, implement `M_PreviewSceneColorOverlay` with `SceneColor` multiplied by the black/white painted mask from `BaseColor`. C++ always sends `PreviewMode = 0` to this overlay path.

The upper-left 2D texture preview uses a separate dynamic instance of `M_PreviewMat` and always forces `PreviewMode = 0`, so it remains a black/white painted texture preview regardless of the selected mesh preview mode.

## Monopolar vs Bipolar

- **Monopolar** is the simpler case. Symmetric output can use the same threshold across RGB; separate 0-90 / 90-180 output uses R/A.
- **Bipolar** stores shadow enter/exit behavior separately, useful for asymmetric shapes where a shadow enters and leaves a region differently as the light rotates. Values are generated in the legacy R/G/B/A field and exported with an R/A/B/G swizzle, preserving B as the 0-90 exit value.

## LilToon / General Toon Shader Notes

QuickSDFTool does not require a specific toon shader. For LilToon-inspired or similar pipelines, use the generated map as an authored control texture:

- Sample the threshold map in the same UV channel used during painting.
- Convert the current light angle or directional term into the same normalized range expected by the material.
- Compare the light value and threshold value.
- Blend lit and shadow ramps with the comparison result.

Keep the first integration simple. Once the direction and value range are confirmed, add project-specific ramps, softening, and color grading.
