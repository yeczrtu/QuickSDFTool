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
- If the shadow appears inverted, verify the material comparison direction first, then check the known SDF output-direction item in the roadmap.

## Monopolar vs Bipolar

- **Monopolar** is the simpler symmetric case. The same threshold can be used across the main channels.
- **Bipolar** stores shadow enter/exit behavior separately, useful for asymmetric shapes where a shadow enters and leaves a region differently as the light rotates.

## LilToon / General Toon Shader Notes

QuickSDFTool does not require a specific toon shader. For LilToon-inspired or similar pipelines, use the generated map as an authored control texture:

- Sample the threshold map in the same UV channel used during painting.
- Convert the current light angle or directional term into the same normalized range expected by the material.
- Compare the light value and threshold value.
- Blend lit and shadow ramps with the comparison result.

Keep the first integration simple. Once the direction and value range are confirmed, add project-specific ramps, softening, and color grading.
