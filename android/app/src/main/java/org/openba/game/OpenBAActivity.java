// android/app/src/main/java/org/openba/game/OpenBAActivity.java (docs/android/README.md)
//
// The app's one activity: SDL's SDLActivity (compiled from the pinned SDL submodule, see
// android/app/build.gradle) with three changes.
//
//  * The native entry point is the game's own `main` (src/app/main.cpp, built into libmain.so),
//    the same function Linux runs, not SDL's renamed SDL_main: main.cpp defines SDL_MAIN_HANDLED.
//
//  * The launch environment. An Android process has no environment a launcher could fill, and
//    the game reads its BANISHED_* flags once at boot from the environment (src/app/options.cpp,
//    the one getenv in the tree; src/app/options.h is the inventory). The sanctioned source on
//    Android is the launch intent: every string extra whose key starts with BANISHED_ becomes that
//    environment variable before the native library is loaded, e.g.
//        adb shell am start -n <package>/org.openba.game.OpenBAActivity -e BANISHED_AUTOSTART 1
//    Variables left from an earlier launch in the same process are cleared first, so a launch
//    from the icon (no extras) runs with none. Nothing else sets the environment.
//
//  * The surface is OpenBASurface: SDL's, with the captured mouse's buttons and motion passed on
//    (a camera-rotate drag's middle-button release was lost; OpenBASurface.java has the defect).
package org.openba.game;

import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.system.ErrnoException;
import android.system.Os;
import android.util.Log;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

public class OpenBAActivity extends SDLActivity {
    private static final String TAG = "OpenBA";
    private static final String FLAG_PREFIX = "BANISHED_";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        applyLaunchEnvironment(getIntent());
        super.onCreate(savedInstanceState);
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new OpenBASurface(context);
    }

    private static void applyLaunchEnvironment(Intent intent) {
        try {
            for (String entry : Os.environ()) {
                final int eq = entry.indexOf('=');
                if (eq > 0 && entry.startsWith(FLAG_PREFIX)) {
                    Os.unsetenv(entry.substring(0, eq));
                }
            }
        } catch (ErrnoException e) {
            Log.w(TAG, "clearing the launch environment failed: " + e);
        }
        final Bundle extras = intent != null ? intent.getExtras() : null;
        if (extras == null) {
            return;
        }
        for (String key : extras.keySet()) {
            if (!key.startsWith(FLAG_PREFIX)) {
                continue;
            }
            final Object value = extras.get(key);
            if (value == null) {
                continue;
            }
            try {
                Os.setenv(key, String.valueOf(value), true);
                Log.i(TAG, "launch environment " + key + "=" + value);
            } catch (ErrnoException e) {
                Log.w(TAG, "setenv " + key + " failed: " + e);
            }
        }
    }
}
