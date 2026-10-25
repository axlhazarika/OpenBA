# Android: the mouse, pointer capture and SDL's Java glue

Facts for the Android build (S25, Android 15 / API 35, SDL 3.4.18 pinned in `third_party/SDL`).

## How a mouse reaches the game

- Uncaptured, a mouse's events arrive at `SDLSurface.onTouch` (button down / up and dragged motion,
  `TOOL_TYPE_MOUSE`, `getButtonState()`) and at the generic-motion listener (`ACTION_HOVER_MOVE`,
  `ACTION_SCROLL`). Both call `SDLActivity.onNativeMouse(buttonState, action, x, y, relative)`;
  `SDL_androidmouse.c` `Android_OnMouse` turns `ACTION_DOWN` / `ACTION_UP` into the button whose bit
  changed against the state it last saw, `ACTION_MOVE` / `ACTION_HOVER_MOVE` into motion.
- The game reads buttons through `SDL_GetGlobalMouseState` (`src/platform/sdl/input_sdl.cpp`,
  `ReadMouse`); Android has no global-state backend, so that is SDL's event-tracked state.
- While the middle button is held, `SetRotateDrag` turns on relative mouse mode; SDL's
  `SDLGenericMotionListener_API26.setRelativeMouseEnabled` calls `View.requestPointerCapture()`.

## The defect (SDL 3.4.18), and the workaround

With the pointer captured, Android delivers the mouse to `View.onCapturedPointerEvent` with source
`SOURCE_MOUSE_RELATIVE`: `ACTION_MOVE` (motion with a button held; `getX/getY` are deltas),
`ACTION_BUTTON_PRESS` / `ACTION_BUTTON_RELEASE`, `ACTION_DOWN` / `ACTION_UP`, `ACTION_HOVER_MOVE`,
`ACTION_SCROLL`. SDL's `SDLSurface.onCapturedPointerEvent` hands the event to the generic-motion
listener, whose mouse arm keeps only `ACTION_SCROLL` and `ACTION_HOVER_MOVE`. Every button change and
every held-button motion is dropped, so the middle button's release never reaches SDL: the game keeps
rotating and never releases the capture.

`android/app/src/main/java/org/openba/game/OpenBASurface.java` (made by `OpenBAActivity.createSDLSurface`)
overrides `onCapturedPointerEvent`: `ACTION_MOVE` -> `onNativeMouse(0, ACTION_MOVE, dx, dy, true)`;
`ACTION_BUTTON_PRESS` / `RELEASE` -> `onNativeMouse(getButtonState(), ACTION_DOWN / ACTION_UP, 0, 0, true)`;
the plain `ACTION_DOWN` / `ACTION_UP` beside them are consumed (a second call would change no bit and
send SDL button 0); everything else goes to SDL's own handler. A fix in SDL itself would do the same in
`SDLSurface.onCapturedPointerEvent`; until an SDL bump carries one, the override stays.

## Testing without a mouse: a uinput device

`adb shell uinput <file>` (Android 14+, runs as shell) makes a virtual evdev device from JSON lines;
the device exists while the command runs. A three-button mouse, a middle press, a drag, a release:
```
{"id":1,"command":"register","name":"test mouse","vid":4660,"pid":22136,"bus":"usb","configuration":[{"type":"UI_SET_EVBIT","data":[1,2]},{"type":"UI_SET_KEYBIT","data":[272,273,274]},{"type":"UI_SET_RELBIT","data":[0,1,8]}]}
{"id":1,"command":"delay","duration":3000}
{"id":1,"command":"inject","events":[1,274,1,0,0,0]}
{"id":1,"command":"delay","duration":1000}
{"id":1,"command":"inject","events":[2,0,60,0,0,0]}
{"id":1,"command":"delay","duration":1000}
{"id":1,"command":"inject","events":[1,274,0,0,0,0]}
{"id":1,"command":"delay","duration":2000}
```
(`events` are `type, code, value` triples: EV_KEY 1 / BTN_MIDDLE 274, EV_REL 2 / REL_X 0, EV_SYN 0.)
`adb shell dumpsys input | grep -i "pointer capture"` shows the capture: `Current Window with Pointer
Capture: ...OpenBAActivity` while the button is held, `None` after the release.

`adb shell input tap` is a touch, which the game sees as a left click only when held (a 1-frame tap is
missed by the polled mouse): `adb shell input swipe X Y X Y 200` clicks.
