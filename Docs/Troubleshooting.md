# Troubleshooting

## The Plugin Does Not Appear In The Editor

- Confirm the project is a C++ project and was rebuilt after copying the plugin.
- Confirm the plugin folder is `YourProject/Plugins/QuickSDFTool/`.
- Confirm `QuickSDFTool.uplugin` is directly inside that folder.
- Regenerate project files and rebuild from Visual Studio or your normal UE build workflow.

## The Generated Shadow Looks Inverted

- First verify the comparison direction in your project material and check whether the result matches the painted masks.
- Confirm whether you are using a Monopolar or Bipolar output path, and follow the R/A/B/G output layout documented in [Material Setup](./MaterialSetup.md).
- The built-in Generated SDF preview uses the same exported threshold-map layout as the saved texture. If your project material is inverted while the built-in preview is correct, the project shader comparison direction or light-angle mapping is the likely mismatch.

## Brush Size Feels Wrong On Some Meshes

Brush size can appear inconsistent when UV density varies strongly across the mesh. For now:

- Test on clean, evenly distributed UVs first.
- Use the 2D UV preview for precise edits.
- Avoid judging brush behavior on heavily stretched UV islands until the UV brush-size item is addressed.

## UE 5.4 / 5.5 / 5.6 Build Issues

QuickSDFTool v1.0 supports UE 5.7.x, with UE 5.7.4 as the required release verification target. Earlier versions may need source edits around editor tooling, modeling components, material baking, or shader module setup. UE 5.8+ is intended to be supported, but is not part of the v1.0 release verification matrix.

When reporting compatibility issues, include:

- Unreal Engine version.
- Compiler version.
- Full build error.
- QuickSDFTool commit or release tag.
