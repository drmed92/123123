package app.wablink;

import android.app.Activity;
import android.app.NotificationManager;
import android.content.ComponentName;
import android.content.Intent;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;

/** Setup checklist: each button shows whether a permission is granted and opens its settings page. */
public class MainActivity extends Activity {
    private LinearLayout list;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        list = new LinearLayout(this);
        list.setOrientation(LinearLayout.VERTICAL);
        int pad = (int) (24 * getResources().getDisplayMetrics().density);
        list.setPadding(pad, pad * 2, pad, pad);
        setContentView(list);
    }

    @Override
    protected void onResume() {
        super.onResume();
        list.removeAllViews();
        NotificationManager nm = getSystemService(NotificationManager.class);

        TextView info = new TextView(this);
        info.setText("While a WhatsApp, SMS, missed call or Gmail notification is unread and the screen is off, "
                + "a dim screen blinks over the lock screen.\n\nTap it, or press power, to silence it "
                + "until the next message or until you unlock.\n");
        list.addView(info);

        slider("Brightness", BlinkActivity.BRIGHTNESS, BlinkActivity.DEF_BRIGHTNESS, 1, 100, false);
        slider("Blink on", BlinkActivity.ON, BlinkActivity.DEF_ON, 5, 100, true);
        slider("Blink off", BlinkActivity.OFF, BlinkActivity.DEF_OFF, 5, 300, true);

        step("Notification access (to see unread notifications)",
                nm.isNotificationListenerAccessGranted(new ComponentName(this, WaListener.class)),
                new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS));
        step("Allow notifications",
                nm.areNotificationsEnabled(),
                new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS)
                        .putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName()));
        if (Build.VERSION.SDK_INT >= 34) {
            step("Full-screen alerts (to show over the lock screen)",
                    nm.canUseFullScreenIntent(),
                    new Intent(Settings.ACTION_MANAGE_APP_USE_FULL_SCREEN_INTENT,
                            Uri.parse("package:" + getPackageName())));
        }
        step("Test blink", false, new Intent(this, BlinkActivity.class).putExtra("test", true));
    }

    /** A labelled slider saved to BlinkActivity.PREFS; tenths of a second when {@code seconds}, else percent. */
    private void slider(final String name, final String key, int def, int min, int max, final boolean seconds) {
        final SharedPreferences prefs = getSharedPreferences(BlinkActivity.PREFS, MODE_PRIVATE);
        final TextView label = new TextView(this);
        SeekBar bar = new SeekBar(this);
        bar.setMin(min);
        bar.setMax(max);
        bar.setProgress(prefs.getInt(key, def));
        SeekBar.OnSeekBarChangeListener l = new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar s, int v, boolean fromUser) {
                label.setText(name + ": " + (seconds ? v / 10f + " s" : v + " %"));
                if (fromUser) prefs.edit().putInt(key, v).apply();
            }

            @Override
            public void onStartTrackingTouch(SeekBar s) {}

            @Override
            public void onStopTrackingTouch(SeekBar s) {}
        };
        bar.setOnSeekBarChangeListener(l);
        l.onProgressChanged(bar, bar.getProgress(), false);
        list.addView(label);
        list.addView(bar);
    }

    private void step(String label, boolean ok, final Intent intent) {
        Button btn = new Button(this);
        btn.setText((ok ? "✓  " : "") + label);
        btn.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                startActivity(intent);
            }
        });
        list.addView(btn);
    }
}
