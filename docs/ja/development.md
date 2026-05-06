---
title: Development Notes
description: QuickSDFTool の内部構成、開発時検証、リポジトリ保守メモ。
permalink: /ja/development/
lang: ja
alternate_url: /development/
alternate_label: English
---

# Development Notes

このページは、コントリビューターとリリースメンテナー向けの実装・保守メモです。

## 内部構成

```text
QuickSDFTool/
|-- Content/
|   |-- Materials/        # プレビュー / トゥーン用マテリアル
|   |-- Textures/         # デフォルトテクスチャ
|   |-- Widget/           # UMG ウィジェットブループリント
|-- Shaders/
|   |-- Private/
|       |-- JumpFloodingCS.usf
|       |-- QuickSDFFastPreview.usf
|-- Source/
    |-- QuickSDFTool/              # Runtime module と UQuickSDFAsset
    |-- QuickSDFToolEditor/        # Editor Mode、paint tool、timeline、processor
    |-- QuickSDFToolShaders/       # Compute shader binding
```

| Module | Type | Key Dependencies |
| --- | --- | --- |
| `QuickSDFTool` | Runtime | `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI` |
| `QuickSDFToolEditor` | Editor | `InteractiveToolsFramework`, `EditorInteractiveToolsFramework`, `GeometryCore`, `DynamicMesh`, `MeshDescription`, `ModelingComponents`, `MeshConversion`, `EditorSubsystem`, `UMG`, `Slate`, `LevelEditor`, `PropertyEditor`, `MaterialBaking`, `DesktopPlatform`, `ImageWrapper`, `AssetRegistry` |
| `QuickSDFToolShaders` | Runtime / `PostConfigInit` | `Core`, `CoreUObject`, `Engine`, `RenderCore`, `RHI`, `Projects` |

## エディターコード構成

- `UQuickSDFAsset` は、編集中の mask、resolution、UV channel、final SDF texture の主データ源として active `FQuickSDFTextureSetData` を使います。旧 top-level fields は保存済み asset 互換のため load 時に best-effort で移行します。
- `UQuickSDFPaintTool` は Interactive Tools Framework の lifecycle、input routing、UI commands を担当する facade です。Paint state、Undo changes、mask utilities、SDF helpers、asset selection、render target support は責務別 private helper に分かれています。
- Live SDF preview は `QuickSDFPaintToolLivePreview.cpp` に分離され、`QuickSDFFastPreviewRendering` 経由で描画します。transient render target だけを持ち、保存済み final SDF texture は置き換えません。
- Timeline keyframe rendering は main timeline widget から分離されています。range / key status calculation は `QuickSDFTimelineStatus` にあり、range highlight、badge、tooltip を Slate なしで test できます。
- Mask import validation は Slate に依存しない import model 側で扱うため、UI と取り込み rule を個別に保守できます。
- 開発用 Automation Test では、default angles、angle-name parsing、SDF edge cases、channel packing、UV-island mirror application、asset migration、mask import model validation、`TimelineRangeStatus`、`TimelineKeyStatus`、Monotonic Guard を確認します。

## 仕組み

1. **Paint:** light angle ごとに、mesh または UV preview へ binary mask を描きます。
2. **SDF:** 各 mask を signed distance field へ変換します。
3. **Interpolate:** 隣接 mask 間の transition を探し、threshold value `T` を求めます。
4. **Composite:** Monopolar / Bipolar output を自動判定し、値を RGBA channel へ格納します。
   - **Monopolar:** 対称的な影向け。選択中の symmetry mode が後半側を生成する場合は、0-90 / 90-180 の値を分けて保持します。
   - **Bipolar:** 非対称な影向け。内部の combined field で生成した値を、出力時に R/A/B/G へ並べ替えます。
   - **UV Island Channel Flip:** 0-90 側の combined field から開始し、90-180 側を UV island 単位の mirrored sampling で埋め、通常の 0-180 map と同じ RGBA16F / HDR texture として出力します。
5. **Export:** final threshold map を 16-bit half-float texture として保存します。

`Live SDF` は preview-only branch です。paint mask を選択中の transient preview resolution へ downsample し、GPU JFA で近似 threshold map を作り、preview material がその render target を参照します。保存用の final generation は CPU 経路のままです。

## 開発時の検証

現在の開発対象は UE 5.7.x です。ローカル検証では、プラグインを有効化した C++ host project を build し、必要に応じて `QuickSDFTool` の Automation Test group を実行します。

Unreal Editor の command line または Session Frontend で使う主な検証コマンド:

```text
Automation RunTests QuickSDFTool
Automation RunTests QuickSDFTool.Core.Timeline
Automation RunTests QuickSDFTool.Core
Automation RunTests QuickSDFTool.MonotonicGuard
```

v1.0 release candidate は `sdfbuildEditor Win64 Development`、timeline 周辺の重点 Automation Test、`QuickSDFTool.Core`、Monotonic Guard test で検証します。

## GitHub 公開チェックリスト

メンテナー向け:

- Topics に `unreal-engine`, `ue5`, `toon-shading`, `cel-shading`, `sdf`, `editor-plugin`, `technical-art` を追加する。
- `.github/assets/social-preview.svg` を GitHub Social Preview に設定するか、PNG に書き出して設定する。
- [release notes]({{ '/release-notes/' | relative_url }}) にある対応する release notes を使って release を作成する。
