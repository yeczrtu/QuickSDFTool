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
- Timeline keyframe rendering is split from the main timeline widget. Timeline range/key status calculations live in `QuickSDFTimelineStatus` so range highlighting, badges, and tooltips can be tested without Slate.
- Mask import validation is handled by a Slate-independent import model so the UI and import rules can evolve independently.
- Developer automation tests cover default angles, angle-name parsing, SDF edge cases, channel packing, UV-island mirror application, asset migration, mask import model validation, `TimelineRangeStatus`, `TimelineKeyStatus`, and Monotonic Guard behavior.

## How It Works

1. **Paint:** for each light angle, paint a binary mask on the mesh or UV preview.
2. **SDF:** convert each mask to a signed distance field.
3. **Interpolate:** find transitions between neighboring masks and derive threshold value `T`.
4. **Composite:** automatically choose Monopolar or Bipolar output and pack values into RGBA channels:
   - **Monopolar:** symmetric shadow behavior, or separate 0-90 / 90-180 values when the selected symmetry mode generates a second half.
   - **Bipolar:** asymmetric shadow enter/exit values are generated in the legacy combined field, then exported with an R/A/B/G swizzle so the final texture keeps the expected shader layout and the B channel remains the 0-90-side value.
   - **UV Island Channel Flip:** starts from the 0-90 combined field, fills the 90-180 channels by island-local mirrored sampling, and exports the result as the same RGBA16F / HDR texture format used by normal 0-180 maps.
5. **Export:** save the final threshold map as a 16-bit half-float texture.

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

## Repository Setup Checklist

For maintainers preparing the GitHub page:

- Add these repository topics: `unreal-engine`, `ue5`, `toon-shading`, `cel-shading`, `sdf`, `editor-plugin`, `technical-art`.
- Upload `.github/assets/social-preview.svg` as the GitHub Social Preview image, or export it to PNG first.
- Create releases using the matching files under [release notes]({{ '/release-notes/' | relative_url }}).
