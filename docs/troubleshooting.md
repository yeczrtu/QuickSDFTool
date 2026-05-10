---
title: Troubleshooting
description: Common QuickSDFTool installation, build, material, pen, 2D Canvas, Quick Stroke, and brush behavior issues.
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
- Use the 2D Canvas for precise texture-space edits.
- Avoid judging brush behavior on heavily stretched UV islands until the UV brush-size item is addressed.

## Pen Display Or Tablet Input Feels Misaligned

On Windows, QuickSDF reads pen pointer coordinates directly for 3D Paint and the 2D Canvas. Hover, stroke start, drag, release, pressure-sensitive radius, Quick Stroke, and `Ctrl + F` brush resizing should continue to track correctly after moving or resizing the editor or canvas window.

- Confirm **Advanced > Pen Pressure** is enabled if you expect pressure-sensitive radius. Disable it if you need a fixed-radius mouse-like stroke.
- Adjust **Advanced > Pen Pressure Curve** if the radius response feels too soft or too aggressive. `1.0` is linear, higher values keep light pressure thinner for longer, and lower values reach larger radii sooner.
- In the 2D Canvas, check **Fit** and **100%** views. The visible brush circle should stay aligned with the painted stroke in both.
- In 3D Paint, check both Screen and Surface projection. Mouse behavior and pen behavior should be compared on the same mesh, material slot, and timeline angle.
- When reporting a mismatch, include tablet/display model, driver version, Windows version, monitor layout, DPI scale per monitor, UE version, QuickSDFTool commit or release tag, and whether the issue happens in Screen, Surface, 2D Canvas, hover, drag, release, pressure, Quick Stroke, or `Ctrl + F` resize.

## The 3D Brush Preview Circle Disappears After Using The 2D Canvas

The green 3D brush circle is shown only while Paint mode has a valid viewport hit on the active paint target.

- Move the pointer back over the level viewport and confirm **Paint** mode is still active.
- Confirm the target mesh and active material slot are still selected. Select mode should show the cyan active-slot overlay; Paint mode should show the active slot context.
- Confirm the pointer is over the active slot surface. Non-target slots are filtered so they do not steal hits from the active slot.
- Close any open menu or modal window that might still be capturing pointer input from the 2D Canvas.
- If the issue happens only with a pen, lift the pen out of contact, hover over the 3D viewport again, and compare with mouse hover in the same spot.
- If the green circle does not return, capture whether `F` focus still works, whether strokes paint despite the missing preview, and whether changing angle or re-entering Paint mode restores it.

## Quick Stroke Feels Heavy Or Does Not Follow The Pen

Quick Stroke starts after a hold and commits on release. While moving the preview, QuickSDF keeps preview updates lightweight so high-frequency tablet input does not do full stroke work every frame.

- Confirm **Quick Stroke** is enabled from the quick toggle menu or **Advanced** settings.
- Hold still long enough for **Quick Stroke Hold Time** and avoid moving beyond **Quick Stroke Move Tolerance** before activation.
- After activation, move the pen or mouse and release at the intended final endpoint. The committed stroke should use the release position even if intermediate preview updates were throttled.
- Compare mouse and pen on the same target. If mouse is light but pen is heavy, include tablet driver details and Windows Ink settings in the report.
- Temporarily lower **Live SDF Preview Resolution** or switch away from **Live SDF** to separate preview cost from Quick Stroke cost.
- When reporting, include whether the problem happens in 2D Canvas, 3D Screen mode, 3D Surface mode, or all modes, plus mesh type, texture resolution, paint target mode, Live SDF setting, and whether Monotonic Guard or Symmetry is enabled.

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
