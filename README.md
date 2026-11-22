# Screen Effect Tools

> **📥 最新版のダウンロード：[release.zip（Windows x64）](https://github.com/503bad/scene_effect_tools/releases/latest/download/release.zip)**
> ／ [リリースページ（更新内容）](https://github.com/503bad/scene_effect_tools/releases/latest)
>
> 展開した `install.bat` をダブルクリックするとインストールできます（詳しくは「[OBS へのインストール](#obs-へのインストール)」）。

OBS Studio 用の映像エフェクト・フィルタープラグインです。映像ソースに右クリック →
「フィルタ」から追加できる10種類のエフェクトフィルターを提供します。各フィルターは
独立しており、自由に重ねがけ（スタック）・順序入替できます（例：ドット絵 → グリッチ）。

## OBS へのインストール

### インストーラーを使う（推奨）

1. OBS を完全に終了します。
2. ZIP をすべて展開し、`install.bat` を**ダブルクリック**します。管理者権限の確認（UAC）が出たら「はい」を選ぶと、新しいウィンドウでインストールが進みます。

`install.bat` は次のことを自動で行います。

- `C:\ProgramData\obs-studio\plugins\screen-effect-tools\` に、DLL を
  **OBS 33 以降の置き方と OBS 32 以前の置き方の両方**で配置します。OBS のバージョンに
  関係なく読み込まれ、32 から 33 へ更新したあともそのまま使えます
  （OBS 33 は新しい置き方を先に読み込み、もう一方は読み飛ばします）。
  - `...\screen-effect-tools\screen-effect-tools.dll`（OBS 33 以降）
  - `...\screen-effect-tools\bin\64bit\screen-effect-tools.dll`（OBS 32 以前）
  - `...\screen-effect-tools\data\`
- OBS の場所（レジストリ → `C:\Program Files\obs-studio`）は、古いコピーの削除と
  ポータブル版の判定にだけ使います。見つからなくてもインストールは続けます。
  - **ポータブル版 OBS 33 以降**は `install.bat "D:\Apps\obs-studio"` のように
    OBS のフォルダを指定してください（`<OBS のフォルダ>\plugins\screen-effect-tools\` に入ります。
    ポータブル版 OBS 32 以前はインストーラーの対象外です）。
- 以前のバージョンが旧方式の場所（`<OBS>\obs-plugins\64bit\` と
  `<OBS>\data\obs-plugins\screen-effect-tools\`）にあれば削除します。
  旧方式の場所は OBS 34 以降で読み込まれなくなる予定です。
- 以前のバージョンの不要なファイル（古いエフェクトなど）も削除します。

インストールのたびに、`install.bat` と同じフォルダへ `install-log.txt`（見つけた OBS・
インストール先・PC にあるプラグインの DLL の一覧）を書き出します。うまく動かないときは
このファイルと OBS のログを添えてお知らせください。`install.bat /check` を実行すると、
何も変更せずにインストール先などを画面に表示します（管理者権限は不要です）。

### 手動でインストールする

OBS を終了し、ZIP の中の `screen-effect-tools` フォルダを次の場所にフォルダごと
コピーします。フォルダ名は必ず `screen-effect-tools`（DLL のベース名と一致）にします。

- `C:\ProgramData\obs-studio\plugins\` の中へそのままコピーし、さらに
  `screen-effect-tools.dll` と `screen-effect-tools.pdb` を `screen-effect-tools\bin\64bit\`
  にもコピーします（両方に置くと OBS 32 / 33 のどちらでも読み込まれます）

以前に `C:\Program Files\obs-studio\obs-plugins\64bit\screen-effect-tools.dll` と
`C:\Program Files\obs-studio\data\obs-plugins\screen-effect-tools\` に入れていた
場合は、それらを削除してください。

## エフェクト一覧

| フィルター | 概要 |
|---|---|
| カートゥーン (Cartoon) | 輪郭線の付与＋色の階調化（ポスタライズ）＋平滑化＋彩度ブースト |
| セピア (Sepia) | 輝度をトーンカラーのグラデーションへマッピング。コントラスト・ビネット付き |
| 古い映画 (Old Film) | セピア＋グレイン＋流れる縦キズ＋ホコリ＋明滅＋フレーム揺れ |
| VHS | 色のにじみ・ノイズ・走査線・トラッキング帯・スライスグリッチ・彩度低下 |
| ドット絵 (Pixel Art) | モザイク化＋色数（16/256/フルカラー）。16色時はパレット＋順序ディザリング |
| ブルーム (Bloom) | しきい値ベースの明部抽出 → ダウンサンプル＋ブラー → 加算合成 |
| フォーカス (Focus) | 起点からの距離に応じた可変ぼかし。ぼけカーブ調整・円/楕円対応 |
| ガビガビ (Crunch) | 低解像度化＋ブロック量子化＋クロマサブサンプリング＋色深度低下＋ブロックノイズ |
| インスタ風色補正 (Instagram Grade) | プリセット（ティール&オレンジ等）＋パラメトリック微調整（WB/スプリットトーン/フェード等） |
| グリッチ (Glitch) | スライス変位＋色ずれ＋明部ブルーム＋走査線＋カラーオーバーレイの統合 |
| 雨 (Rain) | ガラス窓を流れる雨だれ。CPU粒子シミュレーション＋高さ場による親シーンの屈折・軌跡・合体 |

## 共通仕様

- 全エフェクトに「強度 / ミックス」スライダー（0〜100%）。`0%` で原映像へ完全復帰します。
- パラメータは「基本」「詳細」にグループ分け。初見では基本だけ触れば十分です。
- 重い処理（ブルーム・フォーカス）には品質（低/中/高）を用意。
- 解像度依存パラメータは UV/解像度比で正規化し、1080p/4K で見た目を一貫させます。
- ラベルはエンドユーザー向け日本語（`data/locale/ja-JP.ini`）。

## ビルド

Windows (Visual Studio 2022 / 2026 + CMake):

```
build.bat            ビルド（インクリメンタル）→ package\ 生成 → OBS へ配置
build.bat clean      build_x64\ と package\ を消してフルビルド
```

初回 configure 時に `buildspec.json` 記載の OBS ソース／prebuilt deps を `.deps\` へ
ダウンロードします。OBS への配置は `package\install.bat` で `C:\ProgramData\obs-studio\plugins\` に
書き込むため、管理者権限のコマンドプロンプトから実行してください。

## 既知の制限

- インスタ風色補正の LUT 読み込み（`.cube`）は UI のみ用意しており、適用処理は未実装です
  （パラメトリック調整は全て動作します）。
- カートゥーンの「ひずみ」はセル画調（色数削減＋輪郭線）として実装しています
  （spec.md §12 の確認事項参照）。
