<h1 align="center">QuickSDFTool</h1>

<p align="center">
  Unreal Engine 5 内でアニメ / トゥーン顔影マスクをペイントし、
  SDF しきい値マップまで生成できるエディターモードプラグインです。
  <br>
  UE5 で制御しやすいアニメ調の顔影を作りたいアーティスト、テクニカルアーティスト向けです。
  <br>
  <a href="#デモ">デモ</a> | <a href="#なぜ-quicksdftool">なぜ QuickSDFTool?</a> | <a href="#クイックスタート">クイックスタート</a> | <a href="#ドキュメント">ドキュメント</a> | <a href="./README.md">English</a>
</p>

<p align="center">
  <a href="https://github.com/yeczrtu/QuickSDFTool/releases/latest"><img alt="Release v1.1.0" src="https://img.shields.io/badge/release-v1.1.0-2f80ed"></a>
  <img alt="Unreal Engine 5.7.4" src="https://img.shields.io/badge/Unreal%20Engine-5.7.4-313f9f">
  <img alt="Platform Win64" src="https://img.shields.io/badge/platform-Win64-4b5563">
  <a href="https://yeczrtu.github.io/QuickSDFTool/"><img alt="Docs GitHub Pages" src="https://img.shields.io/badge/docs-GitHub%20Pages-0f766e"></a>
  <a href="./LICENSE"><img alt="License MIT" src="https://img.shields.io/badge/license-MIT-green"></a>
</p>

## デモ

QuickSDFTool は、顔影制作のループを UE5 エディター内にまとめます。実際のキャラクターメッシュ上にマスクを塗り、SDF の見え方を確認し、トゥーン / セルシェーディング用のしきい値テクスチャとしてベイクできます。

https://github.com/user-attachments/assets/7eec2890-be31-4cbc-9662-756b6e84c620

<p align="center">
  <img src="docs/images/quick-sdf-2d-canvas-pen-paint.gif" alt="2D Canvas でトゥーン影マスクをペイントしている様子" width="800">
</p>

<p align="center">
  UE5 内でマスクをペイント → Live SDF でプレビュー → しきい値テクスチャを生成 → toon material に適用。
</p>

| Active slot 選択 | Screen mode での Paint | 生成された SDF preview |
| --- | --- | --- |
| ![Select mode active material slot overlay](docs/images/quick-sdf-select-active-slot.png) | ![Paint mode with Screen projection brush preview](docs/images/quick-sdf-paint-screen-mode.png) | ![Generated SDF threshold texture preview](docs/images/quick-sdf-sdf-preview.png) |

スクリーンショット内キャラクターモデル: [真冬 Mafuyu / オリジナル3Dモデル](https://booth.pm/ja/items/5007531)（ぷらすわん）。キャラクターデザイン / 3Dモデリング: 有坂みと。

## なぜ QuickSDFTool?

| Workflow | QuickSDFTool | 外部ツール中心のワークフロー |
| --- | --- | --- |
| UE メッシュ上へ直接ペイント | 可能 | 不可 |
| 実際のキャラクター上でプレビュー | 可能 | できないことが多い |
| Material Slot 単位のペイント | 可能 | できないことが多い |
| 2D Canvas と液タブ向けワークフロー | 可能 | ツール依存 |
| UE undo/redo support | 可能 | 不可 |
| SDF しきい値テクスチャ生成 | 可能 | 可能 |
| DCC / ペイントソフト / script 間の往復 | 不要 | 必要になりがち |

## 誰向け?

QuickSDFTool は、DCC ツール、2D ペイントソフト、外部 script、Unreal Engine を何度も往復せずに、制御しやすいアニメ / トゥーン顔影を作りたい technical artist、character artist、UE5 developer 向けです。

キャラクターメッシュ上に影マスクを直接ペイントし、複数の light angle を Unreal Editor 内で確認しながら調整し、toon / cel shading material 用の SDF しきい値テクスチャとしてベイクできます。

## 機能ハイライト

- `Quick SDF` という専用 UE5 エディターモード。
- Static Mesh / Skeletal Mesh component への直接ペイント。
- Material Slot 単位のペイント、slot isolation、Select mode の active-slot overlay。
- Screen、Surface、2D Canvas のペイントワークフローと液タブ入力サポート。
- 最終 bake 前に確認できる Live SDF material preview。
- half-float texture 出力の CPU SDF しきい値テクスチャ生成。

## クイックスタート

1. [latest release](https://github.com/yeczrtu/QuickSDFTool/releases/latest) をダウンロードします。
2. `QuickSDFTool` を `YourProject/Plugins/` にコピーします。
3. Project files を再生成します。
4. C++ Unreal project をビルドします。
5. Plugins で **QuickSDFTool** を有効化し、エディターを再起動します。
6. Editor Mode selector から **Quick SDF** を開きます。
7. Select mode で編集したい mesh / material surface をクリックし、**Material Slots** で active slot を確認します。
8. **Start Paint** を押し、`LMB` で白 / light、`Shift + LMB` で黒 / shadow をペイントします。
9. texture-space で正確に塗りたい場合は **2D Canvas** を使います。UV guide、onion skin、checker/grid、zoom、rotate/flip、ペン入力に対応します。
10. 必要に応じて **Material Preview** を **Live SDF** に切り替え、Timeline で mask を追加して最終 SDF しきい値マップを生成します。
11. `/Game/QuickSDF_GENERATED/` に生成された texture を toon material で使います。

詳しい流れは [Authoring Workflow](./docs/ja/workflow.md)、[Material Setup](./docs/ja/material-setup.md)、[Troubleshooting](./docs/ja/troubleshooting.md) を参照してください。

## インストール

QuickSDFTool v1.1.0 には Unreal Engine 5.7.x と C++ Unreal project が必要です。

```bash
git clone https://github.com/yeczrtu/QuickSDFTool.git
```

プラグインを次の場所に配置します。

```text
YourProject/
|-- Plugins/
    |-- QuickSDFTool/
        |-- QuickSDFTool.uplugin
        |-- Source/
        |-- Shaders/
        |-- Content/
```

その後、project files を再生成し、プロジェクトをビルドして **QuickSDFTool** を有効化し、エディターを再起動します。

## 互換性

| Unreal Engine version | ステータス |
| --- | --- |
| 5.7.4 | 必須リリース検証ターゲット |
| 5.7.x | v1.1.0 のサポート対象 |
| 5.8+ | 対応予定。ただし v1.1.0 リリース検証は未実施 |
| 5.6 以前 | 非対応 |

v1.1.0 の source release は Unreal Engine 5.7.4 を必須検証 version としています。source-built、licensee、custom、または異なる engine build で使う場合も含め、使用中の engine build に合わせて source から再ビルドしてください。

## ドキュメント

- [ドキュメントサイト](./docs/ja/index.md)
- [Authoring Workflow](./docs/ja/workflow.md)
- [Material Setup](./docs/ja/material-setup.md)
- [Troubleshooting](./docs/ja/troubleshooting.md)
- [Release Notes](./docs/release-notes/v1.1.0.md)
- [Roadmap](./docs/ja/roadmap.md)
- [Development Notes](./docs/ja/development.md)
- [English README](./README.md)

## コントリビューション

ドキュメント、UE version 検証、小さなワークフロー修正、sample content の追加を歓迎します。変更範囲は小さく保ち、pull request には再現手順または検証内容を書いてください。

## 謝辞

- [Unreal Engine Interactive Tools Framework](https://docs.unrealengine.com/5.0/en-US/interactive-tools-framework-in-unreal-engine/) - editor paint workflow の基盤。
- Felzenszwalb & Huttenlocher - *Distance Transforms of Sampled Functions* (2012)。
- Jump Flooding Algorithm (JFA) - GPU distance field generation の参考。
- [UE5 SDF Face Shadow Mappingでアニメ顔用の影を作ろう](https://unrealengine.hatenablog.com/entry/2024/02/28/222220)。
- [SDF TextureとLiltoonでセルルックの影を再現しよう](https://note.com/ca__mocha/n/n9289fbbc4c8b)。
