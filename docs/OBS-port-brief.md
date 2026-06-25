# Rain-on-Glass → OBS ネイティブフィルタ 移植ブリーフ

このドキュメントは、完成した WebGL プロトタイプ `rain-on-glass-v4.html` を
**OBS Studio のネイティブ映像フィルタプラグイン**として正確に再実装するための指示書です。
Claude Code には **このブリーフと HTML の両方** を渡してください。

- HTML = 「正確な仕様書」。アルゴリズム・定数・シェーダ数式・最終チューニング値がすべて入っています。ゼロから作り直さず、**移植**してください。
- 本ブリーフ = HTML に書かれていない OBS 固有の設計判断（下記）を埋めるもの。

---

## 0. 結論・推奨ルート

| ルート | 可否 | 備考 |
|---|---|---|
| **ネイティブ filter プラグイン (C/C++ + libobs)** | ✅ 推奨 | 正確な再現が可能。本ブリーフの対象。 |
| obs-shaderfilter / StreamFX カスタムシェーダ | ❌ 不可 | 単一パスのフラグメントシェーダのみ。CPU粒子シム・複数RT・動的ジオメトリ・フレーム間状態を扱えない。 |
| ブラウザソース (このHTMLをそのまま) | ⭕ 手軽な代替 | CEFで動く。ネイティブが要らないならこれが最短。`u_bg` を実シーンにはできず背景画像は固定。 |

ターゲット環境（2026年6月時点で確認済み）:
- OBS Studio **32.2.x**
- 公式テンプレート: `https://github.com/obsproject/obs-plugintemplate`（CMake + buildspec.json + CI 同梱）
- フィルタは `obs_source_info` を `OBS_SOURCE_TYPE_FILTER` / `OBS_SOURCE_VIDEO | OBS_SOURCE_CUSTOM_DRAW` で登録。

---

## 1. 全体構造の対応（HTML → プラグイン）

| HTML 側 | プラグイン側 |
|---|---|
| `step(dt, P)`（粒子シム） | `static void rg_tick(void *data, float seconds)` = `obs_source_info.video_tick` |
| `draw(time, P)`（描画） | `static void rg_render(void *data, gs_effect_t *)` = `obs_source_info.video_render` |
| `P`（パラメータ群） | フィルタの data 構造体メンバ + `obs_properties` / `update` で反映 |
| `drops[] / orphans[]` 配列 | data 構造体に保持（フレーム間で生存）。`create` で確保、`destroy` で解放 |
| `u_bg`（背景テクスチャ） | **フィルタの親ソースのレンダリング結果**。背景画像は不要になる |
| キャンバスサイズ W,H | フィルタ対象の幅・高さ（`obs_source_get_base_width/height(target)`） |

`step` と `draw` をそのまま `video_tick` / `video_render` に割り当てるのが基本方針です。
JS の数値計算（`vnoise`, `dropRadius`, stick-slip, 合体, trail/orphan 管理）は **そのまま C に移植**できます。乱数は `rand()` ではなく一貫した PRNG（例: xorshift）を用意してください。

---

## 2. レンダリングパイプライン（filter, multi-pass）

WebGL では「背景RT → 高さ場RT → 合成」の3段でした。OBS filter では `u_bg` が親ソースになります。

`video_render` の流れ（疑似コード）:

```
target = obs_filter_get_target(filter);
cx = obs_source_get_base_width(target);  cy = ...height...;

// (A) 親ソースを自前の texrender に描いて u_bg テクスチャを得る
gs_texrender_reset(bgTR);
if (gs_texrender_begin(bgTR, cx, cy)) {
    // 透過/座標を整えて
    obs_source_video_render(target);   // 親をこの RT に描く
    gs_texrender_end(bgTR);
}
gs_texture_t *bg = gs_texrender_get_texture(bgTR);

// (B) 高さ場を RGBA16F の texrender に描く（drops + ribbon）
//     ※ MAX 合成の扱いは §3 を参照
gs_texrender_reset(heightTR);  // GS_RGBA16F
if (gs_texrender_begin(heightTR, cx, cy)) {
    clear(0);
    draw_ribbons();   // CPU構築の動的頂点バッファ
    draw_splats();    // CPU構築の動的頂点バッファ（インスタンシング不可: §4）
    gs_texrender_end(heightTR);
}
gs_texture_t *height = gs_texrender_get_texture(heightTR);

// (C) 合成パス：bg と height をサンプルして最終出力
//     FS_COMP 相当の .effect を使い、出力先は現在のRT（フィルタ出力）
set effect params: image=bg, u_h=height, u_refr, u_fog, u_debug, u_res
gs_draw_sprite or full-screen tri
```

`obs_source_process_filter_begin/end` を使う素直なフィルタもありますが、本件は複数RTと自前ジオメトリが必要なので
**自前 texrender + 最後に合成スプライトを現在のRTへ描く**形が扱いやすいです（`OBS_SOURCE_CUSTOM_DRAW`）。

---

## 3. 最重要：MAX ブレンドが libobs に無い問題

直近で「軌跡を雨粒の手前に出さない」ために高さ場合成を **加算→MAX** に変えました。
しかし **libobs の graphics API には blend equation（FUNC_ADD/MIN/MAX）の設定が存在しません**。
`gs_blend_function` / `gs_blend_function_separate`（src/dst 係数）だけで、`gs_blend_op` 相当は非公開です。

→ そのままでは `gl.blendEquation(gl.MAX)` を再現できません。以下のいずれかで**必ず**対応してください。

**方法A（推奨・確実）: グループ別RT + シェーダで max()**
- 高さ場を 2 枚に分ける：`heightDrops`（スプラット）と `heightTrail`（リボン）。
- 各RT内は加算でOK（リボンはセグメント共有なので加算でもほぼムラ無し）。
- 合成シェーダで `float H = max(hDrop, hTrail);` を計算。これで「粒が軌跡の手前」を完全再現。
- 欠点：粒どうしの重なりは加算に戻る（密集コアがやや明るい）。気になる場合のみ方法B併用。

**方法B（完全再現）: 深度テストで per-pixel MAX**
- 高さ場RTに深度バッファを付け、ピクセルシェーダで `H` を深度（SV_Depth）に書き、深度比較 GREATER で描画。
- 最も高い `H` のフラグメントだけが残り、ハードウェアMAXと同義になる（drops も trail も全部一括でmax）。
- 要確認：OBS の `.effect`（HLSL）で `SV_Depth` 出力が通るか。通ればこれが最も忠実。

まずは**方法A**で実装し、密集時の見えが気になったら方法Bへ、という順序を推奨します。
（HTMLの現状は「全部まとめてMAX」。方法Aは「粒群 vs 軌跡群」の境界だけMAX、粒内は加算、という近似です。）

---

## 4. 次点の制約：インスタンシングが無い

WebGL では `drawArraysInstanced` で最大4000個のスプラットを描いていますが、
**libobs の gs API にインスタンス描画はありません**。

→ スプラットも**リボンと同じ方式**にしてください：
- CPU 側で各ドロップを 1 クアッド（2三角形 = 6頂点）に展開し、
  進行方向 `dir` と `stretch`、`perp` を使って頂点位置を計算（VS_SPLAT のロジックをCPUへ）。
- 1フレーム分を 1 本の動的頂点バッファに詰めて `gs_draw` 一回で描画。
- 頂点属性：position(px) と、フラグメントで必要な `v_local(corner)`, `v_seed`, `v_peak`, `v_stretch`, `v_phase`。
  → これらは頂点ごとに複製して持たせる（インスタンス属性 → 頂点属性へ降格）。
- `gs_vertexbuffer_create(..., GS_DYNAMIC)` を確保し、毎フレーム `gs_vertexbuffer_flush` で更新。

リボンは既に「動的頂点バッファをCPU構築」しているので、その実装をそのまま流用できます。

---

## 5. シェーダ移植（GLSL ES 300 → OBS .effect / HLSL）

- 4本のシェーダを `.effect`（D3D11 系 HLSL、OBS が GLSL へクロスコンパイル）に書き換え：
  `FS_BG` は不要（背景は親ソース=`u_bg`に置換）。残りは
  `VS/FS_SPLAT`（高さ場・スプラット）、`VS/FS_RIBBON`（高さ場・リボン）、`FS_COMP`（合成）。
- 数式はそのまま移植可能（`smoothstep`, `pow`, `length`, `atan` 等は HLSL にもある。`atan(y,x)`→`atan2(y,x)`）。
- **座標系の注意**：OBS/D3D は左上原点・UVのv下向き。WebGL の y 反転（`1.0 - py/u_res.y*...`）と
  refraction のオフセット符号を、最終出力が上下正しくなるよう合わせること（デバッグ表示Dで高さ場を確認）。
- 高さ場RTは **GS_RGBA16F**（`extF ? RGBA16F : RGBA8` 相当）。赤チャンネルのみ使用。
- 合成の屈折は `texture(u_bg, uv ± n.xy*…)`。`u_bg` は親ソースなので、ここが実シーンに屈折がかかる本命挙動。
- `FS_COMP` 内のブラー（5x5）・法線（有限差分）・スペキュラー・フレネル・edge 減光・mask は数式そのまま。

---

## 6. パラメータ → obs_properties

チューニング値の**正本は HTML 上の「設定をコピー」出力**です（スライダー値）。
スライダー→内部値の換算は HTML の `bind(...)` 内の式に書かれています。
プラグインでは remap を持たず、**内部値を直接プロパティとして公開**するのが簡潔です。

| 内部パラメータ | 換算式（slider x=0..N → 値） | プロパティ型 / 範囲 |
|---|---|---|
| spawn（plain）/sec | `= x` | float/int 0–120 |
| spawnTrail /sec | `= x` | 0–120 |
| spawnStatic /sec | `= x` | 0–120 |
| dropSize（初期半径基準）| `0.2 + x*0.118` | float 0.2–12.0 |
| dropVar | `x/100` | 0–1 |
| **mergeGain**（合体時の取り込み率）| `x/100` | 0–1（大玉化を抑える主要ツマミ）|
| refr | `x/1000*1.6` | float |
| fog | `x/100` | 0–1 |
| grav（flow speed）| `80 + x*4.0` | float |
| meander | `x*2.0` | float |
| stopGoProb | `x/100` | 0–1 |
| sgFreq | `x/100` | 0–1 |
| sgDecel | `1 + x*0.14` | float |
| maxStretch | `1 + x/100*1.5` | 1.0–2.5 |
| wobble（roundness）| `1 - x/100` | 0–1 |
| trailLife（秒）| `1 + x*0.35` | float |
| trailWidth（半幅係数）| `0.2 + x/100*1.3` | 0.2–1.5 |
| trailMode | beads / line / both | enum（list） |

ユーザーが「仕上がった」と判断した最新スクショの目安値：
spawn plain≈80, trail≈87, static≈85, drop size≈0.4, variance 0%, refraction 73, fog 57,
flow 100, meander 18, stop&go apply 50% / freq 25%, decel 40, max stretch 1.38, roundness 65%,
trail life 20s, trail width 0.62, 軌跡=両方。
（merge gain はスクショ後に追加。大玉が気になる場合 30–50% から調整する想定。）

内部の固定定数（UIに無いがHTMLにある）も忘れず移植：
`P.grow=1.0, P.evap=0.22, GSC=0.02, PIN=0.13, CELL=42, TRAIL_FADE=0.7,`
最小半径クランプ `Math.max(0.2, r)`、消滅しきい値 `r<0.15`、trail消滅 `r<2.1` 等。

---

## 7. CPU シミュレーション移植メモ

- `video_tick(dt)`：HTML の `step(dt, P)` 相当。`simTime += dt`、3系統スポーン、グリッド再構築、
  各ドロップの sliding/stuck/braking・成長・蒸発・合体・trail記録・orphan化・cull。
  - 空間グリッド（`CELL=42`、ハッシュ）も移植して合体判定を O(n)。
  - `pinned`（static）は成長せず・滑らず・吸収/蒸発で入れ替わる。
  - 死亡時に `path` を `orphans` へ退避し `TRAIL_FADE` 秒で全体フェード。
- `video_render`：§2 のパイプライン。頂点バッファ構築（リボン+スプラット）→ 高さ場 → 合成。
- 可変フレームレート対策：`dt = min(dt, 1/30)` のクランプは残す。
- クリック等の入力は OBS では基本不要（プレビューのマウスは取れるが必須でない）。

---

## 8. Claude Code が着手前に確認すべきこと

1. `.effect` で `SV_Depth` 出力が使えるか（§3 方法B を採るなら）。不可なら方法A。
2. `GS_RGBA16F` への描画と、その上での加算ブレンドが動くか（半精度floatブレンドは通常OK）。
3. `gs_vertexbuffer` を毎フレーム `flush` する更新コスト（数千頂点×2系統）。問題あれば容量を事前確保し使い回す。
4. フィルタが親ソースを `obs_source_video_render` で texrender に描く際の透過・色空間（`begin_with_color_space`）の扱い。
5. 2系統RT（方法A）のVRAMと、合成での `max()` 合算が意図通りか（高さ場デバッグ表示を最初に実装すると早い）。

## 9. 既知の落とし穴

- MAX非対応（§3）と非インスタンシング（§4）が二大ハマりどころ。最初にここを設計で潰す。
- 座標系・y反転・屈折符号（§5）。最初に高さ場デバッグ（Dキー相当）を出して確認。
- 高密度時の頂点数：plain/trail/static を高く＋trail両方だと頂点が膨らむ。上限（MAX=4000, MAXRV）を移植し超過cull維持。
- `mergeGain` を入れた今、合体の大玉化はこのツマミで制御。移植時に式 `cbrt(r1^3 + mergeGain*r2^3)` を正確に。
```
