package com.george.hackctl;

import android.app.Service;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.os.IBinder;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public class FloatingService extends Service {
    private WindowManager wm;
    private LinearLayout floatView;
    private boolean isMagnetOn = true;
    private boolean isAimOn = true;

    @Override
    public void onCreate() {
        super.onCreate();
        wm = (WindowManager) getSystemService(WINDOW_SERVICE);
        createFloatingView();
    }

    private void createFloatingView() {
        floatView = new LinearLayout(this);
        floatView.setOrientation(LinearLayout.VERTICAL);
        floatView.setBackgroundColor(Color.argb(200, 0, 0, 0));
        floatView.setPadding(20, 20, 20, 20);

        TextView title = new TextView(this);
        title.setText("乔治控制台");
        title.setTextColor(Color.WHITE);
        title.setTextSize(18);
        floatView.addView(title);

        TextView status = new TextView(this);
        status.setId(View.generateViewId());
        status.setText("磁力: ON  自瞄: ON");
        status.setTextColor(Color.GREEN);
        status.setTextSize(14);
        floatView.addView(status);

        Button btnMagnet = new Button(this);
        btnMagnet.setText("磁力追踪 (ON)");
        btnMagnet.setBackgroundColor(Color.DKGRAY);
        btnMagnet.setOnClickListener(v -> {
            isMagnetOn = !isMagnetOn;
            btnMagnet.setText(isMagnetOn ? "磁力追踪 (ON)" : "磁力追踪 (OFF)");
            writeConfig("magnet", isMagnetOn ? "1" : "0");
            updateStatus(status);
        });
        floatView.addView(btnMagnet);

        Button btnAim = new Button(this);
        btnAim.setText("自瞄锁头 (ON)");
        btnAim.setBackgroundColor(Color.DKGRAY);
        btnAim.setOnClickListener(v -> {
            isAimOn = !isAimOn;
            btnAim.setText(isAimOn ? "自瞄锁头 (ON)" : "自瞄锁头 (OFF)");
            writeConfig("aim", isAimOn ? "1" : "0");
            updateStatus(status);
        });
        floatView.addView(btnAim);

        Button btnExit = new Button(this);
        btnExit.setText("退出并停止功能");
        btnExit.setBackgroundColor(Color.RED);
        btnExit.setTextColor(Color.WHITE);
        btnExit.setOnClickListener(v -> {
            try {
                Runtime.getRuntime().exec(new String[]{"su", "-c", "killall pmagnet"});
                File cfg = new File("/sdcard/hack.cfg");
                if (cfg.exists()) cfg.delete();
                stopSelf();
            } catch (IOException e) {
                e.printStackTrace();
                stopSelf();
            }
        });
        floatView.addView(btnExit);

        WindowManager.LayoutParams params = new WindowManager.LayoutParams(
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.WRAP_CONTENT,
                WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
                WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
                PixelFormat.TRANSLUCENT
        );
        params.gravity = Gravity.TOP | Gravity.CENTER_HORIZONTAL;
        params.y = 150;

        wm.addView(floatView, params);
    }

    private void updateStatus(TextView tv) {
        tv.setText("磁力: " + (isMagnetOn ? "ON" : "OFF") + "  自瞄: " + (isAimOn ? "ON" : "OFF"));
        tv.setTextColor(isMagnetOn && isAimOn ? Color.GREEN : Color.YELLOW);
    }

    private void writeConfig(String key, String value) {
        File file = new File("/sdcard/hack.cfg");
        try {
            FileOutputStream fos = new FileOutputStream(file, false);
            fos.write(("magnet=" + (isMagnetOn ? "1" : "0") + "\n").getBytes());
            fos.write(("aim=" + (isAimOn ? "1" : "0") + "\n").getBytes());
            fos.close();
        } catch (IOException e) {
            e.printStackTrace();
        }
    }

    @Override
    public void onDestroy() {
        if (floatView != null) wm.removeView(floatView);
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) { return null; }
}