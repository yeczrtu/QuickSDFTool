# QuickSDFTool Examples

This folder documents the smallest useful test scene for evaluating QuickSDFTool. The plugin already ships sample content under `Content/`, so this guide focuses on how to assemble a quick project-level test.

## Five-Minute Test Scene

1. Create or open a C++ Unreal Engine 5.7 project.
2. Copy this repository to `YourProject/Plugins/QuickSDFTool/`.
3. Build the project, enable **QuickSDFTool**, and restart the editor.
4. Add a simple Static Mesh or Skeletal Mesh to a level.
5. Assign a material that can later consume the generated threshold map. The included `Content/Materials/M_SDFToon.uasset` is the intended starting point.
6. Enter **Quick SDF** mode, select the mesh, and confirm the active material slot in **Material Slots**.
7. Paint at least two angle masks in Screen mode, then open the **2D Canvas** and verify texture-space painting on the same slot and angle.
8. Test mouse and pen input if a tablet or pen display is available. Confirm hover, drag, pressure radius, `Ctrl + F` brush resize, and Quick Stroke in both 3D Paint and 2D Canvas.
9. Generate the threshold map and inspect `/Game/QuickSDF_GENERATED/`.

## Included Sample Assets

| Asset | Purpose |
| --- | --- |
| `Content/Materials/M_SDFToon.uasset` | Starting toon material for previewing the generated map. |
| `Content/Materials/M_PreviewMat.uasset` | Editor preview material used while painting. |
| `Content/Textures/T_SampleThresholdMap.uasset` | Reference threshold map for material experiments. |
| `Content/Textures/T_QuickSDF_ThresholdMap.uasset` | Default generated-map style asset. |

## Recommended First Test

Use a mesh with clean UVs and a visible front-facing surface, such as a head proxy, bust, or simple mannequin part. Paint three masks:

- `0 degrees`: mostly lit with a narrow shadow on one side.
- `45 degrees`: shadow crosses the form.
- `90 degrees`: stronger side shadow.

Generate the threshold map and connect it to the toon material. This gives a quick read on whether the mask direction, UVs, and material logic are aligned.

## Input And Canvas Checks

Before sharing a test result, verify the current input behavior:

- 2D Canvas brush circle and stroke position line up after moving or resizing the canvas window.
- 3D Screen mode shows the green brush circle during pen hover.
- Quick Stroke activates after hold, follows while moving, and commits the release position.
- Mouse painting remains responsive when Live SDF preview is enabled.
- Pen painting remains responsive at the chosen Live SDF preview resolution.

## What To Capture For Sharing

For GitHub, social posts, and issue reports, capture:

- Select mode active material slot overlay.
- Paint mode Screen Projection with the green brush circle, UV preview, and active slot context.
- Timeline thumbnails / seek lane / keyframe lane / status badges.
- Generated SDF threshold texture.
- 2D Canvas pen painting with the brush circle aligned to the stroke.
- 3D Screen mode pen hover with the green brush circle.
- Quick Stroke following in both 2D Canvas and 3D Paint.
- Advanced settings showing **Pen Pressure** and **Pen Pressure Curve**.
- UE version, plugin commit/tag, tablet/display model, driver version, monitor DPI scale, and whether Live SDF was enabled.
