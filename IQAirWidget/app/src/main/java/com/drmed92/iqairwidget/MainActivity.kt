package com.drmed92.iqairwidget

import android.Manifest
import android.app.Activity
import android.app.AlertDialog
import android.appwidget.AppWidgetManager
import android.content.Intent
import android.content.SharedPreferences
import android.content.pm.PackageManager
import android.graphics.Color
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.PowerManager
import android.provider.Settings
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.RadioGroup
import android.widget.SeekBar
import android.widget.Switch
import android.widget.TextView
import android.widget.Toast
import kotlin.concurrent.thread

/** Settings: API key, city, update interval, lock screen notification. Also the widgets' "Settings" screen. */
class MainActivity : Activity() {
    private lateinit var prefs: SharedPreferences
    private lateinit var key: EditText
    private lateinit var placeView: TextView
    private lateinit var status: TextView
    private var place = listOf("", "", "") // country, state, city

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        prefs = Aqi.prefs(this)
        // Opened for a widget (when placed, or from its Settings): OK keeps the widget, and it can pick what it shows
        val widgetId = intent.getIntExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, AppWidgetManager.INVALID_APPWIDGET_ID)
        val forWidget = widgetId != AppWidgetManager.INVALID_APPWIDGET_ID
        setResult(RESULT_OK, Intent().putExtra(AppWidgetManager.EXTRA_APPWIDGET_ID, widgetId))
        val mode = findViewById<RadioGroup>(R.id.mode)
        val modeButtons = mapOf("out" to R.id.mode_out, "in" to R.id.mode_in, "both" to R.id.mode_both)
        if (forWidget) {
            mode.visibility = View.VISIBLE
            mode.check(modeButtons.getValue(Aqi.mode(prefs, widgetId)))
            sendBroadcast(Intent(this, UpdateReceiver::class.java)) // a newly placed widget gets no update of its own
        }

        key = findViewById(R.id.key)
        key.setText(prefs.getString("key", ""))
        val channel = findViewById<EditText>(R.id.channel)
        channel.setText(prefs.getString("channel", ""))

        place = listOf("country", "state", "city").map { prefs.getString(it, "")!! }
        placeView = findViewById(R.id.place)
        showPlace()
        findViewById<Button>(R.id.pick).setOnClickListener { pickCity() }

        val every = findViewById<TextView>(R.id.every)
        val quota = findViewById<TextView>(R.id.quota)
        val normalColor = quota.textColors
        fun showInterval(minutes: Int) {
            every.text = "Update every $minutes min"
            quota.text = "≈ ${1440 / minutes} calls/day, ${43200 / minutes}/month. A free IQAir key allows 500/day and " +
                "10,000/month, so stay at 5 min or slower. IQAir stations report about once an hour."
            if (minutes < 5) quota.setTextColor(Color.RED) else quota.setTextColor(normalColor)
        }
        val interval = findViewById<SeekBar>(R.id.interval)
        interval.progress = prefs.getInt("interval", 5)
        showInterval(interval.progress)
        interval.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(bar: SeekBar, minutes: Int, fromUser: Boolean) = showInterval(minutes)
            override fun onStartTrackingTouch(bar: SeekBar) {}
            override fun onStopTrackingTouch(bar: SeekBar) {}
        })

        val notify = findViewById<Switch>(R.id.notify)
        notify.isChecked = prefs.getBoolean("notify", false)
        notify.setOnCheckedChangeListener { _, on ->
            if (on && Build.VERSION.SDK_INT >= 33 &&
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED
            ) {
                requestPermissions(arrayOf(Manifest.permission.POST_NOTIFICATIONS), 1)
            }
        }

        findViewById<Button>(R.id.battery).setOnClickListener {
            startActivity(Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:$packageName")))
        }

        status = findViewById(R.id.status)
        showStatus()
        findViewById<Button>(R.id.save).setOnClickListener {
            val newChannel = channel.text.toString().trim()
            prefs.edit()
                .putString("key", key.text.toString().trim())
                .putString("country", place[0])
                .putString("state", place[1])
                .putString("city", place[2])
                .putInt("interval", interval.progress)
                .putBoolean("notify", notify.isChecked)
                .putString("channel", newChannel)
                .also { if (forWidget) it.putString("mode_$widgetId", modeButtons.entries.first { e -> e.value == mode.checkedRadioButtonId }.key) }
                // Another (or no) sensor: drop the old one's reading, it would otherwise pass as "Indoor"
                .also { if (newChannel != prefs.getString("channel", "")) it.remove("in_last").remove("in_at").remove("in_error") }
                .commit()
            status.text = "Updating…"
            thread {
                Aqi.update(applicationContext, fetch = true)
                runOnUiThread { showStatus() }
            }
            if (forWidget) finish()
        }
    }

    override fun onResume() {
        super.onResume()
        val unrestricted = getSystemService(PowerManager::class.java).isIgnoringBatteryOptimizations(packageName)
        findViewById<View>(R.id.battery).visibility = if (unrestricted) View.GONE else View.VISIBLE
    }

    private fun showPlace() {
        placeView.text = if (place[2].isEmpty()) "City: not chosen" else "City: ${place[2]}, ${place[1]}, ${place[0]}"
    }

    private fun showStatus() {
        fun line(r: Reading) = "${r.place}: AQI ${r.aqi} (${Aqi.label(r.aqi)}) · ${Aqi.weather(r)} · ${r.detail}"
        status.text = listOfNotNull(
            Aqi.outdoor(prefs)?.let(::line),
            prefs.getString("error", null)?.let { "Outdoor update failed: $it" },
            Aqi.indoor(prefs)?.let(::line),
            prefs.getString("in_error", null)?.let { "Indoor update failed: $it" },
        ).joinToString("\n")
    }

    private fun pickCity() {
        pick("Country", "countries", "country") { country ->
            pick("State / region", "states", "state", "country" to country) { state ->
                pick("City", "cities", "city", "country" to country, "state" to state) { city ->
                    place = listOf(country, state, city)
                    showPlace()
                }
            }
        }
    }

    /** Loads one IQAir list (one API call) and offers it as a dialog. */
    private fun pick(title: String, path: String, field: String, vararg query: Pair<String, String>, then: (String) -> Unit) {
        val apiKey = key.text.toString().trim()
        if (apiKey.isEmpty()) {
            toast("Paste your IQAir API key first")
            return
        }
        thread {
            val result = runCatching { Aqi.list(path, apiKey, field, *query) }
            runOnUiThread {
                if (isDestroyed) return@runOnUiThread
                result
                    .onSuccess { items -> AlertDialog.Builder(this).setTitle(title).setItems(items.toTypedArray()) { _, i -> then(items[i]) }.show() }
                    .onFailure { toast("IQAir: ${it.message}") }
            }
        }
    }

    private fun toast(msg: String) = Toast.makeText(this, msg, Toast.LENGTH_LONG).show()
}
