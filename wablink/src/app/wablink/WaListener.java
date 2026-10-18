package app.wablink;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.PowerManager;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;

/** Watches WhatsApp notifications and, while one is unread and the screen is off, brings up BlinkActivity. */
public class WaListener extends NotificationListenerService {
    static final BlinkState state = new BlinkState();
    private static final String CHANNEL = "blink";
    private static final int NOTIF_ID = 1;

    private final BroadcastReceiver screenReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent i) {
            if (Intent.ACTION_USER_PRESENT.equals(i.getAction())) {
                state.dismissed = false;
                // Fingerprint unlock straight from the blink screen would otherwise leave it on top.
                if (BlinkActivity.current != null) BlinkActivity.current.finish();
            } else if (BlinkActivity.current != null) {
                // Power pressed while blinking: the user has seen it.
                state.dismissed = true;
                BlinkActivity.current.finish();
            } else {
                maybeShow();
            }
        }
    };

    @Override
    public void onListenerConnected() {
        NotificationChannel ch = new NotificationChannel(CHANNEL, "Blink trigger", NotificationManager.IMPORTANCE_HIGH);
        ch.setSound(null, null);
        ch.enableVibration(false);
        ch.setShowBadge(false);
        getSystemService(NotificationManager.class).createNotificationChannel(ch);

        IntentFilter f = new IntentFilter(Intent.ACTION_SCREEN_OFF);
        f.addAction(Intent.ACTION_USER_PRESENT);
        registerReceiver(screenReceiver, f);

        for (StatusBarNotification sbn : active()) {
            if (counts(sbn)) state.onPosted(sbn.getNotification().when);
        }
    }

    @Override
    public void onListenerDisconnected() {
        unregisterReceiver(screenReceiver);
    }

    @Override
    public void onNotificationPosted(StatusBarNotification sbn) {
        if (counts(sbn) && state.onPosted(sbn.getNotification().when)) maybeShow();
    }

    @Override
    public void onNotificationRemoved(StatusBarNotification sbn) {
        if (!BlinkState.isWhatsApp(sbn.getPackageName()) || hasUnread()) return;
        cancelTrigger(this);
        if (BlinkActivity.current != null) BlinkActivity.current.finish();
    }

    private static boolean counts(StatusBarNotification sbn) {
        // Ongoing ones are calls / "checking for new messages", not unread messages.
        return BlinkState.isWhatsApp(sbn.getPackageName()) && !sbn.isOngoing();
    }

    private StatusBarNotification[] active() {
        StatusBarNotification[] all = getActiveNotifications();
        return all != null ? all : new StatusBarNotification[0]; // null when the system call fails
    }

    private boolean hasUnread() {
        for (StatusBarNotification sbn : active()) {
            if (counts(sbn)) return true;
        }
        return false;
    }

    private void maybeShow() {
        boolean screenOn = getSystemService(PowerManager.class).isInteractive();
        if (!state.shouldShow(hasUnread(), screenOn, BlinkActivity.current != null)) return;
        // A full-screen intent is the sanctioned way to open an activity over the lock screen from the background.
        PendingIntent pi = PendingIntent.getActivity(this, 0, new Intent(this, BlinkActivity.class),
                PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification n = new Notification.Builder(this, CHANNEL)
                .setSmallIcon(android.R.drawable.sym_action_chat)
                .setContentTitle("Unread WhatsApp message")
                .setCategory(Notification.CATEGORY_REMINDER)
                .setContentIntent(pi)
                .setFullScreenIntent(pi, true)
                .setAutoCancel(true)
                .build();
        getSystemService(NotificationManager.class).notify(NOTIF_ID, n);
    }

    static void cancelTrigger(Context c) {
        c.getSystemService(NotificationManager.class).cancel(NOTIF_ID);
    }
}
