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

## GitHub Pages

Enable the documentation site from the repository settings:

1. Open **Settings > Pages**.
2. Set **Source** to **Deploy from a branch**.
3. Set **Branch** to `main` and **Folder** to `/docs`.
4. Save the setting and wait for GitHub Pages to publish `https://yeczrtu.github.io/QuickSDFTool/`.

## Releases

Create a tag for each public release and use the matching file under `docs/release-notes/` as the release body. For v1.0.0, attach `QuickSDFTool-v1.0.0-Win64.zip` and rely on GitHub's generated source archive for source distribution. Attach a short demo video/GIF if available.
