#pragma once

// HIDゲームパッド軸（両スティック）
#define JOYSTICK_AXIS_COUNT 4
#define JOYSTICK_BUTTON_COUNT 4
#define JOYSTICK_AXIS_RESOLUTION 10
#define JOYSTICK_COLLECTION_USAGE 0x04

#define JOYSTICK_AXIS_PIN_A GP26  // Left stick X
#define JOYSTICK_AXIS_PIN_B GP27  // Left stick Y
#define JOYSTICK_AXIS_PIN_C GP28  // Right stick X
#define JOYSTICK_AXIS_PIN_D GP29  // Right stick Y

// コンボ（ゲームモードトグル用）
// VIAL_COMBO_ENTRIES は keymaps/vial/config.h 側で定義
#define COMBO_TERM 80
