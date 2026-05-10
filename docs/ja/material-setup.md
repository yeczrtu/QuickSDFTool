---
title: Material Setup
description: QuickSDFTool が生成する threshold map の material integration notes。
permalink: /ja/material-setup/
lang: ja
alternate_url: /material-setup/
alternate_label: English
---

# Material Setup

QuickSDFTool は、stylized light-to-shadow transition を制御する threshold texture を出力します。shader graph は project ごとに異なりますが、基本は安定しています。light angle value と generated texture value を比較し、その結果で lit / shadow color を選びます。

## Basic Shader Concept

```text
light direction / artist control value
    compared with
SDF threshold map sample
    produces
toon shadow mask
```

最初の reference material として、同梱の `Content/Materials/M_SDFToon.uasset` を使ってください。これは最終的な production shader を固定するものではなく、期待される data flow を示すための material です。

## Texture Expectations

- generated maps は既定で `/Game/QuickSDF_GENERATED/` に保存されます。
- output は precision を重視した data texture です。compression と color-space settings は data texture 向けにしてください。
- albedo / color texture ではなく threshold data として扱います。
- shadow が反転して見える場合は、まず material comparison direction と light-angle value を確認してください。built-in Generated SDF preview は saved texture と同じ exported threshold-map layout を使います。

## Original Shading Bake Parameters

`M_OriginalShading` は original-shading bake 中に次の dynamic parameters を受け取ります。

- `Angle`: authored light angle in degrees。
- `BakeForwardAngleOffset`: forward-axis offset in degrees。Static Mesh は `0`、Skeletal Mesh は visual forward axis を component local +Y として扱うため `90` です。

actor rotation に依存しない output にするには、`Angle + BakeForwardAngleOffset` から bake direction を作り、material baking space の normalized `PixelNormalWS.rg` と比較します。QuickSDF はこの bake で primitive transform を neutralize するため、`M_OriginalShading` 内で `PixelNormalWS` を world から local へ変換しないでください。

## Mesh Preview Material Parameters

QuickSDF は 2 つの mesh preview material interface を読み込みます。

- Opaque replacement: `/QuickSDFTool/Materials/M_PreviewMat.M_PreviewMat`
- SceneColor multiply overlay: `/QuickSDFTool/Materials/M_PreviewSceneColorOverlay.M_PreviewSceneColorOverlay`

opaque material は replacement preview と左上の 2D texture preview に使われます。overlay material は original-material overlay preview にだけ使われます。Blend mode は C++ から動的に変更しないため、`M_PreviewSceneColorOverlay` は Blend Mode が Translucent の実 material asset である必要があります。opaque `M_PreviewMat` の material instance にしないでください。opaque parent では `SceneColor` を使えません。

Preview modes:

- `Original + Painted`: original material slots を復元し、`UMeshComponent::SetOverlayMaterial()` で translucent preview を適用します。
- `Painted Texture`: opaque preview を replacement material として適用します。
- `Painted + UV`: opaque preview を replacement material として適用します。
- `Painted + Shadow`: opaque preview を replacement material として適用します。

default mesh preview mode は `Original + Painted` です。

両方の dynamic material instance は次の parameters を受け取ります。

- Texture `BaseColor`: active mask render target。
- Scalar `PreviewMode`: `0 = Painted Texture`, `1 = Painted + UV`, `2 = Painted + Shadow`。
- Scalar `UVChannel`: active UV channel index。
- Scalar `Angle`: active timeline angle in degrees。`M_OriginalShading` と一致します。
- Scalar `BakeForwardAngleOffset`: forward-axis offset in degrees。`M_OriginalShading` と一致します。

`Painted + Shadow` では、mesh preview と baked mask が一致するよう、`M_OriginalShading` と同じ `Angle + BakeForwardAngleOffset` direction calculation を使ってください。Translucent にする必要があるのは `M_PreviewSceneColorOverlay` だけです。`M_PreviewMat` は opaque のままにし、`SceneColor` node を含めないでください。

`Original + Painted` では、`M_PreviewSceneColorOverlay` を `SceneColor` と `BaseColor` の black/white painted mask を multiply する形で実装します。C++ はこの overlay path に常に `PreviewMode = 0` を送ります。

左上の 2D texture preview は別の `M_PreviewMat` dynamic instance を使い、常に `PreviewMode = 0` を強制します。そのため selected mesh preview mode に関係なく black/white painted texture preview のままです。

## Monopolar vs Bipolar

- **Monopolar** は単純な case です。symmetric output では RGB に同じ threshold を使えます。0-90 / 90-180 の separate output では R/A を使います。
- **Bipolar** は shadow enter/exit behavior を別々に保存します。light rotation に対して shadow の入り方と抜け方が非対称な形状に有効です。値は legacy R/G/B/A field で生成され、R/A/B/G swizzle で export されます。これにより B は 0-90 exit value として保持されます。

## LilToon / General Toon Shader Notes

QuickSDFTool は特定の toon shader を要求しません。LilToon inspired または同様の pipeline では、generated map を authored control texture として使います。

- painting 時に使った同じ UV channel で threshold map を sample します。
- current light angle または directional term を material が期待する normalized range に変換します。
- light value と threshold value を比較します。
- 比較結果で lit / shadow ramps を blend します。

最初の integration は単純にしてください。direction と value range が合っていることを確認してから、project-specific ramps、softening、color grading を追加します。
