package com.drmed.datacounter;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.Typeface;
import android.graphics.drawable.Icon;
import android.net.TrafficStats;
import android.widget.RemoteViews;

/** Counts mobile bytes since the last reset and shows "quota - used" in the status bar and widget. */
final class DataCounter {
    static final String RESET = "com.drmed.datacounter.RESET";
    static final int NOTIF_ID = 1;
    private static final long MB = 1_000_000; // same unit as Android's data-usage screen

    static SharedPreferences prefs(Context c) {
        return c.getSharedPreferences("data", Context.MODE_PRIVATE);
    }

    /** Bytes used between two counter readings; the counters restart at 0 after a reboot. */
    static long delta(long last, long cur) {
        return cur >= last ? cur - last : cur;
    }

    static void tick(Context c) {
        long cur = TrafficStats.getMobileRxBytes() + TrafficStats.getMobileTxBytes();
        SharedPreferences p = prefs(c);
        if (cur >= 0) { // negative = TrafficStats.UNSUPPORTED on this device
            p.edit().putLong("used", p.getLong("used", 0) + delta(p.getLong("last", cur), cur))
                    .putLong("last", cur).apply();
        }
        render(c);
    }

    static void reset(Context c) {
        prefs(c).edit().putLong("used", 0).remove("last").apply();
        tick(c);
    }

    static long remainingMb(Context c) {
        SharedPreferences p = prefs(c);
        return Math.max(0, p.getLong("quota", 0) * MB - p.getLong("used", 0)) / MB;
    }

    static void render(Context c) {
        c.getSystemService(NotificationManager.class).notify(NOTIF_ID, notification(c));
        RemoteViews v = new RemoteViews(c.getPackageName(), R.layout.widget);
        v.setTextViewText(R.id.mb, String.valueOf(remainingMb(c)));
        v.setOnClickPendingIntent(R.id.root, PendingIntent.getBroadcast(c, 0,
                new Intent(c, WidgetProvider.class).setAction(RESET), PendingIntent.FLAG_IMMUTABLE));
        AppWidgetManager.getInstance(c).updateAppWidget(new ComponentName(c, WidgetProvider.class), v);
    }

    static Notification notification(Context c) {
        String left = String.valueOf(remainingMb(c));
        // DEFAULT (not LOW) so the icon isn't hidden as a "silent" notification; no sound set.
        NotificationChannel ch = new NotificationChannel("data", "Data remaining", NotificationManager.IMPORTANCE_DEFAULT);
        ch.setSound(null, null);
        c.getSystemService(NotificationManager.class).createNotificationChannel(ch);
        return new Notification.Builder(c, "data")
                .setSmallIcon(icon(left))
                .setContentTitle(left + " MB remaining")
                .setContentText("Tap the widget to reset to " + prefs(c).getLong("quota", 0) + " MB")
                .setContentIntent(PendingIntent.getActivity(c, 0,
                        new Intent(c, MainActivity.class), PendingIntent.FLAG_IMMUTABLE))
                .setOngoing(true)
                .setOnlyAlertOnce(true)
                .build();
    }

    /** The number drawn white on transparent; the status bar only uses the alpha channel. */
    static Icon icon(String text) {
        Bitmap b = Bitmap.createBitmap(96, 96, Bitmap.Config.ARGB_8888);
        Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
        p.setColor(Color.WHITE);
        p.setTypeface(Typeface.create("sans-serif-condensed", Typeface.BOLD));
        p.setTextAlign(Paint.Align.CENTER);
        p.setTextSize(96);
        p.setTextSize(Math.min(96, 96 * 96 / p.measureText(text))); // shrink long numbers to fit
        Rect r = new Rect();
        p.getTextBounds(text, 0, text.length(), r);
        new Canvas(b).drawText(text, 48, 48 - r.exactCenterY(), p);
        return Icon.createWithBitmap(b);
    }
}
