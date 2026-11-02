package com.drmed.datacounter;

import android.Manifest;
import android.app.Activity;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.text.InputFilter;
import android.text.InputType;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;

/** Small dialog: type the MB you have left; the widget tap resets back to this value. */
public class MainActivity extends Activity {
    @Override protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (Build.VERSION.SDK_INT >= 33) requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 0);

        EditText mb = new EditText(this);
        mb.setInputType(InputType.TYPE_CLASS_NUMBER);
        mb.setFilters(new InputFilter[]{new InputFilter.LengthFilter(9)});
        mb.setHint("MB remaining");
        long quota = DataCounter.prefs(this).getLong("quota", 0);
        if (quota > 0) mb.setText(String.valueOf(quota));

        Button set = new Button(this);
        set.setText("Set");
        set.setOnClickListener(v -> {
            try {
                DataCounter.prefs(this).edit().putLong("quota", Long.parseLong(mb.getText().toString())).apply();
            } catch (NumberFormatException e) {
                mb.setError("Enter a number");
                return;
            }
            DataCounter.reset(this);
            startForegroundService(new Intent(this, CounterService.class));
            finish();
        });

        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.VERTICAL);
        l.setPadding(48, 24, 48, 24);
        l.addView(mb);
        l.addView(set);
        setContentView(l);
    }
}
