<h1 align="center">QuickSDFTool</h1>

<p align="center">
  Unreal Engine 5 Editor Mode for painting toon-shadow masks and generating SDF threshold maps.
  <br>
  <a href="#demo">Demo</a> | <a href="#quick-start">Quick Start</a> | <a href="#documentation">Documentation</a> | <a href="./README_JP.md">日本語</a>
</p>

> [!NOTE]
> **Status: Stable 1.0 for UE 5.7.x.** QuickSDFTool is ready for production evaluation on Unreal Engine 5.7.x. UE 5.8+ is intended to be supported, but has not been release-tested for this version.

## Demo

QuickSDFTool lets artists paint binary light/shadow masks on a mesh at multiple light angles, then composites those masks into a high-precision SDF threshold texture for toon and cel shading.

https://github.com/user-attachments/assets/7eec2890-be31-4cbc-9662-756b6e84c620

| Select active slot | Paint in Screen mode |
| --- | --- |
| ![Select mode active material slot overlay](docs/images/quick-sdf-select-active-slot.png) | ![Paint mode with Screen projection brush preview](docs/images/quick-sdf-paint-screen-mode.png) |

Screenshot character model credit: [真冬 Mafuyu / Original 3D Model](https://booth.pm/ja/items/5007531) by ぷらすわん. Character design and 3D modeling: 有坂みと.

## Features

- Dedicated UE5 Editor Mode named `Quick SDF`.
- Select/prep workflow that keeps the full mesh visible and supports viewport picking for both mesh and material slot.
- Direct painting on Static Mesh and Skeletal Mesh components, including PhysicsAsset-less Skeletal Mesh targets.
- Screen, Surface, and UV-oriented paint workflows with brush preview, pressure-sensitive radius, lazy-radius smoothing, and `F` focus on the active brush position.
- Compact `Material Slots` list with row selection, cyan active-slot overlay in Select mode, per-slot Bake, and paint-time slot isolation.
- Angle timeline with seek/keyframe lanes, thumbnails, snapping, keyframe drag/seek synchronization, and paint-target range highlights.
- Symmetry workflows for `Auto`, `Texture Flip`, `UV Island Channel Flip`, and full 0-180 painting.
- Monotonic Guard validation and clipping for stable threshold-map transitions.
- Mask import/export, non-destructive `UQuickSDFAsset` storage, UE undo/redo, and CPU SDF generation with half-float texture output.

## Quick Start

1. Copy this repository into your C++ Unreal project as `Plugins/QuickSDFTool/`.
2. Regenerate project files, build the project, enable **QuickSDFTool**, then restart the editor.
3. Open the Editor Mode selector and choose **Quick SDF**.
4. In Select mode, click the mesh/material surface you want to edit in the viewport.
5. Confirm the active slot in **Material Slots**. The selected row and cyan viewport overlay show the active material slot.
6. Click **Start Paint**. Paint mode isolates the active slot by default; turn off **Isolate Slot** if you need the full mesh visible.
7. Paint white with `LMB`; paint black/shadow with `Shift + LMB`.
8. Use the timeline to seek light angle, add masks, choose a paint target range, and generate the SDF threshold map.
9. Use the generated texture from `/Game/QuickSDF_GENERATED/` in your toon material.

See [Authoring Workflow](./docs/workflow.md), [Material Setup](./docs/material-setup.md), and [Troubleshooting](./docs/troubleshooting.md) for the full workflow.

## Installation

QuickSDFTool v1.0 requires Unreal Engine 5.7.x and a C++ Unreal project.

```bash
git clone https://github.com/yeczrtu/QuickSDFTool.git
```

Place the plugin here:

```text
YourProject/
|-- Plugins/
    |-- QuickSDFTool/
        |-- QuickSDFTool.uplugin
        |-- Source/
        |-- Shaders/
        |-- Content/
```

Then regenerate project files, build the project, enable **QuickSDFTool**, and restart the editor.

## Compatibility

| Unreal Engine version | Status |
| --- | --- |
| 5.7.4 | Required release verification target |
| 5.7.x | Supported target for v1.0 |
| 5.8+ | Intended to be supported, but not v1.0 release-tested |
| 5.6 and earlier | Not supported |

The v1.0.x binary releases are built with the current custom UE 5.7-based verification editor. They are not advertised as compatible with Epic Games Launcher builds of Unreal Engine. Launcher UE users should rebuild the plugin from source against their exact engine build.

## Documentation

- [Documentation site](./docs/index.md)
- [Authoring Workflow](./docs/workflow.md)
- [Material Setup](./docs/material-setup.md)
- [Troubleshooting](./docs/troubleshooting.md)
- [Release Notes](./docs/release-notes/v1.0.0.md)
- [Roadmap](./docs/roadmap.md)
- [Development Notes](./docs/development.md)

## Contributing

Contributions are welcome. Good first areas are documentation, UE version verification, small workflow fixes, and sample content. Keep changes scoped and include reproduction or verification notes in pull requests.

## Acknowledgments

- [Unreal Engine Interactive Tools Framework](https://docs.unrealengine.com/5.0/en-US/interactive-tools-framework-in-unreal-engine/) - foundation for the editor paint workflow.
- Felzenszwalb & Huttenlocher - *Distance Transforms of Sampled Functions* (2012).
- Jump Flooding Algorithm (JFA) - GPU distance field generation reference.
- [UE5 SDF Face Shadow Mappingでアニメ顔用の影を作ろう](https://unrealengine.hatenablog.com/entry/2024/02/28/222220).
- [SDF TextureとLiltoonでセルルックの影を再現しよう！](https://note.com/ca__mocha/n/n9289fbbc4c8b).
