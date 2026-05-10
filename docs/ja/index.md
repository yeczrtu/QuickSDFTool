---
title: QuickSDFTool ドキュメント
description: Unreal Engine 向け QuickSDFTool のインストール、使い方、マテリアル連携、リリース情報。
lang: ja
alternate_url: /
alternate_label: English
---

# QuickSDFTool ドキュメント

QuickSDFTool は Unreal Engine 5.7.x でトゥーン影マスクをメッシュ上へペイントし、スタイライズドレンダリング向けの SDF スレッショルドマップを生成するエディターモードプラグインです。

<p class="lead">このサイトでは、導入手順、基本ワークフロー、生成テクスチャのマテリアル接続、互換性、検証済みリリース情報を確認できます。</p>

<div class="hero-actions">
  <a class="button" href="#クイックスタート">クイックスタート</a>
  <a class="button secondary" href="https://github.com/yeczrtu/QuickSDFTool">GitHub リポジトリ</a>
  <a class="button secondary" href="https://github.com/user-attachments/assets/1eb770b6-b65d-44bb-b5a0-fbb78d998202">デモを見る</a>
</div>

## 主なページ

<div class="doc-grid">
  <a class="doc-card" href="{{ '/ja/workflow/' | relative_url }}">
    <strong>Authoring Workflow</strong>
    <span>Select、Paint、2D Canvas、ペン入力、Quick Stroke、Material Slots、Timeline、Live SDF preview、Symmetry、Guard、import/export、SDF generation を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/material-setup/' | relative_url }}">
    <strong>Material Setup</strong>
    <span>生成した SDF スレッショルドマップを toon / cel-shading 用マテリアルへ接続します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/troubleshooting/' | relative_url }}">
    <strong>Troubleshooting</strong>
    <span>インストール、ビルド、マテリアル、ペン入力、2D Canvas、ブラシ挙動の代表的な問題を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/release-notes/' | relative_url }}">
    <strong>Release Notes</strong>
    <span>検証済み Unreal Engine バージョン、アップグレード情報、リリースアセットを確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/roadmap/' | relative_url }}">
    <strong>Roadmap</strong>
    <span>1.0 以降の優先度と計画中の機能要件を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/development/' | relative_url }}">
    <strong>Development Notes</strong>
    <span>内部構成、検証、生成済み概念図、必要な実機スクリーンショット、リポジトリ保守メモを確認します。</span>
  </a>
</div>

## ワークフロースクリーンショット

現在のドキュメント用キャプチャは、slot 選択から SDF 出力までの v1.0 workflow を示します。概念図は authoring pipeline、pen input、pressure curve、symmetry、Monotonic Guard の説明用です。UI の状態を証明する画像は、生成画像ではなく実際の UE editor から撮影したスクリーンショットを使います。

<div class="screenshot-grid">
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-select-active-slot.png' | relative_url }}" alt="Select mode active material slot overlay">
    <figcaption>
      <strong>Select active slot</strong>
      Select mode はメッシュ全体を表示したまま、選択行と cyan viewport overlay で active material slot を示します。
    </figcaption>
  </figure>
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-paint-screen-mode.png' | relative_url }}" alt="Paint mode with Screen projection brush preview">
    <figcaption>
      <strong>Screen mode での Paint</strong>
      Paint mode は Screen projection、ブラシプレビュー、UV texture preview、active slot context を表示します。
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-timeline.png' | relative_url }}" alt="Quick SDF timeline controls and keyframes">
    <figcaption>
      <strong>Timeline</strong>
      Timeline は thumbnails、angle labels、keyframe controls を表示したまま seek や key drag を行えます。
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-sdf-preview.png' | relative_url }}" alt="Generated SDF threshold texture preview">
    <figcaption>
      <strong>SDF output</strong>
      生成された SDF threshold texture が toon material で使うデータです。
    </figcaption>
  </figure>
</div>

<p class="media-credit">スクリーンショット内キャラクターモデル: <a href="https://booth.pm/ja/items/5007531">真冬 Mafuyu / オリジナル3Dモデル</a>（ぷらすわん）。キャラクターデザイン / 3Dモデリング: 有坂みと。</p>

## クイックスタート

1. このリポジトリを C++ Unreal project の `Plugins/QuickSDFTool/` にコピーします。
2. Project files を再生成し、ビルド後に **QuickSDFTool** を有効化して editor を再起動します。
3. Editor Mode selector から **Quick SDF** を選びます。
4. Select mode で、編集したい mesh / material surface を viewport 上でクリックします。
5. **Material Slots** で active slot を確認します。選択行と cyan viewport overlay が現在の material slot を示し、row click で viewport pick を補正できます。
6. **Start Paint** を押します。Paint mode は既定で active slot を isolate します。全体表示が必要な場合は **Isolate Slot** をオフにします。
7. `LMB` で白、`Shift + LMB` で黒 / shadow をペイントします。
8. texture-space の作業には **2D Canvas** を使います。Texture Set / Angle selector、brush size、Fit / 100% zoom、rotate/flip、checker/grid、UV overlay、onion skin、ペン入力に対応します。
9. stroke を hold すると **Quick Stroke** になります。移動して preview を調整し、release で最終位置を確定します。
10. 最終 texture 生成前の確認には **Live SDF** material preview を使います。GPU JFA による高速な近似表示です。
11. Timeline で light angle を seek し、keyframe を追加または duplicate し、paint target mode を選びます。
12. **Generate Selected SDF** または **Generate SDF Threshold Map** を実行します。
13. `/Game/QuickSDF_GENERATED/` に生成された texture を toon material で使います。

## 互換性

QuickSDFTool v1.0 は Unreal Engine 5.7.x を対象にしており、UE 5.7.4 がリリース検証ターゲットです。UE 5.8+ は対応予定ですが、v1.0 の検証対象には含まれていません。

## 基本ワークフロー

```text
painted light/shadow masks -> SDF interpolation -> RGBA threshold texture -> controlled toon shadow
```

![QuickSDF authoring pipeline diagram]({{ '/images/quick-sdf-authoring-pipeline.png' | relative_url }})

QuickSDFTool は、物理的な正しさよりもアートディレクションに沿った影形状を優先したい場面に向いています。顔影、髪影、服の折り目影、小規模チームでの editor 内反復制作に使えます。
