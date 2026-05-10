<h1 align="center">QuickSDFTool</h1>

<p align="center">
  Unreal Engine 5 でトゥーン影マスクをペイントし、SDF スレッショルドマップを生成するエディターモードプラグインです。
  <br>
  <a href="#デモ">デモ</a> | <a href="#クイックスタート">クイックスタート</a> | <a href="#ドキュメント">ドキュメント</a> | <a href="./README.md">English</a>
</p>

> [!NOTE]
> **ステータス: UE 5.7.x 向け安定版 1.0。** QuickSDFTool は Unreal Engine 5.7.x での制作検証に使える正式版です。UE 5.8+ は対応予定ですが、このバージョンではリリース検証対象外です。

## デモ

QuickSDFTool は、複数のライト角度ごとにメッシュ上へ白黒のライト / シャドウマスクをペイントし、それらをトゥーン / セルシェーディング向けの高精度 SDF スレッショルドマップへ合成します。

https://github.com/user-attachments/assets/7eec2890-be31-4cbc-9662-756b6e84c620

| Select active slot | Screen mode での Paint |
| --- | --- |
| ![Select mode active material slot overlay](docs/images/quick-sdf-select-active-slot.png) | ![Paint mode with Screen projection brush preview](docs/images/quick-sdf-paint-screen-mode.png) |

スクリーンショット内キャラクターモデル: [真冬 Mafuyu / オリジナル3Dモデル](https://booth.pm/ja/items/5007531)（ぷらすわん）。キャラクターデザイン / 3Dモデリング: 有坂みと。

## 機能

- `Quick SDF` という専用 UE5 エディターモード。
- メッシュ全体を表示したまま、ビューポート上で mesh と material slot を選べる Select / 準備ワークフロー。
- Static Mesh / Skeletal Mesh component への直接ペイント。PhysicsAsset がない Skeletal Mesh も対象にできます。
- Screen、Surface、2D Canvas のペイントワークフロー。ブラシプレビュー、液タブ hover 追従、筆圧半径、Lazy Radius、ブラシ位置への `F` フォーカスに対応します。
- 2D Canvas は Texture Set / Angle selector、brush size、Fit / 100% zoom、rotate/flip、checker/grid、UV overlay、onion skin、ペン入力に対応します。
- Quick Stroke は、ストロークを一定時間 hold すると直線ストロークとして配置できます。Move Tolerance / Hold Time を調整でき、2D Canvas と 3D Paint の両方で軽量プレビュー後に release 位置で確定します。
- `Material Slots` リストは row selection、Select mode の cyan active-slot overlay、slot 単位 Bake、paint 中の slot isolation に対応します。
- Angle timeline は seek/keyframe lane、thumbnails、snapping、keyframe drag と seek の同期、paint target range highlight を備えます。
- `Auto`、`Texture Flip`、`UV Island Channel Flip`、通常の 0-180 painting の symmetry ワークフロー。
- 安定した threshold-map transition のための Monotonic Guard validation / clipping。
- 最終 bake 前に形状を確認できる GPU JFA 近似の `Live SDF` material preview。
- Mask import/export、非破壊の `UQuickSDFAsset` 保存、UE undo/redo、half-float texture 出力の CPU SDF generation。

## クイックスタート

1. このリポジトリを C++ Unreal project の `Plugins/QuickSDFTool/` にコピーします。
2. Project files を再生成し、ビルド後に **QuickSDFTool** を有効化してエディターを再起動します。
3. Editor Mode selector から **Quick SDF** を選びます。
4. Select mode で、編集したい mesh / material surface をビューポート上でクリックします。
5. **Material Slots** で active slot を確認します。選択行と cyan viewport overlay が現在の material slot を示します。
6. **Start Paint** を押します。Paint mode は既定で active slot を isolate します。全体表示のまま塗りたい場合は **Isolate Slot** をオフにします。
7. `LMB` で白 / light、`Shift + LMB` で黒 / shadow をペイントします。
8. texture-space で正確に塗りたい場合は **2D Canvas** を使います。UV guide、onion skin、checker/grid、zoom、rotate/flip、ペン入力のブラシ円とストローク位置合わせに対応します。
9. 直線ストロークを配置したい場合は、ストロークを hold して **Quick Stroke** にします。移動中のプレビューは軽量化され、release 位置で最終確定します。
10. 必要に応じて **Material Preview** を **Live SDF** に切り替え、ペイント中に GPU JFA preview を確認します。解像度は **Advanced** の `Live SDF Preview Resolution` で `128 px`、`256 px`、`512 px`、`1024 px` から選びます。
11. Timeline で light angle、mask 追加、Paint Target 範囲を調整し、最終 SDF threshold map を生成します。
12. `/Game/QuickSDF_GENERATED/` に生成された texture を toon material で使います。

詳しい流れは [Authoring Workflow](./docs/ja/workflow.md)、[Material Setup](./docs/ja/material-setup.md)、[Troubleshooting](./docs/ja/troubleshooting.md) を参照してください。

## インストール

QuickSDFTool v1.0 には Unreal Engine 5.7.x と C++ Unreal project が必要です。

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
| 5.7.x | v1.0 のサポート対象 |
| 5.8+ | 対応予定。ただし v1.0 リリース検証は未実施 |
| 5.6 以前 | 非対応 |

v1.0.1 の Win64 binary release は Epic Games Launcher 版 Unreal Engine 5.7.4 (`++UE5+Release-5.7`) でビルドされています。Launcher 版 UE 5.7.x project 向けに利用できます。source-built、licensee、custom、または異なる engine build で使う場合は、source から再ビルドしてください。

## ドキュメント

- [ドキュメントサイト](./docs/ja/index.md)
- [Authoring Workflow](./docs/ja/workflow.md)
- [Material Setup](./docs/ja/material-setup.md)
- [Troubleshooting](./docs/ja/troubleshooting.md)
- [Release Notes](./docs/release-notes/v1.0.1.md)
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
