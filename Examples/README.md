# QuickSDFTool Examples

This folder documents the smallest useful test scene for evaluating QuickSDFTool. The plugin already ships sample content under `Content/`, so this guide focuses on how to assemble a quick project-level test.

## Five-Minute Test Scene

1. Create or open a C++ Unreal Engine 5.7 project.
2. Copy this repository to `YourProject/Plugins/QuickSDFTool/`.
3. Build the project, enable **QuickSDFTool**, and restart the editor.
4. Add a simple Static Mesh or Skeletal Mesh to a level.
5. Assign a material that can later consume the generated threshold map. The included `Content/Materials/M_SDFToon.uasset` is the intended starting point.
6. Enter **Quick SDF** mode, select the mesh, and paint at least two angle masks.
7. Generate the threshold map and inspect `/Game/QuickSDF_GENERATED/`.

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

## What To Capture For Sharing

For GitHub, social posts, and issue reports, capture:

- The painted mask view.
- The generated SDF threshold texture.
- The final toon-shaded result.
- UE version and plugin commit/tag.
