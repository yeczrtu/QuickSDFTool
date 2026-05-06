---
title: Troubleshooting
description: Common QuickSDFTool installation, build, material, and brush behavior issues.
permalink: /troubleshooting/
---

# Troubleshooting

## The Plugin Does Not Appear In The Editor

- Confirm the project is a C++ project and was rebuilt after copying the plugin.
- Confirm the plugin folder is `YourProject/Plugins/QuickSDFTool/`.
- Confirm `QuickSDFTool.uplugin` is directly inside that folder.
- Regenerate project files and rebuild from Visual Studio or your normal UE build workflow.

## The Generated Shadow Looks Inverted

- First verify the comparison direction in your project material and check whether the result matches the painted masks.
- Confirm whether you are using a Monopolar or Bipolar output path, and follow the R/A/B/G output layout documented in [Material Setup]({{ '/material-setup/' | relative_url }}).
- The built-in Generated SDF preview uses the same exported threshold-map layout as the saved texture. If your project material is inverted while the built-in preview is correct, the project shader comparison direction or light-angle mapping is the likely mismatch.

## Brush Size Feels Wrong On Some Meshes

Brush size can appear inconsistent when UV density varies strongly across the mesh. For now:

- Test on clean, evenly distributed UVs first.
- Use the 2D UV preview for precise edits.
- Avoid judging brush behavior on heavily stretched UV islands until the UV brush-size item is addressed.

## Pen Display Or Tablet Strokes Connect From The Previous Hit

On some pen displays and tablets, hover movement while the pen is lifted may not update the brush position. The next painted stroke can then start from the previous brush hit and create an unintended connecting segment.

- Move the pen over the target area until the brush preview updates before starting a stroke.
- Prefer Screen mode for camera-facing edits where the brush preview is easier to confirm.
- If this reproduces consistently, include the tablet model, driver version, monitor DPI scale, and whether the issue happens in Screen, Surface, or UV mode when reporting it.

## GPU JFA Is Only Used For Live SDF Preview

`Live SDF` uses a GPU JFA approximation for responsive in-editor feedback. The saved `Generated SDF` output still uses the CPU `FSDFProcessor` path.

- Use `Live SDF` to check shape while painting.
- Use `Generated SDF` or the saved texture when validating final output quality, channel packing, UV island mirror behavior, lilToon-compatible output, or upscaled output.
- If the live preview looks too coarse, increase **Advanced > Live SDF Preview Resolution** from `512 px` to `1024 px`. Lower it to `256 px` or `128 px` if editor input becomes less responsive.

## UE 5.4 / 5.5 / 5.6 Build Issues

QuickSDFTool v1.0 supports UE 5.7.x, with UE 5.7.4 as the required release verification target. Earlier versions may need source edits around editor tooling, modeling components, material baking, or shader module setup. UE 5.8+ is intended to be supported, but is not part of the v1.0 release verification matrix.

When reporting compatibility issues, include:

- Unreal Engine version.
- Compiler version.
- Full build error.
- QuickSDFTool commit or release tag.
