---
title: Roadmap
description: Post-1.0 QuickSDFTool roadmap and planned feature requirements.
permalink: /roadmap/
lang: en
alternate_url: /ja/roadmap/
alternate_label: 日本語
---

# Roadmap

> [!IMPORTANT]
> The roadmap is ordered by what most improves trust, compatibility, and first-run success for artists using the stable plugin.

## P0: Stabilize 1.0 Follow-Up

- [x] Document the final SDF output channel layout and island-mirror behavior for the current CPU path.
- [x] Publish a stable 1.0 release with release notes and install verification steps.
- [ ] Improve the UV-dependent brush-size mismatch.
- [ ] Add a short end-to-end video showing mask paint -> SDF texture -> toon shader result.

## P1: Improve Performance and Compatibility

- [ ] Enable the GPU JFA SDF path in the user-facing generation flow.
- [ ] Benchmark 1K, 2K, and 4K mask workflows.
- [ ] Verify UE 5.8+ compatibility and update release notes when a tested engine version is added.

## P2: Deepen Painting Workflow

- [ ] Import custom brush alpha textures.
- [ ] Add richer brush presets and optional custom brush falloff controls.
- [ ] Add explicit previous/next timeline toolbar buttons if keyboard navigation is not enough for artists.
- [ ] Add autosave/hot-reload recovery for unsaved mask changes.

## Planned Feature Requirements

> [!NOTE]
> These are roadmap requirements for future work. They are not included in the v1.0 stable release and do not change the current C++ API, `UQuickSDFAsset` format, Slate UI, shortcuts, or asset formats unless a future release explicitly says so.

### Quick Nose

- Add `Quick Nose` as a non-destructive vector layer for quickly placing a nose-shadow preset from a single artist-picked nose position.
- Presets should be editable through position, rotation, scale, curve shape, and control points, so the result is a fast starting point rather than a locked final shape.
- Baking should support the current mask or a multi-mask range, be undoable, and preserve the original vector layer for later edits.

### Quick Reshape

- Treat `Quick Reshape` as a tentative name for a higher-level boundary authoring workflow. Artists draw multiple `Boundary Line` curves on one non-destructive UV-canvas guide layer, then assign each curve to a timeline angle with `Assigned Angle`.
- Each boundary line represents the light/shadow split for its assigned mask. `Bake Matching Angles` should generate or update only the masks whose angles are assigned to boundary lines, not every timeline mask.
- Store boundary lines as editable vector data so their position and curve shape can be refined after baking and baked again later.
- Choose the white/black fill side with `Auto Side` by default, inferred from the angle and line direction, and allow per-line correction with `Invert Side`.
- A valid boundary line should either split the active UV island or form a closed region. Ambiguous partial lines should warn before baking, and fills should stay constrained to the active UV island.
- Keep `Quick Reshape` separate from `Stroke Auto Fill`: `Stroke Auto Fill` is a single-line fill helper, while `Quick Reshape` creates masks from a multi-angle boundary plan.
- Allow `Monotonic Guard` to validate Quick Reshape output during or after baking so repeated transitions can be caught.

### Threshold Map Reverse Conversion

- Add a reverse-conversion workflow that can reconstruct or preview an angle-specific mask from a completed threshold map by entering a target light angle.
- Support at least preview, extraction to the current mask, and extraction to a new mask so artists can inspect or repair completed threshold maps.
- Clarify how Monopolar and Bipolar threshold maps are interpreted during reverse conversion.

### Mask Freeze

- Add a `Mask Freeze` workflow to reduce VRAM usage by releasing paint render targets for masks that are not actively being edited.
- Frozen masks should keep authored data as asset-backed mask data or CPU/disk-backed saved texture data, while transient `PaintRenderTarget` data can be discarded until needed.
- Provide actions for freezing the current mask, freezing all inactive masks, thawing the current mask, and thawing all masks.
- Automatically thaw any frozen mask that becomes part of a multi-mask edit.
- Timeline keys should show frozen/unfrozen state with a badge.
- SDF generation, export, save, and overwrite-source workflows must transparently thaw or read frozen masks so output does not silently omit frozen data.
- Undo/Redo should not lose mask data across freeze/thaw operations.

### Stroke Auto Fill

- Add `Stroke Auto Fill` so a drawn line can preview and fill the chosen left/right side or inside/outside region.
- Support both current-mask edits and bulk application through `All / Before / After`.
- Limit fill operations to the active UV island to avoid accidental fills across unrelated islands.
- Show a preview before committing the fill, and make the committed result undoable.
