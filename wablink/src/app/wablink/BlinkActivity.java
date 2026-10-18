package app.wablink;

import android.app.Activity;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextClock;
import java.util.Random;

/** Dim black lock-screen page whose clock + small app icons show for the "on" time, then go dark for the "off" time. */
public class BlinkActivity extends Activity {
    /** Settings written by MainActivity. Times are in tenths of a second, brightness in percent. */
    static final String PREFS = "settings", BRIGHTNESS = "brightness", ON = "on_ds", OFF = "off_ds";
    static final int DEF_BRIGHTNESS = 3, DEF_ON = 20, DEF_OFF = 40;

    // Indexed by BlinkState source: WhatsApp, SMS, missed call, Gmail.
    private static final int[] ICONS = {android.R.drawable.sym_action_chat, android.R.drawable.stat_notify_chat,
            android.R.drawable.stat_notify_missed_call, android.R.drawable.sym_action_email};
    // Missed calls are orange rather than missed-call red so they don't look like Gmail.
    private static final int[] COLORS = {0xFF25D366, 0xFF1A73E8, 0xFFFF9800, 0xFFEA4335};

    /** Non-null while this screen exists; read by WaListener on the main thread. */
    static BlinkActivity current;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();
    private final ImageView[] icons = new ImageView[ICONS.length];
    private FrameLayout content;
    private long onMs, offMs, fadeMs;
    private float shift;
    private boolean test;

    private final Runnable on = new Runnable() {
        @Override
        public void run() {
            int mask = test ? ~0 : WaListener.unread;
            for (int i = 0; i < icons.length; i++) {
                icons[i].setVisibility((mask & 1 << i) != 0 ? View.VISIBLE : View.GONE);
            }
            // Shift a little every cycle so the same OLED pixels are not lit all night (burn-in).
            content.setTranslationX((random.nextFloat() * 2 - 1) * shift);
            content.setTranslationY((random.nextFloat() * 2 - 1) * shift);
            content.animate().alpha(1f).setDuration(fadeMs);
            handler.postDelayed(off, onMs);
        }
    };

    private final Runnable off = new Runnable() {
        @Override
        public void run() {
            content.animate().alpha(0f).setDuration(fadeMs);
            handler.postDelayed(on, offMs);
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        current = this;
        WaListener.cancelTrigger(this);
        test = getIntent().getBooleanExtra("test", false);
        SharedPreferences p = getSharedPreferences(PREFS, MODE_PRIVATE);
        onMs = p.getInt(ON, DEF_ON) * 100L;
        offMs = p.getInt(OFF, DEF_OFF) * 100L;
        fadeMs = Math.min(300, Math.min(onMs, offMs) / 2);

        setShowWhenLocked(true);
        setTurnScreenOn(true);
        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams lp = w.getAttributes();
        lp.screenBrightness = p.getInt(BRIGHTNESS, DEF_BRIGHTNESS) / 100f;
        w.setAttributes(lp);
        w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        float dp = getResources().getDisplayMetrics().density;
        shift = 8 * dp;
        LinearLayout row = new LinearLayout(this);
        for (int i = 0; i < ICONS.length; i++) {
            icons[i] = new ImageView(this);
            icons[i].setImageResource(ICONS[i]);
            icons[i].setColorFilter(COLORS[i]);
            LinearLayout.LayoutParams ilp = new LinearLayout.LayoutParams((int) (16 * dp), (int) (16 * dp));
            ilp.rightMargin = (int) (6 * dp);
            row.addView(icons[i], ilp);
        }
        TextClock clock = new TextClock(this);
        clock.setTextColor(0xFFDDDDDD);
        clock.setTextSize(56);

        content = new FrameLayout(this);
        content.setAlpha(0f);
        FrameLayout.LayoutParams rowLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP | Gravity.LEFT);
        rowLp.setMargins((int) (24 * dp), (int) (32 * dp), 0, 0); // margins > shift keep icons on screen
        content.addView(row, rowLp);
        content.addView(clock, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER));

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.addView(content);
        root.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                WaListener.state.dismissed = true;
                finish();
            }
        });
        setContentView(root);
    }

    @Override
    protected void onStart() {
        super.onStart();
        handler.post(on);
    }

    @Override
    protected void onStop() {
        super.onStop();
        handler.removeCallbacks(on);
        handler.removeCallbacks(off);
        content.setAlpha(0f);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (current == this) current = null;
    }
}
