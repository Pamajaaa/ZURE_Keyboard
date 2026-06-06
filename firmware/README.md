# ZURE Keyboard

RP2040 ベースの自作キーボード「zure」のファームウェア (QMK + Vial)。
デュアルアナログスティック搭載。x360ce を介してゲームパッドとして使用可能。

## 構成

- **MCU**: RP2040
- **Matrix**: 3 行 × 10 列
- **アナログスティック**: 2 本（GP26-GP29 の ADC）
- **ボタン**: 4
- **HID**: ジョイスティック (Usage 0x04) + キーボード

## ファイル構成

```
keyboards/pamaja/zure/
├── config.h              # スティック軸/コンボなどの設定
├── keyboard.json         # マトリクスピン、レイアウト定義
├── rules.mk              # ビルドフラグ (JOYSTICK_ENABLE 等)
├── zure.c                # ボード定義 (空)
└── keymaps/
    ├── default/
    │   ├── keymap.c      # メインのキーマップ + スティック制御ロジック
    │   └── vial.json     # Vial 用レイアウト
    └── vial/
        ├── config.h      # Vial 用 (UID, dynamic layers, combos)
        ├── keymap.c      # default をインクルード
        ├── rules.mk      # VIAL_ENABLE
        └── vial.json     # Vial 用カスタムキーコード定義

tmk_core/protocol/
├── usb_descriptor.c      # QMK 本体改造: HID Usage を設定可能化
└── vusb/vusb.c           # 同上 (V-USB 用)

pamaja_zure_vial.uf2      # ビルド済みファームウェア
```

## ビルド方法

QMK MSYS で:

```bash
qmk compile -kb pamaja/zure -km vial
```

`pamaja_zure_vial.uf2` が `qmk_firmware/` に生成される。

## フラッシュ方法

1. キーボードを BOOTSEL モード（リセットボタン押しながら接続）にする
2. `RPI-RP2` ドライブが現れる
3. `pamaja_zure_vial.uf2` をドラッグ＆ドロップ

## キーマップ概要

- **Layer 0 (Base)**: COLEMAK風レイアウト
- **Layer 1**: モディファイア (Ctrl/Shift/MO)
- **Layer 2**: 矢印・記号
- **Layer 3**: 数字・記号
- **Layer 4**: ゲームモード時のレイヤー

## スティック動作

### 通常モード
スティックを倒すと Layer 1 の特定キー（Ctrl/Shift/Space/Enter等）が押される。
HID ジョイスティック軸は常に 0 に固定（x360ce 等の干渉防止）。

### ゲームモード (`GAME_TOGGLE` / `GAME_ON` / `GAME_OFF`)
スティックを HID ジョイスティック軸として直接出力。x360ce で Xbox 360 コントローラとして使用可能。
- 円形デッドゾーン適用
- ラジアルクランプで斜め方向の速度を均一化

## tmk_core 改造について

QMK 本体の `tmk_core/protocol/usb_descriptor.c` と `vusb/vusb.c` で、HID コレクションの
`Usage` を `JOYSTICK_COLLECTION_USAGE` マクロから取れるように変更している。
Joystick (0x04) ↔ Gamepad (0x05) を切り替えたい場合に `config.h` で再定義可能。
