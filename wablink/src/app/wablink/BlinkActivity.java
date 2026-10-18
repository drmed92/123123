package app.wablink;

import android.app.Activity;
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
import android.widget.TextView;
import java.util.Random;

/** Dim black lock-screen page whose clock + WhatsApp icon show for 2 s, then go dark for 4 s. */
public class BlinkActivity extends Activity {
    static final long ON_MS = 2000, OFF_MS = 4000, FADE_MS = 300;
    private static final int WA_GREEN = 0xFF25D366;

    /** Non-null while this screen exists; read by WaListener on the main thread. */
    static BlinkActivity current;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Random random = new Random();
    private FrameLayout root;
    private LinearLayout content;

    private final Runnable on = new Runnable() {
        @Override
        public void run() {
            // Shift a little every cycle so the same OLED pixels are not lit all night (burn-in).
            content.setTranslationX((random.nextFloat() - 0.5f) * root.getWidth() * 0.3f);
            content.setTranslationY((random.nextFloat() - 0.5f) * root.getHeight() * 0.3f);
            content.animate().alpha(1f).setDuration(FADE_MS);
            handler.postDelayed(off, ON_MS);
        }
    };

    private final Runnable off = new Runnable() {
        @Override
        public void run() {
            content.animate().alpha(0f).setDuration(FADE_MS);
            handler.postDelayed(on, OFF_MS);
        }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        current = this;
        WaListener.cancelTrigger(this);
        setShowWhenLocked(true);
        setTurnScreenOn(true);

        Window w = getWindow();
        w.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        WindowManager.LayoutParams lp = w.getAttributes();
        lp.screenBrightness = 0.03f; // AOD-like dim
        w.setAttributes(lp);
        w.getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY);

        float dp = getResources().getDisplayMetrics().density;
        TextClock clock = new TextClock(this);
        clock.setTextColor(0xFFDDDDDD);
        clock.setTextSize(56);
        ImageView icon = new ImageView(this);
        icon.setImageResource(android.R.drawable.sym_action_chat);
        icon.setColorFilter(WA_GREEN);
        TextView label = new TextView(this);
        label.setText("WhatsApp");
        label.setTextColor(WA_GREEN);

        content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        content.setAlpha(0f);
        content.addView(clock);
        content.addView(icon, new LinearLayout.LayoutParams((int) (64 * dp), (int) (64 * dp)));
        content.addView(label);

        root = new FrameLayout(this);
        root.setBackgroundColor(Color.BLACK);
        root.addView(content, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.WRAP_CONTENT, FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.CENTER));
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
