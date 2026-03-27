package fg.fgmods.key;

import android.content.Context;
import android.content.SharedPreferences;

/**
 * NativePrefsHelper
 *
 * Package-private static bridge so native code can read/write
 * SharedPreferences without exposing a public Java API.
 *
 * All pref key names are passed in from native code (where they are stored
 * as short obfuscated strings).  This class has no knowledge of what the keys
 * mean – it is a pure mechanical bridge.
 */
/* package-private */ class NativePrefsHelper {

    private static final String PREFS_NAME = "vp_secure";

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    static String getString(Context ctx, String key) {
        return prefs(ctx).getString(key, null);
    }

    static void putString(Context ctx, String key, String value) {
        prefs(ctx).edit().putString(key, value).apply();
    }

    static void remove(Context ctx, String key) {
        prefs(ctx).edit().remove(key).apply();
    }

    static long getLong(Context ctx, String key) {
        return prefs(ctx).getLong(key, 0L);
    }

    static void putLong(Context ctx, String key, long value) {
        prefs(ctx).edit().putLong(key, value).apply();
    }

    static int getInt(Context ctx, String key) {
        return prefs(ctx).getInt(key, 0);
    }

    static void putInt(Context ctx, String key, int value) {
        prefs(ctx).edit().putInt(key, value).apply();
    }
}
