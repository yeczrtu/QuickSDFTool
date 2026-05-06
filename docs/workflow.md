---
title: Authoring Workflow
description: Detailed QuickSDFTool artist workflow, controls, material slots, timeline, symmetry, and SDF generation notes.
permalink: /workflow/
lang: en
alternate_url: /ja/workflow/
alternate_label: 日本語
---

# Authoring Workflow

QuickSDFTool is designed around one material slot at a time: select the mesh and slot, paint light/shadow masks across angles, then generate a threshold texture for the toon shader.

## Workflow Screenshots

<div class="screenshot-grid">
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-select-active-slot.png' | relative_url }}" alt="Select mode active material slot overlay">
    <figcaption>
      <strong>Select active slot</strong>
      Select mode keeps the full mesh visible and marks the active material slot with the selected row and cyan viewport overlay.
    </figcaption>
  </figure>
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-paint-screen-mode.png' | relative_url }}" alt="Paint mode with Screen projection brush preview">
    <figcaption>
      <strong>Paint in Screen mode</strong>
      Paint mode shows Screen projection, the brush preview, UV texture preview, and active slot context.
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-timeline.png' | relative_url }}" alt="Quick SDF timeline controls and keyframes">
    <figcaption>
      <strong>Timeline</strong>
      Timeline thumbnails, angle labels, and keyframe controls stay visible while seeking or dragging keys.
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-sdf-preview.png' | relative_url }}" alt="Generated SDF threshold texture preview">
    <figcaption>
      <strong>SDF output</strong>
      The generated SDF threshold texture is the map consumed by toon materials.
    </figcaption>
  </figure>
</div>

<p class="media-credit">Screenshot character model credit: <a href="https://booth.pm/ja/items/5007531">真冬 Mafuyu / Original 3D Model</a> by ぷらすわん. Character design and 3D modeling: 有坂みと.</p>

## Controls

| Input | Action |
| --- | --- |
| `LMB Drag` | Paint light/white |
| `Shift + LMB Drag` | Paint shadow/black |
| `Viewport Mesh / Material Click` in Select mode | Select the target mesh component and clicked material slot |
| `Start Paint` | Enter Paint mode using the selected target mesh and material slot |
| `F` in Paint mode | Focus the viewport on the current brush position; falls back to UE selection focus when no brush hit is active |
| `Ctrl + F`, move mouse, click | Resize brush while the mouse is over the viewport |
| `Alt + T` | Open the quick toggle menu |
| `Alt + 1` | Cycle paint target mode |
| `Alt + 2` - `Alt + 8` | Toggle Auto Light, Preview, UV overlay, Onion Skin, Quick Stroke, Symmetry, and Monotonic Guard |
| `Left / Right Arrow` | Select previous / next timeline frame |
| `Material Slot Row Click` | Select or correct the material slot / texture set to edit |
| `Material Slot Bake Icon` | Bake that slot only |
| `Timeline Seek Lane Click / Drag` | Seek the preview light angle without dragging keyframes |
| `Timeline Key Click` | Select angle |
| `Timeline Key Drag` | Adjust angle after the drag threshold is crossed; the seek cursor and preview light follow the drag |
| `Timeline Status Badge Hover` | Inspect angle, texture, edit state, paint target inclusion, Guard state, overwrite state, and warning details |
| `Timeline Add / Duplicate / Delete` | Create, copy, or remove keyframes |
| `Timeline 8 or 15 / Even` | Complete the default mask set or redistribute angles evenly |
| `Drag Texture2D assets onto timeline` | Import edited masks |
| `Ctrl + Z / Ctrl + Y` | Undo / Redo |

## Select And Material Slots

The `Quick SDF > Material Slots` section is optimized for artists editing one material slot at a time.

- Select mode starts non-destructively, keeps the whole mesh visible, and clears stale targets when nothing is selected.
- Clicking a mesh surface in the viewport selects both the target mesh component and the hit material slot.
- Row clicks select or correct the corresponding texture set and active paint/bake target.
- Select mode uses a cyan viewport overlay to show the active slot instead of hiding the rest of the mesh.
- Paint mode starts with **Isolate Slot** enabled by default. Turning it off restores full-mesh visibility while the selected slot remains the paint target.
- Paint picking and stroke sampling use the selected slot as an edit filter, so non-target slots do not steal hits from the active slot.
- Each row shows slot number, slot name, material name, and compact status such as `Selected`, `Baked`, or `Empty`.
- The row action button bakes only that slot, using the active slot as the material bake scope for original-shading masks.
- Visible rows are rebuilt from the current mesh component, so switching targets does not leave stale `Missing` rows in the active UI.

## Painting

- Mesh painting supports Static Mesh and Skeletal Mesh components, including Skeletal Mesh assets without a PhysicsAsset.
- Screen projection is the default paint mode. It is useful for direct face and hair shadow authoring from the current camera.
- Surface and UV-oriented painting remain available for mesh-space and texture-space workflows.
- The 2D UV preview supports texture-space control, optional UV guides, and onion skinning.
- Brush input includes lazy-radius stroke stabilization, fine spacing, antialiased brush masks, and pressure-driven brush radius for tablet workflows.
- `F` focuses the active viewport on the current brush hit. If the brush is not over a valid hit, the key falls back to UE's standard selection focus.

## Timeline And Paint Targets

The timeline separates seeking from keyframe editing to reduce accidental changes.

- The upper seek lane is the only area that seeks the preview angle by click or drag.
- The lower keyframe lane contains thumbnails and key handles. Keyframes select on click and move only after the drag threshold is crossed.
- Dragging a keyframe updates the seek cursor and preview light immediately.
- Paint-target range highlights are shown for `Current`, `All`, `Before`, and `After`. The visible range uses neighboring-angle midpoints so it matches the masks that will be edited.
- Key status badges are hit-test invisible and do not interfere with selecting, dragging, importing, or seeking.
- The `8` / `15` and `Even` controls complete the default mask set or redistribute angles. Symmetry mode completes to 8 masks; non-symmetry mode completes to 15 masks.

## Live SDF Preview

`Live SDF` is a material preview mode for fast shape feedback while painting. It is intentionally separate from the saved `Generated SDF` texture.

- Enable it from **Material Preview** only when you need live feedback. No GPU preview work is generated while another material preview mode is selected.
- The preview uses the GPU Jump Flooding Algorithm path to approximate the threshold map from the editable paint render targets.
- Live preview resolution is selected in **Advanced > Live SDF Preview Resolution** with `128 px`, `256 px`, `512 px`, and `1024 px` presets. The selected value becomes the long edge of the transient preview render target.
- Higher values improve edge stability and small detail readability, but increase GPU cost. `512 px` is the default balance.
- JFA passes are completed inside a single preview update. They are throttled during strokes, but the individual JFA steps are not spread across multiple frames.
- The final saved SDF still uses the CPU generation path, including full-resolution processing, upscaling, UV island mirror output, and final export packing.

## Symmetry Modes

QuickSDFTool supports four SDF generation modes:

- `Auto`: default face-painting mode. The tool analyzes the active mesh, UV channel, and material slot, then chooses Texture Flip or UV Island Channel Flip.
- `Off`: paint the full 0-180 degree sweep normally.
- `Texture Flip`: paint 0-90 degrees and mirror the texture for the 90-180 side.
- `UV Island Channel Flip`: paint 0-90 degrees and generate the 90-180 side per UV island through island-local mirrored sampling.

`UV Island Channel Flip` is intended for assets where left and right UV islands are separate but still mirror each other. It tolerates position, scale, and light shape differences through normalized island-local mapping, but it does not perform nonlinear per-island warping. Ambiguous, unpaired, overlapping, or out-of-range islands fall back to copying the source-side values and produce warnings.

The generated texture remains shader-compatible with the regular 0-180 layout. Internally, the legacy combined field is processed as `R/G/B/A`, then final export uses the R/A/B/G swizzle (`R <- R`, `G <- A`, `B <- B`, `A <- G`) so existing shader expectations and the historical `B` channel behavior are preserved.

## Monotonic Guard

`Monotonic Guard` is an optional paint-time safety check for SDF threshold masks. It treats `R >= 127` as white and lower values as black, then prevents repeated transitions such as `black -> white -> black` or `white -> black -> white` across the processable angle sequence.

- The quick toggle is labeled `Guard`; the shortcut is `Alt + 8`.
- `Clip Direction` defaults to `Auto`: `0-90` degrees uses `White Expands`, and `90-180` degrees uses `White Shrinks`.
- Normal brush strokes and Quick Stroke are clipped silently before the undo transaction is finalized.
- Soft antialiased stroke edges are handled as stroke intent, even if they do not cross the `127` binary threshold.
- `Current / All / Before / After` still decide which masks receive the stroke.
- Imported masks, rebaked masks, and SDF generation are not automatically modified. Use validation or run SDF generation with Guard enabled to get warnings about existing violations.

## Mask I/O And SDF Generation

- Mask import supports file picker import and timeline drag-and-drop.
- Mask export allows external editing and review.
- Work is stored non-destructively in `UQuickSDFAsset`; mask textures can be saved with the asset when needed.
- SDF generation uses CPU processing with automatic Monopolar/Bipolar packing, optional 1x-8x upscaling, R/A/B/G output swizzling, and half-float texture export.
- `Generated SDF` displays the saved final texture. `Live SDF` displays the transient GPU JFA approximation and is not saved as the final output.
- Generated maps are saved under `/Game/QuickSDF_GENERATED/` by default.
- Use [Material Setup]({{ '/material-setup/' | relative_url }}) for shader integration details.
