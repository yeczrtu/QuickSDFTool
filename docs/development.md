---
title: Development Notes
description: QuickSDFTool architecture, development verification, and repository maintenance notes.
permalink: /development/
lang: en
alternate_url: /ja/development/
alternate_label: 日本語
---

# Development Notes

This page collects implementation and maintenance notes that are useful for contributors and release maintainers.

## Architecture

```text
QuickSDFTool/
|-- Content/
|   |-- Materials/        # Preview and toon materials
|   |-- Textures/         # Default textures
|   |-- Widget/           # UMG widget blueprints
|-- Shaders/
|   |-- Private/
|       |-- JumpFloodingCS.usf
|       |-- QuickSDFFastPreview.usf
|-- Source/
    |-- QuickSDFTool/              # Runtime module and UQuickSDFAsset
    |-- QuickSDFToolEditor/        # Editor Mode, paint tool, timeline, processor
    |-- QuickSDFToolShaders/       # Compute shader binding
```

| Module | Type | Key Dependencies |
| --- | --- | --- |
| `QuickSDFTool` | Runtime | `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI` |
| `QuickSDFToolEditor` | Editor | `InteractiveToolsFramework`, `EditorInteractiveToolsFramework`, `GeometryCore`, `DynamicMesh`, `MeshDescription`, `ModelingComponents`, `MeshConversion`, `EditorSubsystem`, `UMG`, `Slate`, `LevelEditor`, `PropertyEditor`, `MaterialBaking`, `DesktopPlatform`, `ImageWrapper`, `AssetRegistry` |
| `QuickSDFToolShaders` | Runtime / `PostConfigInit` | `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI`, `Projects` |

## Editor Code Layout

- `UQuickSDFAsset` uses the active `FQuickSDFTextureSetData` as the primary source for editable masks, resolution, UV channel, and final SDF texture data. Legacy top-level fields are migrated on load on a best-effort basis.
- `UQuickSDFPaintTool` is the Interactive Tools Framework facade for lifecycle, input routing, and UI commands. Paint state, undo changes, mask utilities, SDF helpers, asset selection, and render target support live in focused private helpers.
- Windows pen-display/tablet input is handled inside the editor module without engine changes. `QuickSDFEditorMode` captures pen pointer position, contact, and pressure, while `UQuickSDFPaintTool` and `SQuickSDFPaintCanvas` convert the current absolute pointer position into fresh 3D viewport rays or 2D Canvas coordinates.
- Live SDF preview is isolated in `QuickSDFPaintToolLivePreview.cpp` and renders through `QuickSDFFastPreviewRendering`. It owns only transient render targets and never replaces the saved final SDF texture.
- Timeline keyframe rendering is split from the main timeline widget. Timeline range/key status calculations live in `QuickSDFTimelineStatus` so range highlighting, badges, and tooltips can be tested without Slate.
- Mask import validation is handled by a Slate-independent import model so the UI and import rules can evolve independently.
- Developer automation tests cover default angles, angle-name parsing, SDF edge cases, channel packing, UV-island mirror application, asset migration, mask import model validation, `TimelineRangeStatus`, `TimelineKeyStatus`, and Monotonic Guard behavior.

## How It Works

1. **Paint:** for each light angle, paint a binary mask on the mesh or 2D Canvas.
2. **SDF:** convert each mask to a signed distance field.
3. **Interpolate:** find transitions between neighboring masks and derive threshold value `T`.
4. **Composite:** automatically choose Monopolar or Bipolar output and pack values into RGBA channels:
   - **Monopolar:** symmetric shadow behavior, or separate 0-90 / 90-180 values when the selected symmetry mode generates a second half.
   - **Bipolar:** asymmetric shadow enter/exit values are generated in the legacy combined field, then exported with an R/A/B/G swizzle so the final texture keeps the expected shader layout and the B channel remains the 0-90-side value.
   - **UV Island Channel Flip:** starts from the 0-90 combined field, fills the 90-180 channels by island-local mirrored sampling, and exports the result as the same RGBA16F / HDR texture format used by normal 0-180 maps.
5. **Export:** save the final threshold map as a 16-bit half-float texture.

`Live SDF` is a preview-only branch: paint masks are downsampled to the selected transient preview resolution, GPU JFA produces an approximate threshold map, and the preview material consumes that render target. Final generation remains on the CPU path so saved textures keep the high-quality output behavior.

![Windows pen input flow into QuickSDF]({{ '/images/quick-sdf-pen-input-flow.png' | relative_url }})

## Development Verification

The current development target is UE 5.7.x. For local verification, build a C++ host project with the plugin enabled, then run the `QuickSDFTool` automation test group when test coverage is required.

Useful verification commands in the Unreal Editor command line or Session Frontend:

```text
Automation RunTests QuickSDFTool
Automation RunTests QuickSDFTool.Core.Timeline
Automation RunTests QuickSDFTool.Core
Automation RunTests QuickSDFTool.MonotonicGuard
```

The v1.0 release candidate should be validated against `sdfbuildEditor Win64 Development`, focused timeline automation coverage, `QuickSDFTool.Core`, and the Monotonic Guard tests.

Manual input verification should include mouse painting plus Windows pen-display/tablet hover, pressure, stroke start/drag/release, 2D Canvas window move/resize behavior, and `Ctrl + F` brush resizing.

## Documentation Screenshot Backlog

These screenshots should be captured from a real UE editor session rather than generated diagrams:

- `quick-sdf-2d-canvas-pen-paint.png`: 2D Canvas with the brush circle aligned to the painted stroke.
- `quick-sdf-advanced-pen-pressure.png`: Advanced settings showing **Pen Pressure** and **Pen Pressure Curve**.
- `quick-sdf-pen-screen-hover.png`: Screen mode 3D Paint brush preview while a pen is hovering.
- `quick-sdf-ctrl-f-pen-resize.png`: Optional capture of `Ctrl + F` brush resizing with pen input.

## Repository Setup Checklist

For maintainers preparing the GitHub page:

- Add these repository topics: `unreal-engine`, `ue5`, `toon-shading`, `cel-shading`, `sdf`, `editor-plugin`, `technical-art`.
- Upload `.github/assets/social-preview.svg` as the GitHub Social Preview image, or export it to PNG first.
- Create releases using the matching files under [release notes]({{ '/release-notes/' | relative_url }}).
