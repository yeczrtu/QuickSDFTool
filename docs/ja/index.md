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
</div>

## クイックスタート

1. このリポジトリを C++ Unreal プロジェクトの `Plugins/QuickSDFTool/` にコピーします。
2. プロジェクトファイルを再生成し、ビルド後に **QuickSDFTool** を有効化してエディターを再起動します。
3. Editor Mode セレクタから **Quick SDF** を選びます。
4. レベル内のメッシュを選択します。
5. **Material Slots** で編集したい行をクリックします。ソースマスクが必要な場合は、その行の Bake アクションを使います。
6. `LMB` で白、`Shift + LMB` で黒 / 影をペイントします。
7. タイムラインでライト角度を移動し、キーフレーム追加や Paint Target を選びます。
8. **Generate Selected SDF** または **Generate SDF Threshold Map** を実行します。
9. `/Game/QuickSDF_GENERATED/` に生成されたテクスチャをトゥーンマテリアルで使います。

## 互換性

QuickSDFTool v1.0 は Unreal Engine 5.7.x を対象にしており、UE 5.7.4 がリリース検証ターゲットです。UE 5.8+ は対応予定ですが、v1.0 の検証対象には含まれていません。

## 基本ワークフロー

```text
ペイント済み明暗マスク -> SDF 補間 -> RGBA threshold texture -> 制御しやすいトゥーン影
```

QuickSDFTool は、物理的に正しい影よりも、アートディレクションに沿った影形状を優先したい場面に向いています。顔影、髪影、服の折り目影、小規模チームでのエディター内反復制作に使えます。
