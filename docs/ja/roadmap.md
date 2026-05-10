---
title: Roadmap
description: QuickSDFTool 1.0 以降のロードマップと計画中の機能要件。
permalink: /ja/roadmap/
lang: ja
alternate_url: /roadmap/
alternate_label: English
---

# Roadmap

> [!IMPORTANT]
> この roadmap は、stable plugin を使う artist にとっての信頼性、互換性、初回導入成功率を最も改善する順に並べています。

## P0: Stabilize 1.0 Follow-Up

- [x] 現在の CPU path における final SDF output channel layout と island-mirror behavior を document する。
- [x] release notes と install verification steps を含む stable 1.0 release を公開する。
- [x] Windows pen-display/tablet hover、pressure radius、2D Canvas input、Quick Stroke preview、pen-based `Ctrl + F` brush resizing を安定化する。
- [x] README、docs、GitHub Pages pages、generated concept diagrams、screenshot requirements を更新する。
- [ ] UV density に依存する brush-size mismatch を改善する。
- [ ] mask paint -> SDF texture -> toon shader result を示す短い end-to-end video を追加する。

## P1: Improve Performance and Compatibility

- [x] `Live SDF` material feedback 向けに preview-only GPU JFA path を有効化する。
- [ ] CPU output quality と packing behavior に一致できるようになった後、GPU JFA を final saved generation に対応させるべきか評価する。
- [ ] 1K、2K、4K mask workflow を benchmark する。
- [ ] UE 5.8+ compatibility を検証し、tested engine version が追加された時点で release notes を更新する。

## P2: Deepen Painting Workflow

- [ ] custom brush alpha textures を import できるようにする。
- [ ] brush presets と optional custom brush falloff controls を増やす。
- [ ] keyboard navigation だけでは artist に足りない場合、explicit previous/next timeline toolbar buttons を追加する。
- [ ] unsaved mask changes 向けの autosave / hot-reload recovery を追加する。

## Planned Feature Requirements

> [!NOTE]
> ここにある内容は将来作業の roadmap requirements です。v1.0 stable release には含まれず、future release が明記しない限り current C++ API、`UQuickSDFAsset` format、Slate UI、shortcuts、asset formats を変更しません。

### Quick Nose

- `Quick Nose` を non-destructive vector layer として追加し、artist が選んだ nose position から nose-shadow preset を素早く置けるようにする。
- preset は position、rotation、scale、curve shape、control points で編集できるようにし、固定された完成形ではなく速い starting point として扱う。
- baking は current mask または multi-mask range に対応し、undoable で、後から編集できるよう original vector layer を保持する。

### Quick Reshape

- `Quick Reshape` は higher-level boundary authoring workflow の仮称として扱う。artist は 1 つの non-destructive UV-canvas guide layer に複数の `Boundary Line` curve を描き、それぞれを `Assigned Angle` で timeline angle に割り当てる。
- 各 boundary line は assigned mask の light/shadow split を表す。`Bake Matching Angles` は boundary line が割り当てられた mask だけを generate / update し、すべての timeline mask は変更しない。
- boundary lines は editable vector data として保存し、bake 後も位置と curve shape を修正して再 bake できるようにする。
- fill side は `Auto Side` を既定にし、angle と line direction から white/black side を推定する。必要に応じて per-line の `Invert Side` で補正できるようにする。
- valid boundary line は active UV island を分割するか closed region を形成する必要がある。曖昧な partial line は bake 前に警告し、fill は active UV island に制限する。
- `Quick Reshape` と `Stroke Auto Fill` は分ける。`Stroke Auto Fill` は single-line fill helper、`Quick Reshape` は multi-angle boundary plan から mask を作成する workflow。
- `Monotonic Guard` が Quick Reshape output を baking 中または baking 後に validate できるようにし、repeated transitions を検出する。

### Threshold Map Reverse Conversion

- completed threshold map から target light angle を入力して angle-specific mask を reconstruct または preview できる reverse-conversion workflow を追加する。
- artist が completed threshold map を inspect / repair できるよう、少なくとも preview、current mask への extraction、新規 mask への extraction に対応する。
- reverse conversion 中に Monopolar と Bipolar threshold maps をどう解釈するか明確にする。

### Mask Freeze

- active editing していない mask の paint render targets を release し、VRAM usage を減らす `Mask Freeze` workflow を追加する。
- frozen masks は asset-backed mask data または CPU/disk-backed saved texture data として authored data を保持し、transient `PaintRenderTarget` data は必要になるまで破棄できるようにする。
- freeze current mask、freeze all inactive masks、thaw current mask、thaw all masks の action を用意する。
- multi-mask edit の対象になった frozen mask は自動 thaw する。
- Timeline keys は frozen/unfrozen state を badge で表示する。
- SDF generation、export、save、overwrite-source workflows は frozen masks を透過的に thaw または read し、output が silent に欠落しないようにする。
- Undo/Redo は freeze/thaw operations をまたいで mask data を失ってはいけない。

### Stroke Auto Fill

- `Stroke Auto Fill` を追加し、drawn line から chosen left/right side または inside/outside region を preview / fill できるようにする。
- current-mask edits と `All / Before / After` による bulk application の両方に対応する。
- unrelated islands への accidental fill を避けるため、fill operations は active UV island に制限する。
- commit 前に preview を表示し、committed result は undoable にする。
