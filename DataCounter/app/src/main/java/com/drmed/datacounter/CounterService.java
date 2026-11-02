package com.drmed.datacounter;

import android.app.Service;
import android.content.Intent;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;

/** Keeps the status-bar counter alive and refreshes it every 10 s. */
public class CounterService extends Service {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable loop = new Runnable() {
        @Override public void run() {
            DataCounter.tick(CounterService.this);
            handler.postDelayed(this, 10_000);
        }
    };

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        startForeground(DataCounter.NOTIF_ID, DataCounter.notification(this));
        handler.removeCallbacks(loop);
        handler.post(loop);
        return START_STICKY;
    }

    @Override public void onDestroy() {
        handler.removeCallbacks(loop);
    }

    @Override public IBinder onBind(Intent intent) {
        return null;
    }
}
