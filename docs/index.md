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
</div>

## Quick Start

1. Copy this repository into your C++ Unreal project as `Plugins/QuickSDFTool/`.
2. Regenerate project files, build the project, enable **QuickSDFTool**, then restart the editor.
3. Open the Editor Mode selector and choose **Quick SDF**.
4. Select a mesh in the level.
5. In **Material Slots**, click the row you want to edit. Use the row Bake action if the slot still needs a baked source mask.
6. Paint white with `LMB`; paint black/shadow with `Shift + LMB`.
7. Use the timeline to seek light angle, add or duplicate keyframes, and choose a paint target mode.
8. Click **Generate Selected SDF** or **Generate SDF Threshold Map**.
9. Use the generated texture from `/Game/QuickSDF_GENERATED/` in your toon material.

## Compatibility

QuickSDFTool v1.0 targets Unreal Engine 5.7.x, with UE 5.7.4 as the required release verification target. UE 5.8+ is intended to be supported later, but it is not part of the v1.0 verification matrix.

## Core Workflow

```text
painted light/shadow masks -> SDF interpolation -> RGBA threshold texture -> controlled toon shadow
```

QuickSDFTool is most useful when the right shadow shape is an art-direction decision rather than a physically derived result. Common uses include face shadows, hair bands, clothing folds, and small-team toon pipelines that benefit from in-editor iteration.
