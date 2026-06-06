"""
Input Bridge for Zure Keyboard Visualizer
==========================================
グローバルキーボード入力 + XInputゲームパッド入力を WebSocket で配信します。
visualizer.html が ws://localhost:8765 に接続して受信します。

依存: pip install pynput websockets

使い方:
  1. このスクリプトを起動 (start_bridge.bat または `python input_bridge.py`)
  2. visualizer.html をブラウザ/OBSで開く
  3. ブリッジが起動している間、ブラウザがバックグラウンドでも入力が反映される
"""

import asyncio
import ctypes
import json
import sys
import threading
import time
from ctypes import wintypes

try:
    import websockets
except ImportError:
    print("ERROR: 'websockets' がインストールされていません。次を実行してください:")
    print("  pip install websockets pynput")
    sys.exit(1)

try:
    from pynput import keyboard
except ImportError:
    print("ERROR: 'pynput' がインストールされていません。次を実行してください:")
    print("  pip install websockets pynput")
    sys.exit(1)


# ============================================================
# XInput (Windows ゲームパッド)
# ============================================================
class XINPUT_GAMEPAD(ctypes.Structure):
    _fields_ = [
        ("wButtons",      ctypes.c_ushort),
        ("bLeftTrigger",  ctypes.c_ubyte),
        ("bRightTrigger", ctypes.c_ubyte),
        ("sThumbLX",      ctypes.c_short),
        ("sThumbLY",      ctypes.c_short),
        ("sThumbRX",      ctypes.c_short),
        ("sThumbRY",      ctypes.c_short),
    ]


class XINPUT_STATE(ctypes.Structure):
    _fields_ = [
        ("dwPacketNumber", wintypes.DWORD),
        ("Gamepad",        XINPUT_GAMEPAD),
    ]


_xinput = None
for _dll in ("XInput1_4", "XInput1_3", "xinput9_1_0"):
    try:
        _xinput = ctypes.windll.LoadLibrary(_dll)
        break
    except OSError:
        continue

if _xinput is None:
    print("WARNING: XInput DLL が見つかりません。ゲームパッドは無効化されます。")


def read_gamepad(index: int = 0):
    """XInput からスティック値を取得。接続なしなら None。"""
    if _xinput is None:
        return None
    state = XINPUT_STATE()
    if _xinput.XInputGetState(index, ctypes.byref(state)) != 0:
        return None

    def norm(v):
        n = v / 32767.0
        return max(-1.0, min(1.0, n))

    return {
        "lx": norm(state.Gamepad.sThumbLX),
        # XInputは上が+、Web Gamepad APIは下が+ なので反転
        "ly": norm(-state.Gamepad.sThumbLY),
        "rx": norm(state.Gamepad.sThumbRX),
        "ry": norm(-state.Gamepad.sThumbRY),
    }


# ============================================================
# pynput キー -> JavaScript event.code マッピング
# ============================================================
# VK code 0x41..0x5A は 'A'..'Z' に直接対応
# OEM キーは US 配列の VK code
VK_OEM_TO_CODE = {
    0xBD: "Minus",      # -
    0xBE: "Period",     # .
    0xBF: "Slash",      # /
    0xBC: "Comma",      # ,
    0xBA: "Semicolon",
    0xDE: "Quote",
    0xDB: "BracketLeft",
    0xDD: "BracketRight",
    0xDC: "Backslash",
    0xC0: "Backquote",
    0xBB: "Equal",
}

# pynput.Key の特殊キー -> event.code
def _opt(name):
    """pynput のバージョンによって存在しない属性を安全に取得"""
    return getattr(keyboard.Key, name, None)

SPECIAL_KEY_TO_CODE = {
    keyboard.Key.space:        "Space",
    keyboard.Key.enter:        "Enter",
    keyboard.Key.tab:          "Tab",
    keyboard.Key.backspace:    "Backspace",
    keyboard.Key.esc:          "Escape",
    keyboard.Key.delete:       "Delete",
    keyboard.Key.up:           "ArrowUp",
    keyboard.Key.down:         "ArrowDown",
    keyboard.Key.left:         "ArrowLeft",
    keyboard.Key.right:        "ArrowRight",
    keyboard.Key.home:         "Home",
    keyboard.Key.end:          "End",
    keyboard.Key.shift:        "ShiftLeft",
    keyboard.Key.shift_l:      "ShiftLeft",
    keyboard.Key.shift_r:      "ShiftRight",
    keyboard.Key.ctrl:         "ControlLeft",
    keyboard.Key.ctrl_l:       "ControlLeft",
    keyboard.Key.ctrl_r:       "ControlRight",
    keyboard.Key.alt:          "AltLeft",
    keyboard.Key.alt_l:        "AltLeft",
    keyboard.Key.alt_gr:       "AltRight",
    keyboard.Key.cmd:          "MetaLeft",
}

# Windows キー (cmd_l / cmd_r) は pynput のバージョンにより属性名が異なる
for _name, _code in [
    ("cmd_l", "MetaLeft"),
    ("cmd_r", "MetaRight"),
    ("alt_r", "AltRight"),
]:
    _k = _opt(_name)
    if _k is not None:
        SPECIAL_KEY_TO_CODE[_k] = _code

# F1-F24 を pynput Key enum でもマップ (バージョン非依存)
for _i in range(1, 25):
    _fk = _opt(f"f{_i}")
    if _fk is not None:
        SPECIAL_KEY_TO_CODE[_fk] = f"F{_i}"

# Windows VK code フォールバック
VK_DIRECT_TO_CODE = {
    0x5B: "MetaLeft",     # VK_LWIN
    0x5C: "MetaRight",    # VK_RWIN
    0xA0: "ShiftLeft",    # VK_LSHIFT
    0xA1: "ShiftRight",   # VK_RSHIFT
    0xA2: "ControlLeft",  # VK_LCONTROL
    0xA3: "ControlRight", # VK_RCONTROL
    0xA4: "AltLeft",      # VK_LMENU
    0xA5: "AltRight",     # VK_RMENU
}

# Japanese IME 用 VK code (コンボ出力検出用)
VK_JAPANESE_TO_CODE = {
    0x1C: "Convert",       # 変換
    0x1D: "NonConvert",    # 無変換
    0xF2: "KanaMode",      # ひらがな/カタカナ
    0xF3: "Lang2",         # 英数 (環境依存)
    0xF4: "Lang1",         # かな (環境依存)
}


def key_to_code(key):
    # 特殊キー (pynput Key enum)
    if key in SPECIAL_KEY_TO_CODE:
        return SPECIAL_KEY_TO_CODE[key]

    # vk 属性を使う (レイアウト非依存)
    vk = getattr(key, "vk", None)
    if vk is None:
        return None

    # Win/Shift/Ctrl/Alt の L/R (pynput が enum で渡さない場合のフォールバック)
    if vk in VK_DIRECT_TO_CODE:
        return VK_DIRECT_TO_CODE[vk]

    # A-Z
    if 0x41 <= vk <= 0x5A:
        return "Key" + chr(vk)

    # 0-9 (上段)
    if 0x30 <= vk <= 0x39:
        return "Digit" + chr(vk)

    # 0-9 (テンキー)
    if 0x60 <= vk <= 0x69:
        return "Numpad" + str(vk - 0x60)

    # F1-F24 (VK 0x70..0x87)
    if 0x70 <= vk <= 0x87:
        return f"F{vk - 0x70 + 1}"

    # OEM キー
    if vk in VK_OEM_TO_CODE:
        return VK_OEM_TO_CODE[vk]

    # 日本語キー
    if vk in VK_JAPANESE_TO_CODE:
        return VK_JAPANESE_TO_CODE[vk]

    # 不明な VK はそのまま "VK0xNN" として送る (Debug 表示用)
    return f"VK0x{vk:02X}"


# ============================================================
# WebSocket サーバー
# ============================================================
clients = set()
event_loop = None
last_keys = {}  # code -> bool (押下中) — 自動リピート抑制用


def schedule_broadcast(payload: dict):
    """別スレッドから asyncio loop に broadcast をスケジュール"""
    if event_loop is None:
        return
    asyncio.run_coroutine_threadsafe(broadcast(payload), event_loop)


async def broadcast(payload: dict):
    if not clients:
        return
    msg = json.dumps(payload)
    dead = []
    for ws in clients:
        try:
            await ws.send(msg)
        except Exception:
            dead.append(ws)
    for ws in dead:
        clients.discard(ws)


def on_press(key):
    code = key_to_code(key)
    if not code:
        return
    if last_keys.get(code):
        return  # 既に押下中(自動リピート抑制)
    last_keys[code] = True
    schedule_broadcast({"type": "keydown", "code": code})


def on_release(key):
    code = key_to_code(key)
    if not code:
        return
    last_keys[code] = False
    schedule_broadcast({"type": "keyup", "code": code})


async def gamepad_loop():
    """60Hz でゲームパッド状態を読み出して送信"""
    last_sent = None
    last_connected = False
    while True:
        state = read_gamepad(0)
        if state is None:
            if last_connected:
                await broadcast({"type": "gamepad", "connected": False})
                last_connected = False
        else:
            payload = {
                "type": "gamepad",
                "connected": True,
                "lx": round(state["lx"], 4),
                "ly": round(state["ly"], 4),
                "rx": round(state["rx"], 4),
                "ry": round(state["ry"], 4),
            }
            # 同値スキップ(帯域節約)
            if payload != last_sent:
                await broadcast(payload)
                last_sent = payload
                last_connected = True
        await asyncio.sleep(1 / 60)


async def ws_handler(websocket):
    clients.add(websocket)
    print(f"[+] client connected ({len(clients)} total)")
    try:
        # 初回 hello
        await websocket.send(json.dumps({"type": "hello"}))
        await websocket.wait_closed()
    except Exception:
        pass
    finally:
        clients.discard(websocket)
        print(f"[-] client disconnected ({len(clients)} total)")


async def main():
    global event_loop
    event_loop = asyncio.get_running_loop()

    # キーボードリスナーを別スレッドで起動
    listener = keyboard.Listener(on_press=on_press, on_release=on_release)
    listener.daemon = True
    listener.start()

    # ゲームパッドポーリングタスク
    asyncio.create_task(gamepad_loop())

    host, port = "localhost", 8765
    print("=" * 60)
    print(" Zure Keyboard Visualizer - Input Bridge")
    print("=" * 60)
    print(f" WebSocket: ws://{host}:{port}")
    print(f" Keyboard:  global hook ENABLED")
    print(f" Gamepad:   {'ENABLED (XInput)' if _xinput else 'DISABLED'}")
    print("")
    print(" visualizer.html を OBS/ブラウザで開いてください。")
    print(" 停止するには Ctrl+C")
    print("=" * 60)

    async with websockets.serve(ws_handler, host, port):
        await asyncio.Future()  # run forever


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n停止しました。")
