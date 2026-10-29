package com.drmed92.iqairwidget

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Test

class AqiTest {
    @Test
    fun bandsSwitchAtUsAqiBoundaries() {
        val expected = listOf(0 to "Good", 50 to "Good", 51 to "Moderate", 100 to "Moderate", 101 to "Unhealthy for sensitive groups",
            150 to "Unhealthy for sensitive groups", 151 to "Unhealthy", 200 to "Unhealthy", 201 to "Very unhealthy",
            300 to "Very unhealthy", 301 to "Hazardous", 999 to "Hazardous")
        for ((aqi, label) in expected) assertEquals("aqi $aqi", label, Aqi.label(aqi))
        assertEquals(0xFFA8E05F.toInt(), Aqi.color(50))
        assertEquals(0xFFFDD64B.toInt(), Aqi.color(51))
        assertEquals(0xFFA87383.toInt(), Aqi.color(301))
    }

    @Test
    fun convertsPmToUsAqiWithEpaBreakpoints() {
        // Band edges of the 2024 EPA PM2.5 table, truncation to 0.1, and a mid-band value
        val pm25 = listOf(0.0 to 0, 9.0 to 50, 9.05 to 50, 9.1 to 51, 12.0 to 56, 29.7 to 89, 35.4 to 100, 35.5 to 101,
            55.4 to 150, 55.5 to 151, 125.4 to 200, 225.4 to 300, 325.4 to 500, -3.0 to 0)
        for ((c, aqi) in pm25) assertEquals("PM2.5 $c", aqi, Aqi.aqiPm25(c))
        val pm10 = listOf(0.0 to 0, 31.7 to 29, 54.0 to 50, 54.9 to 50, 55.0 to 51, 154.0 to 100, 155.0 to 101, 604.0 to 500)
        for ((c, aqi) in pm10) assertEquals("PM10 $c", aqi, Aqi.aqiPm10(c))
    }

    @Test
    fun parsesIqairCityResponse() {
        // Sample from the IQAir API docs (GET /v2/city, Los Angeles)
        val data = JSONObject(
            """{"city":"Los Angeles","state":"California","country":"USA",
            "current":{"pollution":{"ts":"2025-09-08T08:00:00.000Z","aqius":36,"mainus":"p2","aqicn":19,"maincn":"p1"},
            "weather":{"ts":"2025-09-08T08:00:00.000Z","ic":"01n","hu":66,"pr":1017,"tp":20,"wd":146,"ws":1.57,"heatIndex":20}}}"""
        )
        val r = Reading.outdoor(data)
        assertEquals(36, r.aqi)
        assertEquals("Los Angeles", r.place)
        assertEquals("20°C · 💧66%", Aqi.weather(r))
        assertEquals("PM2.5 · 🌬 1.6 m/s", r.detail)
    }

    @Test
    fun parsesThingSpeakIndoorSensor() {
        // Shape of api.thingspeak.com/channels/3427507/feeds.json?results=1 (PMS7003 + CO2 sensor)
        val json = JSONObject(
            """{"channel":{"id":3427507,"name":"AirQuality","field1":"Pm1","field2":"pm2.5","field3":"pm10","field4":"co2",
            "field5":"temp","field6":"hum"},
            "feeds":[{"created_at":"2026-10-29T10:50:20Z","entry_id":96090,"field1":"17.7","field2":"29.7","field3":"31.7",
            "field4":"871","field5":"26.7","field6":"50.2"}]}"""
        )
        val r = Reading.indoor(json)
        assertEquals(89, r.aqi) // PM2.5 29.7 → 89 beats PM10 31.7 → 29
        assertEquals("Indoor", r.place)
        assertEquals("27°C · 💧50%", Aqi.weather(r))
        assertEquals("CO₂ 871 ppm · PM2.5 29.7 µg/m³", r.detail)
    }
}
