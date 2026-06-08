#include QMK_KEYBOARD_H
#include "print.h"
#include "joystick.h"
#include "analog.h"
#include "timer.h"

// ===== スティック設定 =====
// 実測した ADC 範囲（コンソール出力で観測）
// 左スティック: GND接続を応急修理してフルレンジになった
#define LEFT_X_MIN     2
#define LEFT_X_MID   512
#define LEFT_X_MAX  1023
#define LEFT_Y_MIN     2
#define LEFT_Y_MID   512
#define LEFT_Y_MAX  1023
// 右スティック: 観測 2..1023、idle が中心 512 ではなく 472〜496 付近
#define RIGHT_X_MIN     2
#define RIGHT_X_MID   496
#define RIGHT_X_MAX  1023
#define RIGHT_Y_MIN     2
#define RIGHT_Y_MID   472
#define RIGHT_Y_MAX  1023

#define STICK_KEY_LAYER_NORMAL 1
#define STICK_KEY_LAYER_GAME   5

static bool game_mode = false;

// ===================================================================
// キーマップ
// ===================================================================
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = {
        { KC_Q,    KC_V,    KC_U,    KC_MINS, KC_DOT,  KC_K,    KC_W,    KC_R,    KC_Y,    KC_P    },
        { KC_E,    KC_I,    KC_A,    KC_O,    KC_SLSH, KC_F,    KC_T,    KC_N,    KC_S,    KC_H    },
        { KC_Z,    KC_X,    KC_C,    KC_L,    KC_COMM, KC_G,    KC_D,    KC_M,    KC_J,    KC_B    }
    },
    [1] = {
        { _______, _______, MO(3),   _______, _______, _______, _______, KC_ENT,  _______, _______ },
        { _______, KC_LCTL, _______, MO(2),   _______, _______, KC_SPC,  _______, _______, _______ },
        { _______, _______, KC_LSFT, _______, _______, _______, _______, _______, _______, _______ }
    },
    [2] = {
        { KC_ESC,  _______, KC_UP,   _______, _______, _______, LSFT(KC_QUOT), LSFT(KC_SLSH), LSFT(KC_1), KC_QUOT },
        { KC_END,  KC_LEFT, KC_DOWN, KC_RGHT, KC_NO,   _______, LSFT(KC_9),    KC_BSPC,      KC_ENT,     LSFT(KC_0) },
        { _______, MC_2,    MC_1,    KC_HOME, _______, _______, KC_LBRC,        LSFT(KC_MINS), LSFT(KC_SCLN), KC_RBRC }
    },
    [3] = {
        { KC_ESC, KC_7, KC_8, KC_9, KC_BSPC, LSFT(KC_GRV), LSFT(KC_2), LSFT(KC_5), LSFT(KC_6), LSFT(KC_7) },
        { KC_0,   KC_1, KC_2, KC_3, KC_DOT,  LSFT(KC_4),   KC_DOT,     KC_BSPC,    KC_ENT,     LSFT(KC_3) },
        { KC_TAB, KC_4, KC_5, KC_6, KC_ENT,  KC_EQL,       LSFT(KC_EQL), KC_MINS,  LSFT(KC_8), KC_SLSH }
    },
    [4] = {
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ },
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ },
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ }
    },
    // Layer 5: ゲームモード時のスティック→キー
    // 左スティック: 上=W、下=S、左=A、右=D（WASD）
    // 右スティック: TRNS（Vialで自由に設定）
    [5] = {
        { _______, _______, KC_W,    _______, _______, _______, _______, _______, _______, _______ },
        { _______, KC_A,    _______, KC_D,    _______, _______, _______, _______, _______, _______ },
        { _______, _______, KC_S,    _______, _______, _______, _______, _______, _______, _______ }
    },
};

// HIDゲームパッド軸：VIRTUALにしてhousekeeping内で完全制御
// （AUTO読み取りを無効にして通常モード時の干渉を防ぐ）
joystick_config_t joystick_axes[JOYSTICK_AXIS_COUNT] = {
    JOYSTICK_AXIS_VIRTUAL,  // 左 X (GP26)
    JOYSTICK_AXIS_VIRTUAL,  // 左 Y (GP27)
    JOYSTICK_AXIS_VIRTUAL,  // 右 X (GP28)
    JOYSTICK_AXIS_VIRTUAL,  // 右 Y (GP29)
};

// HID joystick は使わないので map_axis_value / LEFT_DEADZONE は削除済み

// ===================================================================
// スティック → キー変換
// ===================================================================
// 通常モード：スティックを傾けると、Layer STICK_KEY_LAYER_NORMAL のキーを発火。
// ゲームモード（GAME_TOGGLE）：Layer STICK_KEY_LAYER_GAME を参照（WASD等）。
//
// 方向 → マトリクス位置マッピング：
//   左スティック：  上=[0,2] 下=[2,2] 左=[1,1] 右=[1,3]
//   右スティック：  上=[0,7] 下=[2,7] 左=[1,6] 右=[1,8]
// 各レイヤーの対応位置を編集すれば割り当てキーを変更できます（Vial可）。

static uint16_t lookup_keycode(uint8_t row, uint8_t col) {
    uint8_t layer = game_mode ? STICK_KEY_LAYER_GAME : STICK_KEY_LAYER_NORMAL;
    // Vial 動的キーマップ（EEPROM）に対応：Vial UI で編集した値がスティックに反映される
    return keymap_key_to_keycode(layer, (keypos_t){.row = row, .col = col});
}

typedef enum { DIR_NEUTRAL, DIR_NEG, DIR_POS } axis_state_t;

typedef struct {
    pin_t pin;
    int16_t center;
    int16_t enter_th;
    int16_t exit_th;
    uint8_t neg_row, neg_col;
    uint8_t pos_row, pos_col;
    axis_state_t state;
    uint16_t active_kc;
    bool tap_only;     // true: 修飾キーも含めて全て1回タップ
    int16_t tap_adc;   // tap_only モード用：タップ時のADC値（戻り検出用）
    uint8_t release_count;  // 解除デバウンス用：中立判定が連続した回数
} axis_t;

static axis_t axes[] = {
    // 左スティック：基板を90°回転して取り付けているため GP26↔GP27 を入れ替え
    // GND修理でフルレンジになったので閾値を大きくする (enter=200, exit=100)
    // 物理X軸（左右） = GP27：左=ADC-→[1,1] LCTRL  右=ADC+→[1,3] MO(2)
    { GP27, LEFT_X_MID, 200, 100, 1, 1, 1, 3, DIR_NEUTRAL, KC_NO, false, 0, 0 },
    // 物理Y軸（上下） = GP26：下=ADC-→[2,2] LSHIFT  上=ADC+→[0,2] MO(3)
    { GP26, LEFT_Y_MID, 200, 100, 2, 2, 0, 2, DIR_NEUTRAL, KC_NO, false, 0, 0 },
    // 右スティック：基板を90°回転 (左スティックとは逆方向) + 配線極性も逆
    // tap_only=false: 倒している間ホールド（Shift/レイヤー切替などを保持できる）
    // 物理X軸 = GP29：右=ADC-→[1,8]、左=ADC+→[1,6]
    { GP29, RIGHT_X_MID, 250, 180, 1, 8, 1, 6, DIR_NEUTRAL, KC_NO, false, 0, 0 },
    // 物理Y軸 = GP28：上=ADC-→[0,7]、下=ADC+→[2,7]
    { GP28, RIGHT_Y_MID, 250, 180, 0, 7, 2, 7, DIR_NEUTRAL, KC_NO, false, 0, 0 },
};

#define AXES_COUNT (sizeof(axes) / sizeof(axes[0]))

static void stick_press(uint16_t kc, bool tap_only) {
    if (kc == KC_NO || kc == KC_TRANSPARENT) return;
    // MO(layer): モーメンタリレイヤー切替（押しっぱなしで保持）
    if (IS_QK_MOMENTARY(kc)) {
        layer_on(QK_MOMENTARY_GET_LAYER(kc));
        return;
    }
    // Alt (LALT/RALT) は tap_only でも常に押しっぱなし
    if (kc == KC_LALT || kc == KC_RALT) {
        register_code16(kc);
        return;
    }
    // tap_only: その他の修飾キー含め全て1回タップ
    if (tap_only) {
        tap_code16(kc);
        return;
    }
    // tap_only=false: 修飾キー・英字キー・全て押しっぱなし保持
    // （WASD移動など、スティックを倒している間ずっと押した状態にする）
    register_code16(kc);
}

static void stick_release(uint16_t kc, bool tap_only) {
    if (kc == KC_NO || kc == KC_TRANSPARENT) return;
    if (IS_QK_MOMENTARY(kc)) {
        layer_off(QK_MOMENTARY_GET_LAYER(kc));
        return;
    }
    // Alt は tap_only でも常に解放処理が必要
    if (kc == KC_LALT || kc == KC_RALT) {
        unregister_code16(kc);
        return;
    }
    // tap_only モードはタップで完結しているので離す処理は不要
    if (tap_only) return;
    // tap_only=false: 押し続けたキーを解放
    unregister_code16(kc);
}

static void release_all_stick_keys(void) {
    for (uint8_t i = 0; i < AXES_COUNT; i++) {
        if (axes[i].active_kc != KC_NO) {
            stick_release(axes[i].active_kc, axes[i].tap_only);
            axes[i].active_kc = KC_NO;
        }
        axes[i].state = DIR_NEUTRAL;
    }
}

// tap_only モード用：タップ後にスティックが少し戻ったら「再武装」する距離
// （メカニカル・ディテントで中心まで戻れないスティック対策）
#define TAP_REARM_DISTANCE 50

// ホールド軸の解除デバウンス：中立判定が連続このサンプル数続いたら離す（10ms間隔）
// ADCが一瞬中心に飛んでも Shift 等が断続しないようにする。5 = 50ms。
#define RELEASE_DEBOUNCE 5

static void axis_update(axis_t *ax) {
    int16_t raw = analogReadPin(ax->pin);
    int16_t delta = raw - ax->center;
    int16_t adelta = delta < 0 ? -delta : delta;

    axis_state_t target = ax->state;
    if (ax->state == DIR_NEUTRAL) {
        if (adelta >= ax->enter_th) {
            target = (delta < 0) ? DIR_NEG : DIR_POS;
        }
    } else {
        if (adelta <= ax->exit_th) {
            target = DIR_NEUTRAL;
        } else {
            axis_state_t desired = (delta < 0) ? DIR_NEG : DIR_POS;
            if (desired != ax->state && adelta >= ax->enter_th) {
                target = desired;
            }
        }
    }

    // tap_only モードの追加判定：
    // スティックがタップ時の位置から「戻る方向」に TAP_REARM_DISTANCE 以上動いたら
    // 中立扱いに戻して次のタップを受け付け可能にする
    if (ax->tap_only && target == ax->state) {
        if (ax->state == DIR_POS && raw < ax->tap_adc - TAP_REARM_DISTANCE) {
            target = DIR_NEUTRAL;
        } else if (ax->state == DIR_NEG && raw > ax->tap_adc + TAP_REARM_DISTANCE) {
            target = DIR_NEUTRAL;
        }
    }

    if (target == ax->state) {
        ax->release_count = 0;  // 方向を維持中なのでリセット
        return;
    }

    // 解除（方向→中立）のデバウンス：
    // ホールド中にスティックのADCが一瞬中心に飛んでも即解除しない。
    // 中立判定が RELEASE_DEBOUNCE 回連続したときだけ本当に離す。
    // （tap_only モードは即時解除のままにする）
    if (!ax->tap_only && target == DIR_NEUTRAL) {
        if (ax->release_count < RELEASE_DEBOUNCE) {
            ax->release_count++;
            return;  // まだ離さない（一瞬のジャンプの可能性）
        }
    }
    ax->release_count = 0;

    if (ax->active_kc != KC_NO) {
        stick_release(ax->active_kc, ax->tap_only);
        ax->active_kc = KC_NO;
    }

    if (target == DIR_NEG) {
        uint16_t kc = lookup_keycode(ax->neg_row, ax->neg_col);
        stick_press(kc, ax->tap_only);
        ax->active_kc = kc;
        ax->tap_adc = raw;  // タップ時のADCを記録
    } else if (target == DIR_POS) {
        uint16_t kc = lookup_keycode(ax->pos_row, ax->pos_col);
        stick_press(kc, ax->tap_only);
        ax->active_kc = kc;
        ax->tap_adc = raw;  // タップ時のADCを記録
    }
    ax->state = target;
}

void housekeeping_task_user(void) {
    static uint32_t last = 0;
    if (timer_elapsed32(last) < 10) return;  // 10ms間隔
    last = timer_read32();

    // HIDジョイスティック軸は常に0に固定（キーボード動作のみ使用）
    // ゲームモード時もスティック→キー変換で動作させる
    for (uint8_t i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
        joystick_set_axis(i, 0);
    }
    joystick_flush();

    // スティック→キー変換
    for (uint8_t i = 0; i < AXES_COUNT; i++) {
        axis_update(&axes[i]);
    }
}

// ===================================================================
// ゲームモード切替
// ===================================================================
// Vialはcomboを独自管理するため、firmware内combo定義は不可。
// カスタムキーコードをVialのキー/コンボに割り当ててください：
//   GAME_TOGGLE (QK_KB_0): ON/OFF をトグル
// 例: Vial Combos タブで V+U+- → GAME_TOGGLE を登録

enum custom_keycodes {
    GAME_TOGGLE = QK_KB_0,
};

// ゲームモードを指定状態に設定（変化がなければ何もしない）
static void set_game_mode(bool on) {
    if (game_mode == on) return;
    release_all_stick_keys();
    game_mode = on;
    default_layer_set(game_mode ? (1UL << 4) : (1UL << 0));
    uprintf("Game mode: %s\n", game_mode ? "ON" : "OFF");
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (!record->event.pressed) return true;
    switch (keycode) {
        case GAME_TOGGLE:
            set_game_mode(!game_mode);
            return false;
    }
    return true;
}

void keyboard_post_init_user(void) {
    debug_enable  = true;
    debug_matrix  = false;
    debug_keyboard = false;
    debug_mouse    = false;

    // ADCピンをアナログ入力モードに明示設定
    // （JOYSTICK_AXIS_VIRTUAL を使うとQMKジョイスティックドライバが
    //  自動的にこの設定をしてくれないため必要）
    palSetLineMode(GP26, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(GP27, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(GP28, PAL_MODE_INPUT_ANALOG);
    palSetLineMode(GP29, PAL_MODE_INPUT_ANALOG);

    uprintf("ZURE KEYMAP LOADED\n");
}
