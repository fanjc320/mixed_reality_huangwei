/****************************************************************
 * Copyright (c) 2020-2021 Qualcomm Technologies, Inc.
 * All Rights Reserved.
 * Confidential and Proprietary - Qualcomm Technologies, Inc.
 ****************************************************************/

package com.qualcomm.qti.xr.mixedreality;

import android.app.NativeActivity;
import android.content.SharedPreferences;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.preference.PreferenceManager;
import android.util.Log;
import android.view.View;
import android.view.WindowManager;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class VrNativeActivity extends NativeActivity {
    public static final String FIRST_TIME_TAG = "first_time";
    public static final String ASSETS_SUB_FOLDER_NAME = "raw";
    public static final int BUFFER_SIZE = 1024;

    void setImmersiveSticky() {
        View decorView = getWindow().getDecorView();
        decorView.setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        setContentView(R.layout.activity_main);

        setImmersiveSticky();

        View decorView = getWindow().getDecorView();
        decorView.setOnSystemUiVisibilityChangeListener(new View.OnSystemUiVisibilityChangeListener() {
            @Override
            public void onSystemUiVisibilityChange(int visibility) {
                setImmersiveSticky();
            }
        });

        SharedPreferences prefs = PreferenceManager.getDefaultSharedPreferences(this);
        if (!prefs.getBoolean(FIRST_TIME_TAG, false)) {
            SharedPreferences.Editor editor = prefs.edit();
            editor.putBoolean(FIRST_TIME_TAG, true);
            editor.commit();
            copyAssetsToExternal();
        }

        super.onCreate(savedInstanceState);
    }

    @Override
    protected void onResume() {
        //Hide toolbar
        int SDK_INT = android.os.Build.VERSION.SDK_INT;
        if (SDK_INT >= 14 && SDK_INT < 19) {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_LOW_PROFILE);
        } else if (SDK_INT >= 19) {
            setImmersiveSticky();
        }
        super.onResume();
    }

    /*
     * copy the Assets from assets/raw to app's external file dir
     */
    public void copyAssetsToExternal() {
        AssetManager assetManager = getAssets();
        String[] files = null;
        try {
            InputStream in = null;
            OutputStream out = null;

            files = assetManager.list(ASSETS_SUB_FOLDER_NAME);
            for (int i = 0; i < files.length; i++) {
                in = assetManager.open(ASSETS_SUB_FOLDER_NAME + "/" + files[i]);
                String outDir = getExternalFilesDir(null).toString() + "/";

                File outFile = new File(outDir, files[i]);
                out = new FileOutputStream(outFile);
                copyFile(in, out);
                in.close();
                in = null;
                out.flush();
                out.close();
                out = null;
            }
        } catch (IOException e) {
            Log.e("copyAssetsToExternal", "Failed to get asset file list.", e);
        }
        File file = getExternalFilesDir(null);
        Log.d("copyAssetsToExternal", "file:" + file.toString());
    }

    /*
     * read file from InputStream and write to OutputStream.
     */
    private void copyFile(InputStream in, OutputStream out) throws IOException {
        byte[] buffer = new byte[BUFFER_SIZE];
        int read;
        while ((read = in.read(buffer)) != -1) {
            out.write(buffer, 0, read);
        }
    }
}
