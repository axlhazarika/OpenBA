// android/app/src/main/java/org/openba/game/OpenBASurface.java (docs/android/README.md, "Mouse")
//
// SDL's SDLSurface with one change: the mouse while the pointer is captured.
//
// The game holds the middle button to rotate the camera, and while it is held the SDL layer puts
// the window in relative mouse mode (src/platform/sdl/input_sdl.cpp, SetRotateDrag), which SDL's
// Java glue turns into View.requestPointerCapture(). A captured pointer's events no longer reach
// onTouch / onGenericMotion; Android hands them to onCapturedPointerEvent, and the pinned SDL
// (3.4.x, SDLSurface.onCapturedPointerEvent) passes them on to its generic-motion listener, which
// keeps only ACTION_SCROLL and ACTION_HOVER_MOVE for a mouse. With a button held a captured mouse
// sends ACTION_MOVE, ACTION_BUTTON_PRESS / ACTION_BUTTON_RELEASE and ACTION_DOWN / ACTION_UP, so
// SDL dropped them all: the drag's motion and, worse, the middle button's release. SDL's button
// state kept the middle button down, the game kept rotating and the capture was never released
// (seen on the S25 with a mouse over a KVM; reproduced with a uinput mouse, docs/android/P8_INPUT_OPTIONS.md).
//
// Here those events go to SDL's own native entry, SDLActivity.onNativeMouse, as SDL's onTouch
// would send them: the button state with ACTION_DOWN / ACTION_UP (SDL_androidmouse.c takes the
// changed button from the state it last saw), the motion as relative. The plain ACTION_DOWN /
// ACTION_UP of a captured mouse always comes beside a BUTTON_PRESS / BUTTON_RELEASE carrying the
// same state, so it is consumed without a second call (a second call would change no button and
// send SDL button 0). Scroll and hover motion stay SDL's.
package org.openba.game;

import android.content.Context;
import android.view.MotionEvent;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class OpenBASurface extends SDLSurface {
    public OpenBASurface(Context context) {
        super(context);
    }

    @Override
    public boolean onCapturedPointerEvent(MotionEvent event) {
        switch (event.getActionMasked()) {
        case MotionEvent.ACTION_MOVE:
            SDLActivity.onNativeMouse(0, MotionEvent.ACTION_MOVE, event.getX(0), event.getY(0), true);
            return true;
        case MotionEvent.ACTION_BUTTON_PRESS:
            SDLActivity.onNativeMouse(event.getButtonState(), MotionEvent.ACTION_DOWN, 0.0f, 0.0f, true);
            return true;
        case MotionEvent.ACTION_BUTTON_RELEASE:
            SDLActivity.onNativeMouse(event.getButtonState(), MotionEvent.ACTION_UP, 0.0f, 0.0f, true);
            return true;
        case MotionEvent.ACTION_DOWN:
        case MotionEvent.ACTION_UP:
            return true;
        default:
            return super.onCapturedPointerEvent(event);
        }
    }
}
