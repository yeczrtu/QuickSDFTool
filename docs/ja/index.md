---
title: QuickSDFTool ドキュメント
description: Unreal Engine 向け QuickSDFTool のインストール、使い方、マテリアル連携、リリース情報。
lang: ja
alternate_url: /
alternate_label: English
---

# QuickSDFTool ドキュメント

QuickSDFTool は、Unreal Engine 5.7.x でトゥーン影マスクをメッシュ上へペイントし、スタイライズドレンダリング向けの SDF スレッショルドマップを生成するエディターモードプラグインです。

<p class="lead">このサイトでは、導入手順、基本ワークフロー、生成テクスチャのマテリアル接続、リリースごとの互換性情報を確認できます。</p>

<div class="hero-actions">
  <a class="button" href="#クイックスタート">クイックスタート</a>
  <a class="button secondary" href="https://github.com/yeczrtu/QuickSDFTool">GitHub リポジトリ</a>
  <a class="button secondary" href="https://github.com/user-attachments/assets/1eb770b6-b65d-44bb-b5a0-fbb78d998202">デモを見る</a>
</div>

## 主要ページ

<div class="doc-grid">
  <a class="doc-card" href="{{ '/ja/workflow/' | relative_url }}">
    <strong>Authoring Workflow</strong>
    <span>Select、Paint、Material Slots、Timeline、Symmetry、Guard、import/export、SDF generation を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/material-setup/' | relative_url }}">
    <strong>Material Setup</strong>
    <span>生成した SDF スレッショルドマップをトゥーン / セルシェーディング用マテリアルへ接続します。</span>
  </a>
  <a class="doc-card" href="{{ '/troubleshooting/' | relative_url }}">
    <strong>Troubleshooting</strong>
    <span>インストール、ビルド、マテリアル、ブラシ挙動に関する代表的な問題を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/release-notes/' | relative_url }}">
    <strong>Release Notes</strong>
    <span>検証済み Unreal Engine バージョン、アップグレード情報、リリースアセットを確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/roadmap/' | relative_url }}">
    <strong>Roadmap</strong>
    <span>1.0 以降の優先度と計画中機能要件を確認します。</span>
  </a>
  <a class="doc-card" href="{{ '/ja/development/' | relative_url }}">
    <strong>Development Notes</strong>
    <span>内部構成、検証、リポジトリ保守メモを確認します。</span>
  </a>
</div>

## ワークフロースクリーンショット

現在のドキュメント用キャプチャは、slot 選択から SDF 出力までの v1.0 ワークフローを示します。

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
      Timeline はサムネイル、角度ラベル、keyframe controls を表示したまま seek や key drag を扱います。
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-sdf-preview.png' | relative_url }}" alt="Generated SDF threshold texture preview">
    <figcaption>
      <strong>SDF output</strong>
      生成された SDF threshold texture がトゥーンマテリアルで使うマップです。
    </figcaption>
  </figure>
</div>

<p class="media-credit">スクリーンショット内キャラクターモデル: <a href="https://booth.pm/ja/items/5007531">真冬 Mafuyu / オリジナル3Dモデル</a>（ぷらすわん）。キャラクターデザイン / 3Dモデリング: 有坂みと。</p>

## クイックスタート

1. このリポジトリを C++ Unreal プロジェクトの `Plugins/QuickSDFTool/` にコピーします。
2. プロジェクトファイルを再生成し、ビルド後に **QuickSDFTool** を有効化してエディターを再起動します。
3. Editor Mode セレクタから **Quick SDF** を選びます。
4. Select mode で、編集したい mesh / material surface をビューポート上でクリックします。
5. **Material Slots** で active slot を確認します。選択中の行と cyan viewport overlay が現在の material slot を示し、行クリックでビューポート選択を補正できます。
6. **Start Paint** を押します。Paint mode は active slot を既定で isolate します。全体表示のまま塗りたい場合は **Isolate Slot** をオフにします。
7. `LMB` で白、`Shift + LMB` で黒 / 影をペイントします。
8. タイムラインでライト角度を移動し、キーフレーム追加や Paint Target を選びます。
9. **Generate Selected SDF** または **Generate SDF Threshold Map** を実行します。
10. `/Game/QuickSDF_GENERATED/` に生成されたテクスチャをトゥーンマテリアルで使います。

## 互換性

QuickSDFTool v1.0 は Unreal Engine 5.7.x を対象にしており、UE 5.7.4 がリリース検証ターゲットです。UE 5.8+ は対応予定ですが、v1.0 の検証対象には含まれていません。

## 基本ワークフロー

```text
ペイント済み明暗マスク -> SDF 補間 -> RGBA threshold texture -> 制御しやすいトゥーン影
```

QuickSDFTool は、物理的に正しい影よりも、アートディレクションに沿った影形状を優先したい場面に向いています。顔影、髪影、服の折り目影、小規模チームでのエディター内反復制作に使えます。
