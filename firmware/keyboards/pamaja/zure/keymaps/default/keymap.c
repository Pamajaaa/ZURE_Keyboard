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

typedef enum {
    MODE_TYPING = 0,
    MODE_WASD,
    MODE_ANALOG,
    MODE_COUNT
} stick_mode_t;

static stick_mode_t current_mode = MODE_TYPING;
static stick_mode_t previous_game_mode = MODE_COUNT; // チャット自動切替からの復帰用
static void set_stick_mode(stick_mode_t mode);

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = {
        { KC_Q, KC_V, KC_U, KC_MINS, KC_NO, KC_K, KC_W, KC_R, KC_Y, KC_P },
        { KC_E, KC_I, KC_A, KC_O, KC_SLASH, MC_0, KC_T, KC_N, KC_S, KC_H },
        { KC_Z, KC_X, KC_C, KC_L, KC_F, KC_G, KC_D, KC_M, KC_J, KC_B }
    },
    [1] = {
        { _______, _______, MO(3), _______, _______, _______, _______, KC_COMMA, _______, _______ },
        { _______, KC_LCTL, _______, MO(2), _______, _______, KC_SPC, _______, KC_DOT, _______ },
        { _______, _______, KC_LSFT, _______, _______, _______, _______, KC_LSFT, _______, _______ }
    },
    [2] = {
        { KC_ESCAPE, KC_HOME, KC_UP, KC_END, KC_NO, KC_QUOTE, LSFT(KC_LBRC), LSFT(KC_SLASH), LSFT(KC_1), LSFT(KC_RBRC) },
        { KC_HOME, KC_LEFT, KC_DOWN, KC_RIGHT, KC_LGUI, KC_DELETE, LSFT(KC_9), KC_BSPC, KC_ENT, LSFT(KC_0) },
        { KC_F8, MC_1, MC_2, KC_NO, _______, LSFT(KC_QUOTE), KC_LBRC, LSFT(KC_MINS), LSFT(KC_SCLN), KC_RBRC }
    },
    [3] = {
        { KC_ESCAPE, KC_7, KC_8, KC_9, KC_MINS, LSFT(KC_GRAVE), LSFT(KC_2), LSFT(KC_5), LSFT(KC_6), LSFT(KC_7) },
        { KC_0, KC_1, KC_2, KC_3, KC_SPC, LSFT(KC_4), LSFT(KC_SCLN), KC_BSPC, KC_ENT, LSFT(KC_3) },
        { KC_TAB, KC_4, KC_5, KC_6, KC_DOT, KC_EQL, LSFT(KC_EQL), KC_MINS, LSFT(KC_8), KC_SLSH }
    },
    [4] = {
        { KC_LALT, KC_T, KC_G, KC_M, KC_R, KC_TAB, KC_U, KC_I, KC_O, KC_P },
        { KC_LSFT, KC_Q, KC_SPC, KC_E, LT(6, KC_V), KC_ENT, KC_J, KC_K, KC_L, KC_DELETE },
        { KC_Z, KC_X, KC_C, KC_LCTL, KC_F, KC_B, KC_ESCAPE, KC_N, KC_M, MC_2 }
    },
    [5] = {
        { _______, _______, KC_W, _______, _______, _______, _______, _______, _______, _______ },
        { _______, KC_A, _______, KC_D, _______, _______, _______, _______, _______, _______ },
        { _______, _______, KC_S, _______, _______, _______, _______, _______, _______, _______ }
    },
    [6] = {
        { _______, KC_7, KC_8, KC_9, _______, _______, _______, _______, _______, _______ },
        { _______, KC_1, KC_2, KC_3, _______, _______, _______, _______, _______, _______ },
        { _______, KC_4, KC_5, KC_6, _______, _______, _______, _______, _______, _______ }
    },
    [7] = {
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ },
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ },
        { _______, _______, _______, _______, _______, _______, _______, _______, _______, _______ }
    }
};

// HIDゲームパッド軸：VIRTUALにしてhousekeeping内で完全制御
joystick_config_t joystick_axes[JOYSTICK_AXIS_COUNT] = {
    JOYSTICK_AXIS_VIRTUAL,  // Axis 0: 左 X (GP28)
    JOYSTICK_AXIS_VIRTUAL,  // Axis 1: 左 Y (GP29)
    JOYSTICK_AXIS_VIRTUAL,  // Axis 2: 右 X (GP27)
    JOYSTICK_AXIS_VIRTUAL,  // Axis 3: 右 Y (GP26)
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
    uint8_t layer = (current_mode == MODE_WASD) ? STICK_KEY_LAYER_GAME : STICK_KEY_LAYER_NORMAL;
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
    axis_state_t pending_target;  // デバウンス中の候補方向
    uint8_t debounce_count;       // 候補方向が連続した回数
} axis_t;

static axis_t axes[] = {
    // 左スティック：物理X軸 = GP28、物理Y軸 = GP29
    // 物理X軸（左右） = GP28：左=ADC+→[1,1] LCTRL  右=ADC-→[1,3] MO(2)
    { GP28, LEFT_X_MID, 120, 60, 1, 3, 1, 1, DIR_NEUTRAL, KC_NO, false, 0, DIR_NEUTRAL, 0 },
    // 物理Y軸（上下） = GP29：下=ADC+→[2,2] LSHIFT  上=ADC-→[0,2] MO(3)
    { GP29, LEFT_Y_MID, 120, 60, 0, 2, 2, 2, DIR_NEUTRAL, KC_NO, false, 0, DIR_NEUTRAL, 0 },
    // 右スティック：物理X軸 = GP27、物理Y軸 = GP26
    // tap_only=false: 倒している間ホールド（Shift/レイヤー切替などを保持できる）
    // GP27（物理上下）：上=ADC+→[0,7]「、」、下=ADC-→[2,7] Shift
    { GP27, RIGHT_X_MID, 150, 100, 2, 7, 0, 7, DIR_NEUTRAL, KC_NO, false, 0, DIR_NEUTRAL, 0 },
    // GP26（物理左右）：左=ADC-→[1,6] Space、右=ADC+→[1,8]「。」
    { GP26, RIGHT_Y_MID, 150, 100, 1, 6, 1, 8, DIR_NEUTRAL, KC_NO, false, 0, DIR_NEUTRAL, 0 },
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
        axes[i].pending_target = DIR_NEUTRAL;
        axes[i].debounce_count = 0;
    }
}

// tap_only モード用：タップ後にスティックが少し戻ったら「再武装」する距離
// （メカニカル・ディテントで中心まで戻れないスティック対策）
#define TAP_REARM_DISTANCE 50

// 発火デバウンス：方向判定が連続このサンプル数続いたら押す
#define ENTER_DEBOUNCE 2

// 解除デバウンス：中立判定が連続このサンプル数続いたら離す
#define RELEASE_DEBOUNCE 3

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
        ax->debounce_count = 0;  // 方向を維持中なのでリセット
        ax->pending_target = ax->state;
        return;
    }

    // デバウンス：状態遷移は target が連続確認できたときだけ確定する。
    // ・発火（中立→方向）：ENTER_DEBOUNCE 回連続 → 一瞬のスパイクで誤発火しない
    // ・解除（方向→中立）：RELEASE_DEBOUNCE 回連続 → 一瞬のジャンプで離れない
    if (target == ax->pending_target) {
        if (ax->debounce_count < 255) ax->debounce_count++;
    } else {
        ax->pending_target = target;
        ax->debounce_count = 1;
    }
    uint8_t threshold = (target == DIR_NEUTRAL) ? RELEASE_DEBOUNCE : ENTER_DEBOUNCE;
    if (ax->debounce_count < threshold) {
        return;  // まだ確定しない
    }
    ax->debounce_count = 0;

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

static int8_t map_axis(int16_t raw, int16_t min_val, int16_t mid_val, int16_t max_val) {
    if (raw < mid_val) {
        int32_t val = (int32_t)(raw - mid_val) * 127 / (mid_val - min_val);
        if (val < -127) val = -127;
        return (int8_t)val;
    } else {
        int32_t val = (int32_t)(raw - mid_val) * 127 / (max_val - mid_val);
        if (val > 127) val = 127;
        return (int8_t)val;
    }
}

// 両スティックを1秒間同じ方向に倒したか判定してモード切替
static bool check_stick_mode_switch(int16_t a28, int16_t a29, int16_t a27, int16_t a26) {
    static uint32_t hold_timer = 0;
    static stick_mode_t target_mode = MODE_COUNT;

    // 左スティック：物理Y軸(上下)=GP29 (上=ADC-, 下=ADC+)、物理X軸(左右)=GP28 (左=ADC+, 右=ADC-)
    // 右スティック：物理上下=GP27 (上=ADC+, 下=ADC-)、物理左右=GP26 (左=ADC-, 右=ADC+)
    bool left_up    = a29 < (LEFT_Y_MID - 300);
    bool right_up   = a27 > (RIGHT_X_MID + 300);
    
    bool left_down  = a29 > (LEFT_Y_MID + 300);
    bool right_down = a27 < (RIGHT_X_MID - 300);
    
    bool left_left   = a28 > (LEFT_X_MID + 300);
    bool right_right = a26 > (RIGHT_Y_MID + 300);

    stick_mode_t detected = MODE_COUNT;
    
    if (left_up && right_up) {
        detected = MODE_TYPING;
    } else if (left_down && right_down) {
        detected = MODE_WASD;
    } else if (left_left && right_right) {
        detected = MODE_ANALOG;
    }
    
    if (detected != MODE_COUNT) {
        if (target_mode != detected) {
            target_mode = detected;
            hold_timer = timer_read32();
        } else {
            if (timer_elapsed32(hold_timer) > 500) { // 0.5秒間保持
                previous_game_mode = MODE_COUNT; // 手動切替が行われたら自動復帰をキャンセル
                set_stick_mode(detected);
            }
        }
        return true; // コンボ入力中
    } else {
        target_mode = MODE_COUNT;
        return false;
    }
}

void housekeeping_task_user(void) {
    static uint32_t last = 0;
    if (timer_elapsed32(last) < 2) return;  // 2ms間隔
    last = timer_read32();

    int16_t a28 = analogReadPin(GP28); // 左X
    int16_t a29 = analogReadPin(GP29); // 左Y
    int16_t a27 = analogReadPin(GP27); // 右 上下
    int16_t a26 = analogReadPin(GP26); // 右 左右

    // スティックホールドによるモード切替判定
    bool combo_active = check_stick_mode_switch(a28, a29, a27, a26);

    // デバッグ：左スティック(GP28/GP29)の値を出力
    {
        int16_t d28 = a28 - LEFT_X_MID; if (d28 < 0) d28 = -d28;
        int16_t d29 = a29 - LEFT_Y_MID; if (d29 < 0) d29 = -d29;
        if (d28 > 150 || d29 > 150) {
            // uprintf("LSTICK move GP28=%d GP29=%d\n", a28, a29);
        }
    }

    // デバッグ：右スティック(GP26/GP27)の値を出力
    {
        int16_t d26 = a26 - RIGHT_Y_MID; if (d26 < 0) d26 = -d26;
        int16_t d27 = a27 - RIGHT_X_MID; if (d27 < 0) d27 = -d27;
        if (d26 > 150 || d27 > 150) {
            // uprintf("RSTICK move GP26=%d GP27=%d\n", a26, a27);
        }
    }

    if (current_mode == MODE_ANALOG) {
        // アナログモード：スティックADCを読み取ってゲームパッドの軸として出力
        joystick_set_axis(0, map_axis(a28, LEFT_X_MIN, LEFT_X_MID, LEFT_X_MAX)); // 左X
        joystick_set_axis(1, map_axis(a29, LEFT_Y_MIN, LEFT_Y_MID, LEFT_Y_MAX)); // 左Y
        joystick_set_axis(2, map_axis(a27, RIGHT_X_MIN, RIGHT_X_MID, RIGHT_X_MAX)); // 右X
        joystick_set_axis(3, map_axis(a26, RIGHT_Y_MIN, RIGHT_Y_MID, RIGHT_Y_MAX)); // 右Y
        joystick_flush();
    } else {
        // HIDジョイスティック軸は常に0に固定
        for (uint8_t i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
            joystick_set_axis(i, 0);
        }
        joystick_flush();

        if (combo_active) {
            // モード切替の同時押し中は、スティック入力を即座にキャンセルして文字の連続送信を防ぐ
            release_all_stick_keys();
        } else {
            // 通常のスティック→キー変換
            for (uint8_t i = 0; i < AXES_COUNT; i++) {
                axis_update(&axes[i]);
            }
        }
    }
}

static void set_stick_mode(stick_mode_t mode) {
    if (current_mode == mode) return;
    release_all_stick_keys();
    current_mode = mode;

    if (current_mode == MODE_WASD || current_mode == MODE_ANALOG) {
        default_layer_set(1UL << 4);
    } else {
        default_layer_set(1UL << 0);
    }

    const char *mode_str = "";
    if (current_mode == MODE_TYPING) mode_str = "TYPING";
    if (current_mode == MODE_WASD)   mode_str = "WASD (Game)";
    if (current_mode == MODE_ANALOG) mode_str = "ANALOG JOYSTICK";
    
    uprintf("Stick mode changed to: %s\n", mode_str);
}

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    if (record->event.pressed) {
        if (keycode == KC_ENT) {
            if (current_mode == MODE_WASD || current_mode == MODE_ANALOG) {
                // ゲームモード中にEnterが押されたら、チャットを開くと同時にタイピングモードへ自動切替
                previous_game_mode = current_mode;
                set_stick_mode(MODE_TYPING);
            } else if (current_mode == MODE_TYPING && previous_game_mode != MODE_COUNT) {
                // 自動切替でタイピングモード中の場合、Enter（送信）で元のゲームモードに戻る
                set_stick_mode(previous_game_mode);
                previous_game_mode = MODE_COUNT;
            }
        } else if (keycode == KC_ESCAPE) {
            if (current_mode == MODE_TYPING && previous_game_mode != MODE_COUNT) {
                // 自動切替でタイピングモード中の場合、Esc（キャンセル）で元のゲームモードに戻る
                set_stick_mode(previous_game_mode);
                previous_game_mode = MODE_COUNT;
            }
        }
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
