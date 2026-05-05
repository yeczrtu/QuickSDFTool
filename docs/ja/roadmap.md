---
title: Roadmap
description: QuickSDFTool 1.0 以降のロードマップと計画中機能要件。
permalink: /ja/roadmap/
lang: ja
alternate_url: /roadmap/
alternate_label: English
---

# Roadmap

> [!IMPORTANT]
> ロードマップは、安定版を使うアーティストにとっての信頼性、互換性、初回成功率を高める優先度で並べています。

## P0: 1.0 フォローアップの安定化

- [x] 現在の CPU 経路における SDF 出力チャンネルレイアウトと UV アイランド反転挙動をドキュメント化する。
- [x] リリースノートと導入確認手順を含む安定版 1.0 リリースを公開する。
- [ ] UV 依存のブラシサイズ差を改善する。
- [ ] マスクペイント -> SDF テクスチャ -> トゥーンシェーダー結果の短い動画を追加する。

## P1: パフォーマンスと互換性

- [ ] GPU JFA SDF 経路をユーザー向け生成フローで有効化する。
- [ ] 1K、2K、4K のマスクワークフローをベンチマークする。
- [ ] UE 5.8+ 互換性を検証し、テスト済みエンジンバージョンを追加するタイミングでリリースノートを更新する。

## P2: ペイント体験の強化

- [ ] カスタムブラシアルファテクスチャのインポート。
- [ ] より豊富なブラシプリセットと、任意のカスタムフォールオフ制御。
- [ ] キーボード操作だけでは足りない場合に備えた、明示的な前 / 次タイムラインボタン。
- [ ] 未保存マスク変更の autosave / hot-reload recovery。

## 計画中の機能要件

> [!NOTE]
> ここは将来作業の要件です。v1.0 安定版には含まれておらず、将来リリースで明示されない限り、現在の C++ API、`UQuickSDFAsset` 形式、Slate UI、ショートカット、アセット形式は変更しません。

### Quick Nose

- `Quick Nose` は、アーティストが指定した鼻位置から鼻影プリセットを素早く配置するための非破壊ベクターレイヤーとして追加します。
- プリセットは位置、回転、スケール、カーブ形状、制御点を編集できるようにし、最終形ではなく高速な出発点として使えるようにします。
- Bake は現在 mask または複数 mask 範囲に対応し、Undo 可能で、あとから編集できるよう元の vector layer を保持します。

### Quick Reshape

- `Quick Reshape` は仮称で、より高レベルな境界線作成ワークフローとして扱います。
- 1 つの非破壊 UV canvas guide layer に複数の `Boundary Line` curve を描き、それぞれに `Assigned Angle` を割り当てます。
- `Bake Matching Angles` は、境界線が割り当てられている角度 mask だけを生成または更新し、全 timeline mask は更新しません。
- 境界線は編集可能な vector data として保存し、Bake 後も位置や curve shape を調整して再 Bake できるようにします。
- 白 / 黒の塗り側は、角度と線方向から推定する `Auto Side` を標準にし、必要に応じて `Invert Side` で線ごとに補正します。
- 有効な境界線は active UV island を分割するか閉じた領域を作る必要があります。曖昧な部分線は Bake 前に警告し、塗りは active UV island 内に制限します。
- `Quick Reshape` は `Stroke Auto Fill` と分けて扱います。`Stroke Auto Fill` は単一線の fill helper、`Quick Reshape` は複数角度の boundary plan から mask を作る機能です。

### Actor Mesh Component Selection

- 1 つの actor が複数の mesh component を持つ場合に、QuickSDFTool の編集対象 mesh を明確に選べない現在の制限を解消します。
- 選択中 actor 内の対象候補として、`StaticMeshComponent` と `SkeletalMeshComponent` を component 単位で選べる target picker を追加します。
- Painting、mask import/export、Bake、SDF generation、material-slot isolation は、actor 内の全 mesh ではなく選択された mesh component だけに適用します。
- Mesh component を切り替えても、それぞれの QuickSDF asset / mask state を保持し、顔、髪、服、accessory を 1 つの actor 内で個別に編集できるようにします。

### Threshold Map Reverse Conversion

- 完成済み threshold map に対して light angle を指定し、その角度の mask を復元または preview する reverse conversion workflow を追加します。
- Preview、現在 mask への抽出、新規 mask への抽出に対応し、完成済み threshold map の確認や修正に使えるようにします。
- Monopolar / Bipolar threshold map を reverse conversion するとき、指定角度に対してどの channel または value pair を解釈するかを明確にします。

### Mask Freeze

- `Mask Freeze` は、編集中ではない mask の paint 用 Render Target を解放し、VRAM 使用量を抑える workflow です。
- Frozen mask は authored data を asset-backed mask data または CPU / disk-backed texture data として保持し、一時的な `PaintRenderTarget` は必要になるまで破棄できるようにします。
- 現在 mask の freeze、非 active mask の freeze、現在 mask の thaw、全 mask の thaw を用意します。
- 複数 mask edit の対象に frozen mask が含まれる場合は、自動で thaw します。
- Timeline key には freeze / thaw state を示す badge を表示します。
- SDF generation、export、save、overwrite-source workflow では、frozen mask を透過的に thaw するか保存済み data を読み取り、出力から mask が欠落しないようにします。
- Freeze / Thaw をまたいでも Undo / Redo で mask 内容が失われないようにします。

### Stroke Auto Fill

- `Stroke Auto Fill` は、線を引いたあとに左右どちら側、または内側 / 外側のどちらを塗るかを preview して自動塗りつぶしする機能です。
- 現在 mask のみの編集と、`All / Before / After` による一括適用の両方に対応します。
- 意図しない別 island への塗り漏れを避けるため、fill operation は active UV island 単位に制限します。
- 確定前に preview を表示し、確定後の結果は Undo できるようにします。
