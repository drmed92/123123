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
import org.json.JSONException
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.net.UnknownHostException
import java.time.Duration
import java.time.Instant
import java.util.Date
import kotlin.math.floor
import kotlin.math.min
import kotlin.math.roundToInt

/** One air reading ready to display: outdoor from IQAir, or indoor from a ThingSpeak sensor channel. */
class Reading(val aqi: Int, val place: String, val tempC: Int?, val humidity: Int?, val detail: String) {
    companion object {
        private val POLLUTANTS = mapOf("p2" to "PM2.5", "p1" to "PM10", "o3" to "Ozone", "n2" to "NO₂", "s2" to "SO₂", "co" to "CO")

        /** From the `data` object of IQAir /v2/city. */
        fun outdoor(data: JSONObject): Reading {
            val current = data.getJSONObject("current")
            val pollution = current.getJSONObject("pollution")
            val weather = current.getJSONObject("weather")
            val main = pollution.optString("mainus")
            return Reading(
                pollution.getInt("aqius"), data.getString("city"), weather.getInt("tp"), weather.getInt("hu"),
                "${POLLUTANTS[main] ?: main} · 🌬 %.1f m/s".format(weather.optDouble("ws", 0.0)),
            )
        }

        /** From ThingSpeak feeds.json?results=1. Fields are found by name (pm2.5, pm10, co2, temp, hum), not position. */
        fun indoor(json: JSONObject): Reading {
            val channel = json.getJSONObject("channel")
            val feed = json.getJSONArray("feeds").getJSONObject(0)
            fun field(vararg names: String) = (1..8)
                .firstOrNull { channel.optString("field$it").lowercase().filter(Char::isLetterOrDigit) in names }
                ?.let { feed.optString("field$it").toDoubleOrNull() }
            val pm25 = field("pm25") ?: throw JSONException("no PM2.5 reading")
            val co2 = field("co2")
            return Reading(
                maxOf(Aqi.aqiPm25(pm25), field("pm10")?.let { Aqi.aqiPm10(it) } ?: 0),
                "Indoor", field("temp", "temperature")?.roundToInt(), field("hum", "humidity")?.roundToInt(),
                listOfNotNull(co2?.let { "CO₂ ${it.roundToInt()} ppm" }, "PM2.5 $pm25 µg/m³").joinToString(" · "),
            )
        }
    }
}

object Aqi {
    const val REFRESH = "com.drmed92.iqairwidget.REFRESH"
    private const val CHANNEL = "aqi"
    private const val NOTIFICATION_ID = 1

    /** The text views of one reading's panel; comparison layouts have a second panel. */
    private class Panel(val bg: Int, val aqi: Int, val unit: Int, val label: Int, val place: Int, val weather: Int, val detail: Int)

    private val PANELS = listOf(
        Panel(R.id.bg, R.id.aqi, R.id.unit, R.id.label, R.id.city, R.id.weather, R.id.detail),
        Panel(R.id.bg2, R.id.aqi2, R.id.unit2, R.id.label2, R.id.city2, R.id.weather2, R.id.detail2),
    )

    // EPA US AQI breakpoints in µg/m³ (PM2.5 per the 2024 update, which IQAir also uses)
    private val AQI_BANDS = listOf(0 to 50, 51 to 100, 101 to 150, 151 to 200, 201 to 300, 301 to 500)
    private val PM25_BANDS = listOf(0.0 to 9.0, 9.1 to 35.4, 35.5 to 55.4, 55.5 to 125.4, 125.5 to 225.4, 225.5 to 325.4)
    private val PM10_BANDS = listOf(0.0 to 54.0, 55.0 to 154.0, 155.0 to 254.0, 255.0 to 354.0, 355.0 to 424.0, 425.0 to 604.0)

    fun prefs(ctx: Context): SharedPreferences = ctx.getSharedPreferences("aqi", Context.MODE_PRIVATE)

    fun intervalMs(p: SharedPreferences) = p.getInt("interval", 5) * 60_000L

    /** What a widget shows: "out" (IQAir), "in" (ThingSpeak sensor) or "both" side by side. */
    fun mode(p: SharedPreferences, widgetId: Int) = p.getString("mode_$widgetId", "out")!!

    private fun shows(mode: String, source: String) = mode == source || mode == "both"

    // Pref key prefix per source; outdoor keeps the unprefixed keys of earlier versions
    private fun prefix(source: String) = if (source == "in") "in_" else ""

    // EPA truncates PM2.5 to 0.1 and PM10 to 1 µg/m³ before looking up the band
    fun aqiPm25(c: Double) = interpolate(floor(c * 10) / 10, PM25_BANDS)

    fun aqiPm10(c: Double) = interpolate(floor(c), PM10_BANDS)

    private fun interpolate(c: Double, bands: List<Pair<Double, Double>>): Int {
        val i = bands.indexOfFirst { c <= it.second }.takeIf { it >= 0 } ?: bands.lastIndex // above 500: extend the top band
        val (cLo, cHi) = bands[i]
        val (aLo, aHi) = AQI_BANDS[i]
        return ((aHi - aLo) * (maxOf(c, 0.0) - cLo) / (cHi - cLo) + aLo).roundToInt()
    }

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

    fun weather(r: Reading) = listOfNotNull(r.tempC?.let { "$it°C" }, r.humidity?.let { "💧$it%" }).joinToString(" · ")

    /** GETs JSON; both APIs put the failure reason in an error body, so that is parsed too. */
    private fun getJson(url: String): Pair<Int, JSONObject> {
        val conn = URL(url).openConnection() as HttpURLConnection
        conn.connectTimeout = 8000
        conn.readTimeout = 8000
        try {
            val stream = if (conn.responseCode < 400) conn.inputStream else conn.errorStream ?: throw IOException("HTTP ${conn.responseCode}")
            return conn.responseCode to JSONObject(stream.bufferedReader().use { it.readText() })
        } finally {
            conn.disconnect()
        }
    }

    /** Calls the IQAir AirVisual v2 API; failures throw with IQAir's reason, e.g. "call limit reached". */
    fun api(path: String, key: String, vararg query: Pair<String, String>): JSONObject {
        val qs = (query.toList() + ("key" to key)).joinToString("&") { (k, v) -> "$k=" + URLEncoder.encode(v, "UTF-8") }
        val (code, json) = getJson("https://api.airvisual.com/v2/$path?$qs")
        if (json.optString("status") != "success") {
            throw IOException(json.optJSONObject("data")?.optString("message")?.replace('_', ' ') ?: "HTTP $code")
        }
        return json
    }

    fun list(path: String, key: String, field: String, vararg query: Pair<String, String>): List<String> {
        val data = api(path, key, *query).getJSONArray("data")
        return (0 until data.length()).map { data.getJSONObject(it).getString(field) }
    }

    private fun thingSpeak(channel: String): JSONObject {
        val (code, json) = getJson("https://api.thingspeak.com/channels/${URLEncoder.encode(channel, "UTF-8")}/feeds.json?results=1")
        if (code >= 400) throw IOException("channel ${json.optString("error", "HTTP $code").lowercase()}")
        Reading.indoor(json) // reject unusable data before it replaces the cached reading
        val minutes = Duration.between(Instant.parse(json.getJSONArray("feeds").getJSONObject(0).getString("created_at")), Instant.now()).toMinutes()
        if (minutes > 30) throw IOException("sensor silent for $minutes min")
        return json
    }

    fun outdoor(p: SharedPreferences): Reading? = p.getString("last", null)?.let { Reading.outdoor(JSONObject(it)) }

    fun indoor(p: SharedPreferences): Reading? = p.getString("in_last", null)?.let { Reading.indoor(JSONObject(it)) }

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
        if (fetch) fetch(ctx, p)
        render(ctx, p)
        schedule(ctx, p)
    }

    private fun fetch(ctx: Context, p: SharedPreferences) {
        val modes = widgetIds(ctx).map { mode(p, it) } + if (p.getBoolean("notify", false)) listOf("both") else emptyList()
        // Only poll sources on screen, to spare the IQAir quota; with nothing on screen (testing from the app) poll all
        fun wanted(source: String) = modes.isEmpty() || modes.any { shows(it, source) }
        val edit = p.edit().putLong("tried", System.currentTimeMillis())
        val key = p.getString("key", "")!!
        val city = p.getString("city", "")!!
        if (key.isNotEmpty() && city.isNotEmpty() && wanted("out")) store(edit, "out") {
            val country = p.getString("country", "")!!
            api("city", key, "city" to city, "state" to p.getString("state", "")!!, "country" to country)
                .getJSONObject("data").also { Reading.outdoor(it) } // reject malformed data before caching it
        }
        val channel = p.getString("channel", "")!!
        if (channel.isNotEmpty() && wanted("in")) store(edit, "in") { thingSpeak(channel) }
        edit.commit()
    }

    /** Caches what [get] returns for [source], or records why it failed while keeping the last good reading. */
    private fun store(edit: SharedPreferences.Editor, source: String, get: () -> JSONObject) {
        val pre = prefix(source)
        try {
            edit.putString(pre + "last", get().toString()).putLong(pre + "at", System.currentTimeMillis()).remove(pre + "error")
        } catch (e: UnknownHostException) {
            edit.putString(pre + "error", "offline")
        } catch (e: Exception) {
            edit.putString(pre + "error", e.message ?: e.javaClass.simpleName)
        }
    }

    /** "⚠ reason" when a source shown in [mode] failed, else when it last updated. */
    private fun status(ctx: Context, p: SharedPreferences, mode: String): String {
        val sources = listOf("in", "out").filter { shows(mode, it) }
        val errors = sources.mapNotNull { s -> p.getString(prefix(s) + "error", null)?.let { if (s == "in") "indoor: $it" else it } }
        if (errors.isNotEmpty()) return "⚠ " + errors.joinToString("; ")
        val at = sources.maxOf { p.getLong(prefix(it) + "at", 0) }
        return if (at == 0L) "" else "Updated " + DateFormat.getTimeFormat(ctx).format(Date(at))
    }

    private fun render(ctx: Context, p: SharedPreferences) {
        val outdoor = "Outdoor" to outdoor(p)
        val indoor = "Indoor" to indoor(p)
        val mgr = AppWidgetManager.getInstance(ctx)
        widgetIds(ctx).groupBy { mode(p, it) }.forEach { (mode, ids) ->
            val panels = when (mode) {
                "both" -> listOf(indoor, outdoor)
                "in" -> listOf(indoor)
                else -> listOf(outdoor)
            }
            val layouts = if (mode == "both") listOf(R.layout.widget_small_both, R.layout.widget_wide_both, R.layout.widget_large_both)
            else listOf(R.layout.widget_small, R.layout.widget_wide, R.layout.widget_large)
            val status = status(ctx, p, mode)
            // The launcher picks the largest layout that fits each widget's current size
            mgr.updateAppWidget(ids.toIntArray(), RemoteViews(mapOf(
                SizeF(40f, 40f) to widget(ctx, layouts[0], panels, status),
                SizeF(110f, 40f) to widget(ctx, layouts[1], panels, status),
                SizeF(180f, 100f) to widget(ctx, layouts[2], panels, status),
            )))
        }
        notification(ctx, p, outdoor.second, indoor.second)
    }

    // [panels] pairs a name shown before the first reading with the reading. Setters for views a layout
    // doesn't have are skipped by RemoteViews.
    private fun widget(ctx: Context, layout: Int, panels: List<Pair<String, Reading?>>, status: String) =
        RemoteViews(ctx.packageName, layout).apply {
            panels.forEachIndexed { i, (name, r) ->
                val v = PANELS[i]
                setInt(v.bg, "setColorFilter", r?.let { color(it.aqi) } ?: Color.LTGRAY)
                val fg = textColor(r?.aqi ?: 0)
                for (id in intArrayOf(v.aqi, v.unit, v.label, v.place, v.weather, v.detail, R.id.time)) setTextColor(id, fg)
                setTextViewText(v.aqi, r?.aqi?.toString() ?: "--")
                setTextViewText(v.label, r?.let { label(it.aqi) } ?: "Tap to set up")
                setTextViewText(v.place, r?.place ?: name)
                setTextViewText(v.weather, r?.let { weather(it) } ?: "")
                setTextViewText(v.detail, r?.detail ?: "")
            }
            setTextViewText(R.id.time, status)
            setOnClickPendingIntent(R.id.root, if (panels.all { it.second == null }) openApp(ctx) else refreshIntent(ctx))
        }

    private fun notification(ctx: Context, p: SharedPreferences, outdoor: Reading?, indoor: Reading?) {
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
        val main = outdoor ?: indoor
        val number = main?.aqi?.toString() ?: "--"
        val text = listOf(main?.let { weather(it) }, indoor?.takeIf { outdoor != null }?.let { "Indoor AQI ${it.aqi}" }, status(ctx, p, "both"))
        val notification = Notification.Builder(ctx, CHANNEL)
            .setSmallIcon(badge(number, null, Color.WHITE)) // the number itself is the status bar / AOD icon
            .setLargeIcon(badge(number, main?.let { color(it.aqi) } ?: Color.LTGRAY, textColor(main?.aqi ?: 0)))
            .setContentTitle(main?.let { "${it.place} AQI ${it.aqi} · ${label(it.aqi)}" } ?: "IQAir widget: tap to set up")
            .setContentText(text.filterNot { it.isNullOrEmpty() }.joinToString(" · "))
            .setColor(main?.let { color(it.aqi) } ?: Color.GRAY)
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
