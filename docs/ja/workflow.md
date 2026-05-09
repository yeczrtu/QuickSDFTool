---
title: Authoring Workflow
description: QuickSDFTool の Select、Paint、Material Slots、Timeline、Symmetry、Monotonic Guard、SDF 生成ワークフロー。
permalink: /ja/workflow/
lang: ja
alternate_url: /workflow/
alternate_label: English
---

# Authoring Workflow

QuickSDFTool は、1 つの material slot を選び、その slot 向けの明暗マスクを複数角度で作成し、最後にトゥーンシェーダー用の threshold texture を生成する流れを中心に設計しています。

## ワークフロースクリーンショット

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

## 操作方法

| 入力 | アクション |
| --- | --- |
| `LMB Drag` | 白 / ライトをペイント |
| `Shift + LMB Drag` | 黒 / 影をペイント |
| Select mode の `Viewport Mesh / Material Click` | target mesh component とクリックした material slot を選択 |
| `Start Paint` | 選択中の target mesh と material slot で Paint mode に入る |
| Paint mode の `F` | 現在のブラシ位置へフォーカス。ブラシヒットがない場合は UE 標準の選択フォーカスへフォールバック |
| `Ctrl + F`, マウスまたはペン移動, クリック | ポインタがビューポートまたは 2D Canvas 上にあるときにブラシサイズを変更 |
| `Alt + T` | クイックトグルメニューを開く |
| `Alt + 1` | Paint Target モードを切り替え |
| `Alt + 2` - `Alt + 8` | Auto Light、Preview、UV overlay、Onion Skin、Quick Stroke、Symmetry、Monotonic Guard を切り替え |
| `Left / Right Arrow` | 前 / 次のタイムラインフレームを選択 |
| `Material Slot Row Click` | 編集する material slot / Texture Set を選択または補正 |
| `Material Slot Bake Icon` | その slot だけを Bake |
| `Timeline Seek Lane Click / Drag` | キーフレームを動かさずにプレビューライト角度をシーク |
| `Timeline Key Click` | 角度を選択 |
| `Timeline Key Drag` | ドラッグしきい値を超えたあとに角度を調整。シークカーソルとプレビューライトも追従 |
| `Timeline Status Badge Hover` | 角度、テクスチャ、編集状態、Paint Target 範囲、Guard、上書き可否、警告内容を確認 |
| `Timeline Add / Duplicate / Delete` | キーフレームを作成、コピー、削除 |
| `Timeline 8 or 15 / Even` | 標準マスク数へ補完、または角度を均等配置 |
| `Texture2D` アセットをタイムラインへドラッグ | 編集済みマスクをインポート |
| `Ctrl + Z / Ctrl + Y` | Undo / Redo |

## Select と Material Slots

`Quick SDF > Material Slots` は、アーティストが 1 つの material slot を選んで編集する作業に合わせています。

- Select mode は非破壊で開始し、メッシュ全体を表示したまま、選択なし入場時は古い target を消します。
- ビューポート上のメッシュ面をクリックすると、target mesh component とヒットした material slot が同時に選択されます。
- 行クリックで対応する Texture Set と paint / bake 対象を選択または補正できます。
- Select mode では cyan viewport overlay で active slot を示し、対象外 slot を隠しません。
- Paint mode は **Isolate Slot** が既定でオンの状態から始まります。オフにすると全体表示へ戻りますが、選択中 slot は引き続き paint target です。
- ペイントのピックとストロークサンプリングは選択中 slot を編集フィルターとして使うため、非対象 slot が active slot のヒットを奪いません。
- 各行には slot 番号、slot 名、material 名、`Selected` / `Baked` / `Empty` などのステータスを表示します。
- 行右端のアクションはその slot だけを Bake し、元シェーディングマスクの bake scope として選択中 slot を使います。
- 表示行は現在の mesh component から毎回再構築されるため、target 切り替え後に古い `Missing` 行が残りません。

## Painting

- Static Mesh / Skeletal Mesh component へ直接ペイントできます。PhysicsAsset がない Skeletal Mesh も選択対象です。
- 既定の paint mode は Screen projection です。現在のカメラから顔や髪の影を直接作る用途に向いています。
- Surface と UV 系の paint workflow も、mesh-space / texture-space の作業向けに利用できます。
- 2D Canvas は texture-space の調整、UV guide、onion skinning、液タブでのペイント、ペンによるブラシリサイズに対応します。
- Windows の液タブ / ペンタブでは、QuickSDF が pen pointer 座標を直接読み取り、hover、stroke start、drag、release、2D Canvas 入力に使います。エディターウィンドウを移動またはリサイズしたあとも、ブラシプレビューと実際のペイント位置がずれにくくなります。
- Brush input は Lazy Radius、細かい stamp spacing、antialiased brush mask、tablet 向けの筆圧半径に対応します。
- 筆圧は既定で有効で、マスク濃度ではなくブラシ半径だけに反映されます。無効化は **Advanced > Pen Pressure**、反応調整は **Advanced > Pen Pressure Curve** で行います。`1.0` が線形で、値を上げると弱筆圧が細く保たれ、値を下げると軽い筆圧でも太くなりやすくなります。
- `F` は有効な brush hit がある場合に現在位置へフォーカスします。無効な場合は UE 標準の選択フォーカスへフォールバックします。

## Timeline と Paint Target

Timeline は、シーク操作と keyframe 操作の誤操作を減らすため、2 つの lane に分かれています。

- 上段の seek lane だけがクリック / ドラッグによる preview angle のシークを処理します。
- 下段の keyframe lane には thumbnails と key handles を配置します。keyframe はクリックで選択され、drag threshold を超えた場合だけ移動します。
- keyframe をドラッグすると seek cursor と preview light も即時更新されます。
- `Current`、`All`、`Before`、`After` の Paint Target 範囲ハイライトを表示します。表示範囲は隣接角度の中点から計算するため、実際に編集される mask 範囲と一致します。
- key status badge は hit-test invisible で、選択、drag、import、seek の邪魔をしません。
- `8` / `15` と `Even` は、標準 mask set の補完または角度の均等配置を行います。Symmetry mode では 8 masks、非 symmetry mode では 15 masks へ補完します。

## Live SDF Preview

`Live SDF` は、ペイント中に形状を素早く確認するための material preview mode です。保存済みの `Generated SDF` texture とは分離されています。

- **Material Preview** で `Live SDF` を選んだ場合だけ GPU preview を生成します。他の preview mode では Live SDF 用の生成は走りません。
- editable paint render target から GPU Jump Flooding Algorithm で近似 threshold map を作ります。
- 解像度は **Advanced > Live SDF Preview Resolution** で `128 px`、`256 px`、`512 px`、`1024 px` から選びます。選択値は transient preview render target の長辺になります。
- 値を上げるとエッジと細部は安定しますが、GPU 負荷は増えます。既定値は `512 px` です。
- JFA pass は 1 回の preview update 内で完了します。ストローク中の更新頻度は throttle されますが、JFA step 自体は複数フレームへ分散していません。
- 最終保存用 SDF は従来通り CPU 生成です。full-resolution 処理、upscaling、UV island mirror output、final export packing は最終生成側で確認してください。

## Symmetry Modes

QuickSDFTool の SDF 生成には 4 つのモードがあります。

- `Auto`: 顔ペイント向けの既定モードです。対象 mesh、UV channel、material slot を解析し、Texture Flip または UV Island Channel Flip を選びます。
- `Off`: 0-180 度の全範囲を通常通りペイントします。
- `Texture Flip`: 0-90 度だけをペイントし、90-180 度側は texture 全体を反転して生成します。
- `UV Island Channel Flip`: 0-90 度だけをペイントし、90-180 度側は UV island 単位で island-local mirror sampling により生成します。

`UV Island Channel Flip` は、左右の UV island が別々でも形状として mirror 関係にある asset 向けです。位置、スケール、軽い形状差は normalized island-local 変換で吸収しますが、非線形の per-island warp は行いません。曖昧、未ペア、重なり、0-1 外 UV などは source 側の値コピーへフォールバックし、warning を残します。

生成 texture は通常の 0-180 layout と同じ shader path で読めます。内部の combined field は `R/G/B/A` として扱い、最終出力時に R/A/B/G swizzle (`R <- R`, `G <- A`, `B <- B`, `A <- G`) を行います。

## Monotonic Guard

`Monotonic Guard` は、SDF threshold mask 向けの任意の paint-time safety check です。`R >= 127` を白、それ未満を黒として扱い、処理対象の角度列で `black -> white -> black` や `white -> black -> white` のような複数回反転が生まれないようにします。

- quick toggle 上の表示名は `Guard`、shortcut は `Alt + 8` です。
- `Clip Direction` の標準は `Auto` です。`0-90` 度では `White Expands`、`90-180` 度では `White Shrinks` として扱います。
- 通常 brush と Quick Stroke は、Undo 差分が確定する前に silent に clip されます。
- antialiased edge も stroke intent として扱うため、`127` の binary threshold をまたがない場合も clip 対象になります。
- `Current / All / Before / After` は、どの mask へ stroke を書き込むかを決めます。
- Import、rebake、SDF generation は自動修正されません。既存違反は validation または Guard 有効状態での SDF generation 時に warning として確認できます。

## Mask I/O と SDF 生成

- Mask import は file picker と timeline drag-and-drop に対応します。
- Mask export により、外部編集やレビューができます。
- 作業状態は非破壊の `UQuickSDFAsset` に保存され、必要に応じて mask texture も保存できます。
- SDF generation は CPU 処理で、Monopolar / Bipolar 自動 packing、1x-8x upscaling、R/A/B/G output swizzling、half-float texture export に対応します。
- `Generated SDF` は保存済みの最終 texture を表示します。`Live SDF` は transient GPU JFA 近似の表示で、最終出力としては保存されません。
- 生成 map は既定で `/Game/QuickSDF_GENERATED/` に保存されます。
- shader integration の詳細は [Material Setup]({{ '/material-setup/' | relative_url }}) を参照してください。
