package com.drmed92.iqairwidget

import android.appwidget.AppWidgetManager
import android.appwidget.AppWidgetProvider
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import kotlin.concurrent.thread

/** Alarm ticks and widget taps (REFRESH), boot, app updates and widget changes all land here. */
class UpdateReceiver : BroadcastReceiver() {
    override fun onReceive(ctx: Context, intent: Intent) {
        val pending = goAsync()
        thread {
            try {
                val p = Aqi.prefs(ctx)
                val due = intent.action == Aqi.REFRESH || System.currentTimeMillis() - p.getLong("tried", 0) >= Aqi.intervalMs(p)
                Aqi.update(ctx, fetch = due && Aqi.inUse(ctx, p))
            } finally {
                pending.finish()
            }
        }
    }
}

/** One provider per picker size; all share the responsive layouts, so any of them can be resized. */
open class AqiWidget : AppWidgetProvider() {
    override fun onUpdate(ctx: Context, mgr: AppWidgetManager, ids: IntArray) = kick(ctx)

    override fun onDeleted(ctx: Context, ids: IntArray) = Aqi.prefs(ctx).edit().also { e -> ids.forEach { e.remove("mode_$it") } }.apply()

    // Last widget of this size removed: lets the receiver stop polling if nothing else needs data
    override fun onDisabled(ctx: Context) = kick(ctx)

    private fun kick(ctx: Context) = ctx.sendBroadcast(Intent(ctx, UpdateReceiver::class.java))
}

class Widget1x1 : AqiWidget()
class Widget2x1 : AqiWidget()
class Widget3x2 : AqiWidget()
