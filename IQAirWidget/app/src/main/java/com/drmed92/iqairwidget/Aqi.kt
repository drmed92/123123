package com.drmed92.iqairwidget

import android.app.AlarmManager
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.appwidget.AppWidgetManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Typeface
import android.graphics.drawable.Icon
import android.text.format.DateFormat
import android.util.SizeF
import android.widget.RemoteViews
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.net.UnknownHostException
import java.util.Date
import kotlin.math.min

/** One IQAir city reading, parsed from the `data` object of /v2/city. */
class Reading(val aqi: Int, val pollutant: String, val tempC: Int, val humidity: Int, val windMs: Double, val city: String) {
    companion object {
        private val POLLUTANTS = mapOf("p2" to "PM2.5", "p1" to "PM10", "o3" to "Ozone", "n2" to "NO₂", "s2" to "SO₂", "co" to "CO")

        fun parse(data: JSONObject): Reading {
            val current = data.getJSONObject("current")
            val pollution = current.getJSONObject("pollution")
            val weather = current.getJSONObject("weather")
            val main = pollution.optString("mainus")
            return Reading(
                pollution.getInt("aqius"), POLLUTANTS[main] ?: main,
                weather.getInt("tp"), weather.getInt("hu"), weather.optDouble("ws", 0.0),
                data.getString("city"),
            )
        }
    }
}

object Aqi {
    const val REFRESH = "com.drmed92.iqairwidget.REFRESH"
    private const val CHANNEL = "aqi"
    private const val NOTIFICATION_ID = 1
    private val TEXTS = intArrayOf(R.id.aqi, R.id.unit, R.id.label, R.id.city, R.id.weather, R.id.detail, R.id.time)

    fun prefs(ctx: Context): SharedPreferences = ctx.getSharedPreferences("aqi", Context.MODE_PRIVATE)

    fun intervalMs(p: SharedPreferences) = p.getInt("interval", 5) * 60_000L

    /** US AQI bands in IQAir's palette. */
    fun color(aqi: Int) = when {
        aqi <= 50 -> 0xFFA8E05F
        aqi <= 100 -> 0xFFFDD64B
        aqi <= 150 -> 0xFFFF9B57
        aqi <= 200 -> 0xFFFE6A69
        aqi <= 300 -> 0xFFA97ABC
        else -> 0xFFA87383
    }.toInt()

    fun label(aqi: Int) = when {
        aqi <= 50 -> "Good"
        aqi <= 100 -> "Moderate"
        aqi <= 150 -> "Unhealthy for sensitive groups"
        aqi <= 200 -> "Unhealthy"
        aqi <= 300 -> "Very unhealthy"
        else -> "Hazardous"
    }

    private fun textColor(aqi: Int) = if (aqi > 200) Color.WHITE else 0xDE000000.toInt()

    fun weather(r: Reading) = "${r.tempC}°C · 💧${r.humidity}%"

    /** Calls the IQAir AirVisual v2 API; failures throw with IQAir's reason, e.g. "call limit reached". */
    fun api(path: String, key: String, vararg query: Pair<String, String>): JSONObject {
        val qs = (query.toList() + ("key" to key)).joinToString("&") { (k, v) -> "$k=" + URLEncoder.encode(v, "UTF-8") }
        val conn = URL("https://api.airvisual.com/v2/$path?$qs").openConnection() as HttpURLConnection
        conn.connectTimeout = 8000
        conn.readTimeout = 8000
        try {
            val stream = if (conn.responseCode < 400) conn.inputStream else conn.errorStream ?: throw IOException("HTTP ${conn.responseCode}")
            val json = JSONObject(stream.bufferedReader().use { it.readText() })
            if (json.optString("status") != "success") {
                throw IOException(json.optJSONObject("data")?.optString("message")?.replace('_', ' ') ?: "HTTP ${conn.responseCode}")
            }
            return json
        } finally {
            conn.disconnect()
        }
    }

    fun list(path: String, key: String, field: String, vararg query: Pair<String, String>): List<String> {
        val data = api(path, key, *query).getJSONArray("data")
        return (0 until data.length()).map { data.getJSONObject(it).getString(field) }
    }

    fun reading(p: SharedPreferences): Reading? = p.getString("last", null)?.let { Reading.parse(JSONObject(it)) }

    private fun widgetIds(ctx: Context): IntArray {
        val mgr = AppWidgetManager.getInstance(ctx)
        return listOf(Widget1x1::class.java, Widget2x1::class.java, Widget3x2::class.java)
            .flatMap { mgr.getAppWidgetIds(ComponentName(ctx, it)).asList() }.toIntArray()
    }

    /** False when nothing shows the data; polling then stops to save data and battery. */
    fun inUse(ctx: Context, p: SharedPreferences) = widgetIds(ctx).isNotEmpty() || p.getBoolean("notify", false)

    /** Optionally fetches, then redraws widgets + notification and arms the next alarm. Blocks: call off the main thread. */
    fun update(ctx: Context, fetch: Boolean) {
        val p = prefs(ctx)
        if (fetch) fetch(p)
        render(ctx, p)
        schedule(ctx, p)
    }

    private fun fetch(p: SharedPreferences) {
        val edit = p.edit().putLong("tried", System.currentTimeMillis())
        try {
            val key = p.getString("key", "")!!
            val city = p.getString("city", "")!!
            if (key.isEmpty() || city.isEmpty()) throw IOException("open the app to set key and city")
            val data = api("city", key, "city" to city, "state" to p.getString("state", "")!!, "country" to p.getString("country", "")!!)
                .getJSONObject("data")
            Reading.parse(data) // reject malformed data before it replaces the cached reading
            edit.putString("last", data.toString()).putLong("at", System.currentTimeMillis()).remove("error")
        } catch (e: UnknownHostException) {
            edit.putString("error", "offline")
        } catch (e: Exception) {
            edit.putString("error", e.message ?: e.javaClass.simpleName)
        }
        edit.commit()
    }

    private fun render(ctx: Context, p: SharedPreferences) {
        val r = reading(p)
        val status = p.getString("error", null)?.let { "⚠ $it" }
            ?: if (r == null) "" else "Updated " + DateFormat.getTimeFormat(ctx).format(Date(p.getLong("at", 0)))
        val ids = widgetIds(ctx)
        if (ids.isNotEmpty()) {
            // The launcher picks the largest layout that fits each widget's current size
            AppWidgetManager.getInstance(ctx).updateAppWidget(ids, RemoteViews(mapOf(
                SizeF(40f, 40f) to widget(ctx, R.layout.widget_small, r, status),
                SizeF(110f, 40f) to widget(ctx, R.layout.widget_wide, r, status),
                SizeF(180f, 100f) to widget(ctx, R.layout.widget_large, r, status),
            )))
        }
        notification(ctx, p, r, status)
    }

    // Setters for views a layout doesn't have are skipped by RemoteViews.
    private fun widget(ctx: Context, layout: Int, r: Reading?, status: String) = RemoteViews(ctx.packageName, layout).apply {
        setInt(R.id.bg, "setColorFilter", r?.let { color(it.aqi) } ?: Color.LTGRAY)
        val fg = r?.let { textColor(it.aqi) } ?: textColor(0)
        for (id in TEXTS) setTextColor(id, fg)
        setTextViewText(R.id.aqi, r?.aqi?.toString() ?: "--")
        setTextViewText(R.id.label, r?.let { label(it.aqi) } ?: "Tap to set up")
        setTextViewText(R.id.city, r?.city ?: "IQAir")
        setTextViewText(R.id.weather, r?.let { weather(it) } ?: "")
        setTextViewText(R.id.detail, r?.let { "${it.pollutant} · 🌬 %.1f m/s".format(it.windMs) } ?: "")
        setTextViewText(R.id.time, status)
        setOnClickPendingIntent(R.id.root, if (r == null) openApp(ctx) else refreshIntent(ctx))
    }

    private fun notification(ctx: Context, p: SharedPreferences, r: Reading?, status: String) {
        val nm = ctx.getSystemService(NotificationManager::class.java)
        if (!p.getBoolean("notify", false)) {
            nm.cancel(NOTIFICATION_ID)
            return
        }
        // Default importance without sound: One UI can hide "silent" notifications from the lock screen and AOD
        nm.createNotificationChannel(NotificationChannel(CHANNEL, "Air quality on lock screen", NotificationManager.IMPORTANCE_DEFAULT).apply {
            setSound(null, null)
            enableVibration(false)
            setShowBadge(false)
            lockscreenVisibility = Notification.VISIBILITY_PUBLIC
        })
        val number = r?.aqi?.toString() ?: "--"
        val notification = Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(badge(number, null, Color.WHITE)) // the number itself is the status bar / AOD icon
            .setLargeIcon(badge(number, r?.let { color(it.aqi) } ?: Color.LTGRAY, r?.let { textColor(it.aqi) } ?: textColor(0)))
            .setContentTitle(r?.let { "AQI ${it.aqi} · ${label(it.aqi)}" } ?: "IQAir widget: tap to set up")
            .setContentText(listOfNotNull(r?.city, r?.let { weather(it) }, status).filter { it.isNotEmpty() }.joinToString(" · "))
            .setColor(r?.let { color(it.aqi) } ?: Color.GRAY)
            .setShowWhen(false)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setVisibility(Notification.VISIBILITY_PUBLIC)
            .setCategory(Notification.CATEGORY_STATUS)
            .setContentIntent(openApp(ctx))
            .build()
        nm.notify(NOTIFICATION_ID, notification)
    }

    /** [text] drawn as an icon, on a filled circle when [bg] is set. */
    private fun badge(text: String, bg: Int?, fg: Int): Icon {
        val bmp = Bitmap.createBitmap(96, 96, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(bmp)
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { typeface = Typeface.DEFAULT_BOLD; textAlign = Paint.Align.CENTER }
        if (bg != null) canvas.drawCircle(48f, 48f, 48f, paint.apply { color = bg })
        paint.color = fg
        paint.textSize = 80f
        paint.textSize = min(80f, 80f * (if (bg == null) 94 else 74) / paint.measureText(text))
        canvas.drawText(text, 48f, 48f - (paint.descent() + paint.ascent()) / 2, paint)
        return Icon.createWithBitmap(bmp)
    }

    private fun schedule(ctx: Context, p: SharedPreferences) {
        val alarms = ctx.getSystemService(AlarmManager::class.java)
        val pi = refreshIntent(ctx)
        if (!inUse(ctx, p)) {
            alarms.cancel(pi)
            return
        }
        val at = maxOf(p.getLong("tried", 0) + intervalMs(p), System.currentTimeMillis() + 10_000)
        // Wake-up + allow-while-idle so the lock screen / AOD number keeps updating with the screen off
        if (alarms.canScheduleExactAlarms()) {
            alarms.setExactAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi)
        } else {
            alarms.setAndAllowWhileIdle(AlarmManager.RTC_WAKEUP, at, pi)
        }
    }

    private fun refreshIntent(ctx: Context): PendingIntent = PendingIntent.getBroadcast(
        ctx, 0, Intent(ctx, UpdateReceiver::class.java).setAction(REFRESH), PendingIntent.FLAG_IMMUTABLE,
    )

    private fun openApp(ctx: Context): PendingIntent = PendingIntent.getActivity(
        ctx, 0, Intent(ctx, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE,
    )
}
