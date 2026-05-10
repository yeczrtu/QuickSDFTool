---
title: Authoring Workflow
description: QuickSDFTool の Select、Paint、2D Canvas、Quick Stroke、Material Slots、Timeline、Symmetry、Monotonic Guard、SDF generation ワークフロー。
permalink: /ja/workflow/
lang: ja
alternate_url: /workflow/
alternate_label: English
---

# Authoring Workflow

QuickSDFTool は、1 つの material slot を選び、その slot 向けの light / shadow mask を複数角度で作成し、最後に toon shader 用の threshold texture を生成する流れを中心に設計されています。

![QuickSDF authoring pipeline diagram]({{ '/images/quick-sdf-authoring-pipeline.png' | relative_url }})

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
      Paint mode は Screen projection、緑のブラシプレビュー、UV texture preview、active slot context を表示します。
    </figcaption>
  </figure>
  <figure class="screenshot-card compact">
    <img src="{{ '/images/quick-sdf-timeline.png' | relative_url }}" alt="Quick SDF timeline controls and keyframes">
    <figcaption>
      <strong>Timeline</strong>
      Timeline は thumbnails、seek lane、keyframe lane、status badges を表示したまま seek や key drag を行えます。
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

## 操作方法

| 入力 | アクション |
| --- | --- |
| `LMB Drag` | 白 / light をペイント |
| `Shift + LMB Drag` | 黒 / shadow をペイント |
| stroke を hold | **Quick Stroke Hold Time** 後に Quick Stroke 化 |
| Select mode の `Viewport Mesh / Material Click` | target mesh component とクリックした material slot を選択 |
| `Start Paint` | 選択中の target mesh と material slot で Paint mode に入る |
| Paint mode の `F` | 現在の brush position へ viewport focus。brush hit がない場合は UE 標準の selection focus に fallback |
| `Ctrl + F`、mouse または pen を移動、click | pointer が viewport または 2D Canvas 上にあるとき brush size を変更 |
| `Alt + T` | quick toggle menu を開く |
| `Alt + 1` | Paint Target mode を切り替える |
| `Alt + 2` - `Alt + 8` | Auto Light、Preview、UV overlay、Onion Skin、Quick Stroke、Symmetry、Monotonic Guard を切り替える |
| `Left / Right Arrow` | 前 / 次の timeline frame を選択 |
| `Material Slot Row Click` | 編集する material slot / texture set を選択または補正 |
| `Material Slot Bake Icon` | その slot だけを Bake |
| `Timeline Seek Lane Click / Drag` | keyframe を動かさずに preview light angle を seek |
| `Timeline Key Click` | angle を選択 |
| `Timeline Key Drag` | drag threshold を超えた後に angle を調整。seek cursor と preview light も追従 |
| `Timeline Status Badge Hover` | angle、texture、edit state、paint target inclusion、Guard state、overwrite state、warning details を確認 |
| `Timeline Add / Duplicate / Delete` | keyframe を作成、コピー、削除 |
| `Timeline 8 or 15 / Even` | default mask set の補完、または angle の均等配置 |
| `Texture2D` assets を timeline へ drag | 編集済み mask を import |
| `Ctrl + Z / Ctrl + Y` | Undo / Redo |

## Select と Material Slots

`Quick SDF > Material Slots` は、アーティストが 1 つの material slot を選んで編集する作業に合わせています。

- Select mode は非破壊で開始し、メッシュ全体を表示したまま、選択がない場合は古い target を clear します。
- viewport 上の mesh surface を click すると、target mesh component と hit した material slot が同時に選択されます。
- row click で対応する texture set と active paint / bake target を選択または補正できます。
- Select mode は cyan viewport overlay で active slot を示し、他の slot を隠しません。
- Paint mode は **Isolate Slot** が既定で on の状態から始まります。off にすると全体表示へ戻りますが、選択中 slot は引き続き paint target です。
- paint picking と stroke sampling は選択中 slot を edit filter として使うため、非対象 slot が active slot の hit を奪いません。
- 各 row には slot number、slot name、material name、`Selected` / `Baked` / `Empty` などの compact status が表示されます。
- row action button はその slot だけを Bake し、original-shading mask の bake scope として active slot を使います。
- visible row は現在の mesh component から毎回 rebuild されるため、target 切り替え後に古い `Missing` row が残りません。

## Painting

- Static Mesh / Skeletal Mesh component へ直接 paint できます。PhysicsAsset がない Skeletal Mesh も対象です。
- 既定の paint mode は Screen projection です。現在の camera から顔や髪の影を直接作る用途に向いています。
- Surface と UV-oriented painting も、mesh-space / texture-space の作業向けに利用できます。
- Brush input は Lazy Radius stroke stabilization、細かい stamp spacing、antialiased brush mask、tablet 向けの pressure-driven brush radius に対応します。
- 筆圧は既定で有効で、mask opacity ではなく brush radius だけに反映されます。無効化は **Advanced > Pen Pressure**、反応調整は **Advanced > Pen Pressure Curve** で行います。`1.0` が線形で、値を上げると弱い筆圧が細く保たれ、値を下げると軽い筆圧でも太くなりやすくなります。
- `F` は有効な brush hit がある場合に現在位置へ focus します。無効な場合は UE 標準の selection focus へ fallback します。

### 2D Canvas

stroke を直接 texture-space に置きたい場合、または UV guide を見ながら調整したい場合は 2D Canvas を使います。

- 有効な mesh、material slot、texture set、timeline angle を選んだ後、Paint mode から canvas を開きます。
- canvas は active **Texture Set** と **Angle** に追従します。slot や timeline key を切り替えた場合は selector を確認してください。
- brush size は paint tool と共有されます。`Ctrl + F` brush resize は 3D viewport だけでなく 2D Canvas 上でも動作します。
- **Fit** は texture 全体を canvas に表示します。**100%** は可能な限り texture pixel と screen pixel を 1:1 で表示します。
- rotate / flip は canvas view の向きだけを変えます。保存される mask data は rotate / flip されません。
- checker と grid overlay は透明領域、背景、texel spacing の確認に使います。
- UV overlay は active UV channel を表示します。onion skin は単一 angle を編集しながら隣接 mask と比較するために使います。
- Windows pen input は現在の absolute pointer position から canvas coordinates へ変換され、hover、stroke start、drag、release、pressure、brush resize に使われます。canvas window を移動または resize した後も、表示される brush circle と painted stroke は揃うべきです。

### Quick Stroke

Quick Stroke は、直線または慎重に位置決めした stroke を置くための hold-to-place workflow です。

- quick toggle menu または **Advanced** settings で **Quick Stroke** を有効にします。
- **Quick Stroke Move Tolerance** より大きく動かさずに press / hold します。
- **Quick Stroke Hold Time** 後、現在の stroke が Quick Stroke preview になります。
- mouse または pen を動かすと preview endpoint を調整できます。preview update は高頻度の pen movement が毎 frame の重い stroke work にならないよう軽量化されています。
- release すると最新 pointer position で最終 stroke を commit します。確定 stroke は古い preview sample ではなく release 時の endpoint を使います。
- Quick Stroke は 3D Paint と 2D Canvas の両方で動作します。`Current / All / Before / After`、active material slot filter、symmetry、Monotonic Guard clipping も尊重します。

<div class="screenshot-grid">
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-pen-input-flow.png' | relative_url }}" alt="Windows pen pointer input flow into QuickSDF 3D Paint and 2D Canvas">
    <figcaption>
      <strong>Pen input flow</strong>
      QuickSDF は新しい Windows pen coordinates を plugin 内で viewport ray または 2D Canvas position へ変換します。
    </figcaption>
  </figure>
  <figure class="screenshot-card">
    <img src="{{ '/images/quick-sdf-pressure-curve.png' | relative_url }}" alt="Pen Pressure Curve response examples for brush radius">
    <figcaption>
      <strong>Pressure curve</strong>
      筆圧は radius だけを変えます。curve は弱い筆圧から大きい brush size へ到達する速さを調整します。
    </figcaption>
  </figure>
</div>

## Timeline と Paint Targets

Timeline は、seek 操作と keyframe 編集の誤操作を減らすため、2 つの lane に分かれています。

- 上段の seek lane だけが click / drag による preview angle の seek を処理します。
- 下段の keyframe lane には thumbnails と key handles を配置します。Keyframe は click で選択され、drag threshold を超えた場合だけ移動します。
- keyframe を drag すると seek cursor と preview light も即時更新されます。
- `Current`、`All`、`Before`、`After` の Paint Target 範囲 highlight を表示します。表示範囲は隣接 angle の中点から計算するため、実際に編集される mask 範囲と一致します。
- key status badge は hit-test invisible で、選択、drag、import、seek の邪魔をしません。
- `8` / `15` と `Even` は default mask set の補完または angle の均等配置を行います。Symmetry mode では 8 masks、非 symmetry mode では 15 masks へ補完します。

## Live SDF Preview

`Live SDF` は、painting 中に形状を素早く確認するための material preview mode です。保存済みの `Generated SDF` texture とは別物です。

- **Material Preview** で `Live SDF` を選んだ場合だけ GPU preview を生成します。他の preview mode では Live SDF 用の生成は走りません。
- editable paint render target から GPU Jump Flooding Algorithm path で threshold map を近似します。
- 解像度は **Advanced > Live SDF Preview Resolution** で `128 px`、`256 px`、`512 px`、`1024 px` から選びます。選択値は transient preview render target の長辺になります。
- 値を上げると edge stability と細部は読みやすくなりますが、GPU cost は増えます。既定値は `512 px` です。
- JFA pass は 1 回の preview update 内で完了します。stroke 中の更新頻度は throttle されますが、JFA step 自体を複数 frame へ分割していません。
- 最終保存用 SDF は従来通り CPU generation path です。full-resolution processing、upscaling、UV island mirror output、final export packing は最終生成側で確認してください。

## Symmetry Modes

QuickSDFTool の SDF generation には 4 つの mode があります。

- `Auto`: 顔ペイント向けの既定 mode です。active mesh、UV channel、material slot を解析し、Texture Flip または UV Island Channel Flip を選びます。
- `Off`: 0-180 degree sweep を通常通り paint します。
- `Texture Flip`: 0-90 degrees を paint し、90-180 側は texture 全体を mirror して生成します。
- `UV Island Channel Flip`: 0-90 degrees を paint し、90-180 側は UV island 単位で island-local mirrored sampling により生成します。

![QuickSDF symmetry mode flow]({{ '/images/quick-sdf-symmetry-flow.png' | relative_url }})

`UV Island Channel Flip` は、左右の UV island が別々でも形状として mirror 関係にある asset 向けです。位置、scale、軽い light shape 差は normalized island-local mapping で吸収しますが、非線形の per-island warp は行いません。曖昧、未ペア、重なり、0-1 外 UV などは source-side values の copy へ fallback し、warning を残します。

生成 texture は通常の 0-180 layout と同じ shader path で読めます。内部の combined field は `R/G/B/A` として扱い、最終出力時に R/A/B/G swizzle (`R <- R`, `G <- A`, `B <- B`, `A <- G`) を行います。

## Monotonic Guard

`Monotonic Guard` は、SDF threshold mask 向けの paint-time safety check です。`R >= 127` を white、それ未満を black として扱い、処理対象の angle sequence で `black -> white -> black` や `white -> black -> white` のような複数回 transition が生まれないようにします。

![QuickSDF Monotonic Guard flow]({{ '/images/quick-sdf-guard-flow.png' | relative_url }})

- quick toggle 上の表示名は `Guard`、shortcut は `Alt + 8` です。
- `Clip Direction` の既定は `Auto` です。`0-90` degrees では `White Expands`、`90-180` degrees では `White Shrinks` として扱います。
- 通常 brush と Quick Stroke は、undo transaction が確定する前に silent に clip されます。
- antialiased stroke edge も stroke intent として扱うため、`127` の binary threshold をまたがない場合も clip 対象になります。
- `Current / All / Before / After` は、どの mask へ stroke を書き込むかを決めます。
- imported masks、rebaked masks、SDF generation は自動修正されません。既存違反は validation または Guard 有効状態での SDF generation 時に warning として確認できます。

## Mask I/O と SDF Generation

- Mask import は file picker と timeline drag-and-drop に対応します。
- Mask export により、外部編集や review ができます。
- 作業状態は非破壊の `UQuickSDFAsset` に保存され、必要に応じて mask texture も保存できます。
- SDF generation は CPU processing で、Monopolar / Bipolar packing、optional 1x-8x upscaling、R/A/B/G output swizzling、half-float texture export に対応します。
- `Generated SDF` は保存済みの最終 texture を表示します。`Live SDF` は transient GPU JFA approximation の表示で、最終出力としては保存されません。
- generated maps は既定で `/Game/QuickSDF_GENERATED/` に保存されます。
- shader integration の詳細は [Material Setup]({{ '/ja/material-setup/' | relative_url }}) を参照してください。

## 必要なスクリーンショット

次の画像は生成図ではなく、実際の UE editor session から撮影してください。

- Select mode の active material slot overlay。
- Paint mode Screen Projection の緑ブラシ円、UV preview、active slot context。
- Timeline thumbnails / seek lane / keyframe lane / status badges。
- Generated SDF preview。
- 2D Canvas でペン描画中、ブラシ円と stroke position が一致している画面。既存 GIF は残し、静止 PNG も追加。
- 3D Screen mode で pen hover 中の緑ブラシ円。
- Quick Stroke が 2D Canvas で hold 後に追従している画面。
- Quick Stroke が 3D Paint で軽く追従している画面。
- Advanced の **Pen Pressure** / **Pen Pressure Curve**。
- 任意: `Ctrl + F` ペン brush resize 中の画面。入力修正の説明用にあると便利です。
