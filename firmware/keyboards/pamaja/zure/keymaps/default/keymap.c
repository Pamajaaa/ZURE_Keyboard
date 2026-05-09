#include QMK_KEYBOARD_H
#include "print.h"
#include "joystick.h"
#include "analog.h"
#include "timer.h"

// ===== スティック設定 =====
// 観測されたADC範囲（実機のスティックごとに要調整）
// 左スティック: 観測 950..1023
#define LEFT_X_MIN   950
#define LEFT_X_MID   987
#define LEFT_X_MAX  1023
#define LEFT_Y_MIN   950
#define LEFT_Y_MID   987
#define LEFT_Y_MAX  1023
// 右スティック: 観測 0..1023（フルレンジ）
#define RIGHT_X_MIN     0
#define RIGHT_X_MID   512
#define RIGHT_X_MAX  1023
#define RIGHT_Y_MIN     0
#define RIGHT_Y_MID   512
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
};

// HIDゲームパッド軸：VIRTUALにしてhousekeeping内で完全制御
// （AUTO読み取りを無効にして通常モード時の干渉を防ぐ）
joystick_config_t joystick_axes[JOYSTICK_AXIS_COUNT] = {
    JOYSTICK_AXIS_VIRTUAL,  // 左 X (GP26)
    JOYSTICK_AXIS_VIRTUAL,  // 左 Y (GP27)
    JOYSTICK_AXIS_VIRTUAL,  // 右 X (GP28)
    JOYSTICK_AXIS_VIRTUAL,  // 右 Y (GP29)
};

// ADC値をHID軸値(-127..127)にマッピング
static int8_t map_axis_value(int16_t raw, int16_t min_v, int16_t mid_v, int16_t max_v) {
    if (raw <= mid_v) {
        int16_t range = mid_v - min_v;
        if (range == 0) return 0;
        int32_t result = -(int32_t)(mid_v - raw) * 127 / range;
        return (int8_t)(result < -127 ? -127 : result);
    } else {
        int16_t range = max_v - mid_v;
        if (range == 0) return 0;
        int32_t result = (int32_t)(raw - mid_v) * 127 / range;
        return (int8_t)(result > 127 ? 127 : result);
    }
}

// 左スティック円形デッドゾーン半径（-127..127スケール、約12%）
#define LEFT_DEADZONE 15

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
    return pgm_read_word(&keymaps[layer][row][col]);
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
} axis_t;

static axis_t axes[] = {
    // 左X
    { GP26, LEFT_X_MID,  30, 10, 1, 1, 1, 3, DIR_NEUTRAL, KC_NO },
    // 左Y
    { GP27, LEFT_Y_MID,  30, 10, 0, 2, 2, 2, DIR_NEUTRAL, KC_NO },
    // 右X
    { GP28, RIGHT_X_MID, 50, 20, 1, 6, 1, 8, DIR_NEUTRAL, KC_NO },
    // 右Y
    { GP29, RIGHT_Y_MID, 50, 20, 0, 7, 2, 7, DIR_NEUTRAL, KC_NO },
};

#define AXES_COUNT (sizeof(axes) / sizeof(axes[0]))

static void stick_press(uint16_t kc) {
    if (kc == KC_NO) return;
    register_code16(kc);
}

static void stick_release(uint16_t kc) {
    if (kc == KC_NO) return;
    unregister_code16(kc);
}

static void release_all_stick_keys(void) {
    for (uint8_t i = 0; i < AXES_COUNT; i++) {
        if (axes[i].active_kc != KC_NO) {
            stick_release(axes[i].active_kc);
            axes[i].active_kc = KC_NO;
        }
        axes[i].state = DIR_NEUTRAL;
    }
}

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

    if (target == ax->state) return;

    if (ax->active_kc != KC_NO) {
        stick_release(ax->active_kc);
        ax->active_kc = KC_NO;
    }

    if (target == DIR_NEG) {
        uint16_t kc = lookup_keycode(ax->neg_row, ax->neg_col);
        stick_press(kc);
        ax->active_kc = kc;
    } else if (target == DIR_POS) {
        uint16_t kc = lookup_keycode(ax->pos_row, ax->pos_col);
        stick_press(kc);
        ax->active_kc = kc;
    }
    ax->state = target;
}

void housekeeping_task_user(void) {
    static uint32_t last = 0;
    if (timer_elapsed32(last) < 10) return;  // 10ms間隔
    last = timer_read32();

    if (game_mode) {
        // ゲームモード：ADCを読んでHID軸として出力
        int32_t ax[4] = {
            map_axis_value(analogReadPin(GP26), LEFT_X_MIN,  LEFT_X_MID,  LEFT_X_MAX),
            map_axis_value(analogReadPin(GP27), LEFT_Y_MIN,  LEFT_Y_MID,  LEFT_Y_MAX),
            map_axis_value(analogReadPin(GP28), RIGHT_X_MIN, RIGHT_X_MID, RIGHT_X_MAX),
            map_axis_value(analogReadPin(GP29), RIGHT_Y_MIN, RIGHT_Y_MID, RIGHT_Y_MAX),
        };

        // 各スティックをラジアルクランプ：斜め方向の速度超過を補正
        // X・Y独立マッピングだと斜め45°で合成長≈180になるため127に正規化する
        for (uint8_t s = 0; s < 2; s++) {
            int32_t x = ax[s * 2];
            int32_t y = ax[s * 2 + 1];
            int32_t mag_sq = x * x + y * y;
            if (mag_sq > (int32_t)127 * 127) {
                // Babylonian法で整数sqrt（3回で十分収束）
                int32_t mag = 127;
                mag = (mag + mag_sq / mag) >> 1;
                mag = (mag + mag_sq / mag) >> 1;
                mag = (mag + mag_sq / mag) >> 1;
                ax[s * 2]     = x * 127 / mag;
                ax[s * 2 + 1] = y * 127 / mag;
            }
        }

        // 左スティック円形デッドゾーン（8方向スナップなし）
        int32_t dist_sq = ax[0] * ax[0] + ax[1] * ax[1];
        if (dist_sq < (int32_t)LEFT_DEADZONE * LEFT_DEADZONE) {
            ax[0] = 0;
            ax[1] = 0;
        }

        for (uint8_t i = 0; i < 4; i++) {
            joystick_set_axis(i, (int8_t)ax[i]);
        }
        joystick_flush();
        return;
    }

    // 通常モード：HID軸を全て0に固定（x360ce干渉防止）
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
//   GAME_TOGGLE (QK_KB_0): ON/OFF をトグル（旧仕様、互換用）
//   GAME_ON     (QK_KB_1): ゲームモードON 専用（既にONなら何もしない）
//   GAME_OFF    (QK_KB_2): ゲームモードOFF 専用（既にOFFなら何もしない）
// 例: Vial Combos タブで V+U+- → GAME_ON、X+C+L → GAME_OFF を登録

enum custom_keycodes {
    GAME_TOGGLE = QK_KB_0,
    GAME_ON     = QK_KB_1,
    GAME_OFF    = QK_KB_2,
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
        case GAME_ON:
            set_game_mode(true);
            return false;
        case GAME_OFF:
            set_game_mode(false);
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
