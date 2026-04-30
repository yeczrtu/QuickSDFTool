# Troubleshooting

## The Plugin Does Not Appear In The Editor

- Confirm the project is a C++ project and was rebuilt after copying the plugin.
- Confirm the plugin folder is `YourProject/Plugins/QuickSDFTool/`.
- Confirm `QuickSDFTool.uplugin` is directly inside that folder.
- Regenerate project files and rebuild from Visual Studio or your normal UE build workflow.

## The Generated Shadow Looks Inverted

- First flip the comparison direction in the material and check whether the result matches the painted masks.
- Confirm whether you are using a Monopolar or Bipolar output path.
- This is a known preview-release risk: final SDF output direction still needs verification against the preview material.

## Brush Size Feels Wrong On Some Meshes

Brush size can appear inconsistent when UV density varies strongly across the mesh. For now:

- Test on clean, evenly distributed UVs first.
- Use the 2D UV preview for precise edits.
- Avoid judging brush behavior on heavily stretched UV islands until the UV brush-size item is addressed.

## Timeline Thumbnails Shift After Clicking

This is currently a known defect. If it blocks editing:

- Save the current `UQuickSDFAsset`.
- Switch out of the mode and back into **Quick SDF** mode.
- Reopen or reselect the target mesh.

## UE 5.4 / 5.5 / 5.6 Build Issues

Only UE 5.7.4 is currently verified. Earlier versions may need source edits around editor tooling, modeling components, material baking, or shader module setup.

When reporting compatibility issues, include:

- Unreal Engine version.
- Compiler version.
- Full build error.
- QuickSDFTool commit or release tag.
