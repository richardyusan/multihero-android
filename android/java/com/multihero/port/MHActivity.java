// SPDX-License-Identifier: MIT
package com.multihero.port;

import android.app.NativeActivity;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

/** NativeActivity with immersive full screen; the game itself runs in libmultihero.so. */
public class MHActivity extends NativeActivity {
    /**
     * Epic Online Services: libEOSSDK.so must be loaded from Java (its JNI_OnLoad keeps the
     * JavaVM) and EOSSDK.init(activity) must run before the game calls EOS_Initialize.
     * Builds without the SDK, and ABIs without libEOSSDK.so (armeabi-v7a), simply skip this.
     */
    private static boolean eosLoaded;
    static {
        try {
            System.loadLibrary("EOSSDK");
            eosLoaded = true;
        } catch (Throwable t) {
            eosLoaded = false;
        }
    }

    private void initEos() {
        if (!eosLoaded) return;
        try {
            Class.forName("com.epicgames.mobile.eossdk.EOSSDK")
                    .getMethod("init", android.app.Activity.class).invoke(null, this);
        } catch (Throwable t) {
            android.util.Log.w("MultiHero", "EOS init failed", t);
        }
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON
                | WindowManager.LayoutParams.FLAG_FULLSCREEN);
        if (Build.VERSION.SDK_INT >= 28) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        super.onCreate(savedInstanceState);
        initEos();
        hideSystemUi();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @SuppressWarnings("deprecation")
    private void hideSystemUi() {
        getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);
    }
}
