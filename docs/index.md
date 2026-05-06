---
title: QuickSDFTool Documentation
description: Learn how to install, use, and integrate QuickSDFTool for Unreal Engine toon-shadow mask authoring.
lang: en
alternate_url: /ja/
alternate_label: 日本語
---

# QuickSDFTool Documentation

QuickSDFTool is an Unreal Engine 5.7.x editor-mode plugin for painting toon-shadow masks on meshes and generating SDF threshold maps for stylized rendering.

<p class="lead">Use this site to install the plugin, understand the authoring workflow, wire generated threshold maps into materials, and check release notes for compatibility details.</p>

<div class="hero-actions">
  <a class="button" href="#quick-start">Quick Start</a>
  <a class="button secondary" href="https://github.com/yeczrtu/QuickSDFTool">View Repository</a>
  <a class="button secondary" href="https://github.com/user-attachments/assets/1eb770b6-b65d-44bb-b5a0-fbb78d998202">Watch Demo</a>
</div>

## Start Here

<div class="doc-grid">
  <a class="doc-card" href="{{ '/workflow/' | relative_url }}">
    <strong>Authoring Workflow</strong>
    <span>Learn Select, Paint, Material Slots, timeline, Live SDF preview, symmetry, Guard, import/export, and SDF generation.</span>
  </a>
  <a class="doc-card" href="{{ '/material-setup/' | relative_url }}">
    <strong>Material Setup</strong>
    <span>Connect generated threshold maps to toon and cel-shading materials.</span>
  </a>
  <a class="doc-card" href="{{ '/troubleshooting/' | relative_url }}">
    <strong>Troubleshooting</strong>
    <span>Fix common install, build, material, and brush behavior issues.</span>
  </a>
  <a class="doc-card" href="{{ '/release-notes/' | relative_url }}">
    <strong>Release Notes</strong>
    <span>Review tested Unreal Engine versions, upgrade notes, and release assets.</span>
  </a>
  <a class="doc-card" href="{{ '/roadmap/' | relative_url }}">
    <strong>Roadmap</strong>
    <span>Review post-1.0 priorities and planned feature requirements.</span>
  </a>
  <a class="doc-card" href="{{ '/development/' | relative_url }}">
    <strong>Development Notes</strong>
    <span>Inspect architecture, verification, and repository maintenance notes.</span>
  </a>
</div>

## Workflow Screenshots

The current documentation captures show the main v1.0 workflow from slot selection to SDF output.

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

## Quick Start

1. Copy this repository into your C++ Unreal project as `Plugins/QuickSDFTool/`.
2. Regenerate project files, build the project, enable **QuickSDFTool**, then restart the editor.
3. Open the Editor Mode selector and choose **Quick SDF**.
4. In Select mode, click the mesh/material surface you want to edit in the viewport.
5. Confirm the active slot in **Material Slots**. The selected row and cyan viewport overlay show the active material slot; row clicks can correct the viewport pick.
6. Click **Start Paint**. Paint mode isolates the active slot by default; turn off **Isolate Slot** if you need full-mesh visibility.
7. Paint white with `LMB`; paint black/shadow with `Shift + LMB`.
8. Use **Live SDF** material preview when you want a fast GPU JFA approximation before generating the final texture.
9. Use the timeline to seek light angle, add or duplicate keyframes, and choose a paint target mode.
10. Click **Generate Selected SDF** or **Generate SDF Threshold Map**.
11. Use the generated texture from `/Game/QuickSDF_GENERATED/` in your toon material.

## Compatibility

QuickSDFTool v1.0 targets Unreal Engine 5.7.x, with UE 5.7.4 as the required release verification target. UE 5.8+ is intended to be supported later, but it is not part of the v1.0 verification matrix.

## Core Workflow

```text
painted light/shadow masks -> SDF interpolation -> RGBA threshold texture -> controlled toon shadow
```

QuickSDFTool is most useful when the right shadow shape is an art-direction decision rather than a physically derived result. Common uses include face shadows, hair bands, clothing folds, and small-team toon pipelines that benefit from in-editor iteration.
