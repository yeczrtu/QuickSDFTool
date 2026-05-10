---
title: Troubleshooting
description: QuickSDFTool の install、build、material、pen、2D Canvas、Quick Stroke、brush behavior に関する問題の切り分け。
permalink: /ja/troubleshooting/
lang: ja
alternate_url: /troubleshooting/
alternate_label: English
---

# Troubleshooting

## Plugin が Editor に表示されない

- project が C++ project で、plugin copy 後に rebuild されていることを確認します。
- plugin folder が `YourProject/Plugins/QuickSDFTool/` であることを確認します。
- `QuickSDFTool.uplugin` がその folder の直下にあることを確認します。
- project files を再生成し、Visual Studio または通常の UE build workflow で rebuild します。

## 生成した影が反転して見える

- まず project material の comparison direction を確認し、painted masks と結果が一致しているか見ます。
- Monopolar / Bipolar のどちらの output path を使っているか確認し、[Material Setup]({{ '/ja/material-setup/' | relative_url }}) の R/A/B/G output layout に従います。
- built-in Generated SDF preview は saved texture と同じ exported threshold-map layout を使います。built-in preview は正しいのに project material だけ反転する場合、project shader の comparison direction または light-angle mapping がずれている可能性が高いです。

## 一部の Mesh で Brush Size が合わない

UV density が大きく変わる mesh では brush size が不均一に見えることがあります。

- まず clean で均等な UV の mesh で確認します。
- precise texture-space edit には 2D Canvas を使います。
- heavily stretched UV islands だけで brush behavior を判断しないでください。この項目は roadmap 上の改善対象です。

## 液タブ / Tablet 入力がずれて見える

Windows では、QuickSDF は 3D Paint と 2D Canvas の pen pointer coordinates を直接読みます。editor または canvas window を移動 / resize した後も、hover、stroke start、drag、release、pressure-sensitive radius、Quick Stroke、`Ctrl + F` brush resizing が正しく追従する想定です。

- pressure-sensitive radius を期待する場合は **Advanced > Pen Pressure** が enabled であることを確認します。固定 radius が必要な場合は disabled にします。
- radius response が弱すぎる / 強すぎる場合は **Advanced > Pen Pressure Curve** を調整します。`1.0` は linear、値を上げると light pressure が細く保たれ、値を下げると軽い筆圧でも太くなりやすくなります。
- 2D Canvas では **Fit** と **100%** の両方で確認します。visible brush circle と painted stroke が一致するべきです。
- 3D Paint では Screen と Surface projection の両方を確認します。mouse と pen は同じ mesh、material slot、timeline angle で比較してください。
- report する場合は tablet/display model、driver version、Windows version、monitor layout、各 monitor の DPI scale、UE version、QuickSDFTool commit または release tag、Screen / Surface / 2D Canvas / hover / drag / release / pressure / Quick Stroke / `Ctrl + F` resize のどこで起きるかを含めてください。

## 2D Canvas 後に 3D の Brush Preview Circle が消える

緑の 3D brush circle は、Paint mode が active paint target 上で valid viewport hit を持っている間だけ表示されます。

- pointer を level viewport に戻し、**Paint** mode が維持されていることを確認します。
- target mesh と active material slot がまだ選択されていることを確認します。Select mode では cyan active-slot overlay、Paint mode では active slot context が目印です。
- pointer が active slot surface 上にあることを確認します。non-target slots は active slot の hit を奪わないよう filter されます。
- 2D Canvas 側の menu や modal window が pointer input を capture していないか確認します。
- pen だけで起きる場合は、pen を contact から離し、3D viewport 上で再度 hover します。同じ場所で mouse hover と比較します。
- 緑円が戻らない場合は、`F` focus が動くか、preview がなくても stroke は塗れるか、angle 変更または Paint mode 再入場で復帰するかを記録してください。

## Quick Stroke が重い / Pen に追従しない

Quick Stroke は hold 後に開始し、release で確定します。preview 移動中は、高頻度 tablet input が毎 frame の重い stroke work にならないよう、preview update を軽量化しています。

- quick toggle menu または **Advanced** settings で **Quick Stroke** が enabled であることを確認します。
- **Quick Stroke Hold Time** まで十分に hold し、activation 前に **Quick Stroke Move Tolerance** を超えて動かさないようにします。
- activation 後、pen または mouse を動かし、意図した final endpoint で release します。intermediate preview updates が throttle されても、committed stroke は release position を使うべきです。
- 同じ target で mouse と pen を比較します。mouse は軽いが pen は重い場合、tablet driver と Windows Ink settings を report に含めてください。
- **Live SDF Preview Resolution** を一時的に下げる、または **Live SDF** 以外へ切り替え、preview cost と Quick Stroke cost を切り分けます。
- report する場合は、2D Canvas、3D Screen mode、3D Surface mode、または全 mode のどこで起きるか、mesh type、texture resolution、paint target mode、Live SDF setting、Monotonic Guard / Symmetry の enabled state を含めてください。

## GPU JFA は Live SDF Preview 専用

`Live SDF` は responsive in-editor feedback のために GPU JFA approximation を使います。保存される `Generated SDF` output は引き続き CPU `FSDFProcessor` path を使います。

- painting 中の形状確認には `Live SDF` を使います。
- final output quality、channel packing、UV island mirror behavior、lilToon-compatible output、upscaled output の検証には `Generated SDF` または saved texture を使います。
- live preview が粗い場合は **Advanced > Live SDF Preview Resolution** を `512 px` から `1024 px` に上げます。editor input が重い場合は `256 px` または `128 px` に下げます。

## UE 5.4 / 5.5 / 5.6 Build Issues

QuickSDFTool v1.0 は UE 5.7.x を support し、UE 5.7.4 を required release verification target としています。古い version では editor tooling、modeling components、material baking、shader module setup まわりの source edit が必要になる場合があります。UE 5.8+ は対応予定ですが、v1.0 release verification matrix には含まれていません。

compatibility issue を report する場合は、次を含めてください。

- Unreal Engine version。
- Compiler version。
- Full build error。
- QuickSDFTool commit または release tag。
