# GitHub Repository Setup

These items cannot be fully applied from source files alone, but they should be configured before promoting the repository publicly.

## Topics

Add:

```text
unreal-engine
ue5
toon-shading
cel-shading
sdf
editor-plugin
technical-art
```

## Social Preview

Use `.github/assets/social-preview.svg` as the source artwork. If GitHub rejects SVG upload for the repository social preview, export it to PNG at 1280x640 and upload the PNG.

## Releases

Create a tag for each public release and use the matching file under `Docs/ReleaseNotes/` as the release body. For v1.0.0, attach `QuickSDFTool-v1.0.0-Win64.zip` and rely on GitHub's generated source archive for source distribution. Attach a short demo video/GIF if available.
