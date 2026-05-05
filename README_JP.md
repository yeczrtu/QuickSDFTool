<h1 align="center">QuickSDFTool</h1>

<p align="center">
  Unreal Engine 5 でトゥーン影マスクをペイントし、SDF スレッショルドマップを生成するエディターモードプラグイン。
  <br>
  <a href="#デモ">デモ</a> | <a href="#クイックスタート">クイックスタート</a> | <a href="#ドキュメント">ドキュメント</a> | <a href="./README.md">English</a>
</p>

> [!NOTE]
> **ステータス: UE 5.7.x 向け安定版 1.0。** QuickSDFTool は Unreal Engine 5.7.x での制作検証に使える正式版です。UE 5.8+ は対応予定ですが、このバージョンではリリース検証対象外です。

## デモ

QuickSDFTool は、複数のライト角度ごとにメッシュ上へ二値の明暗マスクをペイントし、それらをトゥーン / セルシェーディング向けの高精度 SDF スレッショルドテクスチャへ合成します。

https://github.com/user-attachments/assets/7eec2890-be31-4cbc-9662-756b6e84c620

| Select active slot | Screen mode での Paint |
| --- | --- |
| ![Select mode active material slot overlay](docs/images/quick-sdf-select-active-slot.png) | ![Paint mode with Screen projection brush preview](docs/images/quick-sdf-paint-screen-mode.png) |

スクリーンショット内キャラクターモデル: [真冬 Mafuyu / オリジナル3Dモデル](https://booth.pm/ja/items/5007531)（ぷらすわん）。キャラクターデザイン / 3Dモデリング: 有坂みと。

## 機能

- `Quick SDF` という専用 UE5 エディターモード。
- メッシュ全体を表示したまま始まる Select / 準備ワークフロー。ビューポート上で mesh と material slot を同時に選択できます。
- Static Mesh / Skeletal Mesh コンポーネントへの直接ペイント。PhysicsAsset がない Skeletal Mesh も選択対象です。
- Screen、Surface、UV 系のペイントワークフロー。ブラシプレビュー、筆圧半径、Lazy Radius、ブラシ位置への `F` フォーカスに対応します。
- コンパクトな `Material Slots` リスト、Select 中の cyan active-slot overlay、スロット単位 Bake、Paint 中の既定 slot isolation。
- シーク / キーフレームレーンを分けた角度タイムライン。サムネイル、スナップ、キー移動とシーク同期、Paint Target 範囲表示に対応します。
- `Auto`、`Texture Flip`、`UV Island Channel Flip`、0-180 通常ペイントのシンメトリーワークフロー。
- 安定した threshold map 遷移のための Monotonic Guard 検証とクリッピング。
- マスク import/export、非破壊の `UQuickSDFAsset` 保存、UE undo/redo、half-float テクスチャ出力の CPU SDF 生成。

## クイックスタート

1. このリポジトリを C++ Unreal プロジェクトの `Plugins/QuickSDFTool/` にコピーします。
2. プロジェクトファイルを再生成し、ビルド後に **QuickSDFTool** を有効化してエディターを再起動します。
3. エディターモードセレクターから **Quick SDF** を選びます。
4. Select mode で、編集したい mesh / material surface をビューポート上でクリックします。
5. **Material Slots** で active slot を確認します。選択行と cyan viewport overlay が現在の material slot を示します。
6. **Start Paint** を押します。Paint mode では active slot が既定で isolate されます。全体表示のまま塗りたい場合は **Isolate Slot** をオフにします。
7. `LMB` で白 / ライト、`Shift + LMB` で黒 / 影をペイントします。
8. タイムラインでライト角度、マスク追加、Paint Target 範囲を調整し、SDF threshold map を生成します。
9. `/Game/QuickSDF_GENERATED/` に生成されたテクスチャをトゥーンマテリアルで使います。

詳しい流れは [Authoring Workflow](./docs/ja/workflow.md)、[Material Setup](./docs/material-setup.md)、[Troubleshooting](./docs/troubleshooting.md) を参照してください。

## インストール

QuickSDFTool v1.0 には Unreal Engine 5.7.x と C++ Unreal プロジェクトが必要です。

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

その後、プロジェクトファイルを再生成し、プロジェクトをビルドして **QuickSDFTool** を有効化し、エディターを再起動します。

## 互換性

| Unreal Engine version | ステータス |
| --- | --- |
| 5.7.4 | 必須リリース検証ターゲット |
| 5.7.x | v1.0 のサポート対象 |
| 5.8+ | 対応予定。ただし v1.0 リリース検証は未実施 |
| 5.6 以前 | 非対応 |

v1.0 のバイナリリリースは、現在のカスタム UE 5.7 系検証エディターでビルドしたものです。Epic Games Launcher 版 Unreal Engine との互換バイナリとしては表記していません。Launcher 版 UE では、使用するエンジンビルドに合わせてソースから再ビルドしてください。

## ドキュメント

- [ドキュメントサイト](./docs/ja/index.md)
- [Authoring Workflow](./docs/ja/workflow.md)
- [Material Setup](./docs/material-setup.md)
- [Troubleshooting](./docs/troubleshooting.md)
- [Release Notes](./docs/release-notes/v1.0.0.md)
- [Roadmap](./docs/ja/roadmap.md)
- [Development Notes](./docs/ja/development.md)

## コントリビューション

ドキュメント、UE バージョン検証、小さなワークフロー修正、サンプル追加は特に歓迎します。変更範囲を小さく保ち、pull request には再現手順または検証内容を書いてください。

## 謝辞

- [Unreal Engine Interactive Tools Framework](https://docs.unrealengine.com/5.0/en-US/interactive-tools-framework-in-unreal-engine/) - エディターペイントワークフローの基盤。
- Felzenszwalb & Huttenlocher - *Distance Transforms of Sampled Functions* (2012)。
- Jump Flooding Algorithm (JFA) - GPU 距離場生成の参考。
- [UE5 SDF Face Shadow Mappingでアニメ顔用の影を作ろう](https://unrealengine.hatenablog.com/entry/2024/02/28/222220)。
- [SDF TextureとLiltoonでセルルックの影を再現しよう！](https://note.com/ca__mocha/n/n9289fbbc4c8b)。
