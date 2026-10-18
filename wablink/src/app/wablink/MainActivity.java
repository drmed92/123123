package app.wablink;

import android.app.Activity;
import android.app.NotificationManager;
import android.content.ComponentName;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
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
        info.setText("While a WhatsApp message is unread and the screen is off, a dim screen blinks "
                + "2 s on / 4 s off over the lock screen.\n\nTap it, or press power, to silence it "
                + "until the next message or until you unlock.\n");
        list.addView(info);

        step("Notification access (to see WhatsApp notifications)",
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
        step("Test blink", false, new Intent(this, BlinkActivity.class));
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
