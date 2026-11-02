package com.drmed.datacounter;

import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.Context;
import android.content.Intent;
import android.widget.Toast;

public class WidgetProvider extends AppWidgetProvider {
    @Override public void onUpdate(Context c, AppWidgetManager m, int[] ids) {
        DataCounter.render(c);
    }

    @Override public void onReceive(Context c, Intent i) {
        super.onReceive(c, i);
        String a = i.getAction();
        if (DataCounter.RESET.equals(a)) {
            DataCounter.reset(c);
            Toast.makeText(c, "Reset to " + DataCounter.prefs(c).getLong("quota", 0) + " MB", Toast.LENGTH_SHORT).show();
        }
        // A widget tap and boot are both allowed to start a foreground service from the background.
        if (DataCounter.RESET.equals(a) || Intent.ACTION_BOOT_COMPLETED.equals(a)) {
            c.startForegroundService(new Intent(c, CounterService.class));
        }
    }
}
