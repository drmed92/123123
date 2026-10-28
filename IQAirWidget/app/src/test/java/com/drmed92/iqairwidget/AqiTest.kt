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
    fun parsesIqairCityResponse() {
        // Sample from the IQAir API docs (GET /v2/city, Los Angeles)
        val data = JSONObject(
            """{"city":"Los Angeles","state":"California","country":"USA",
            "current":{"pollution":{"ts":"2025-09-08T08:00:00.000Z","aqius":36,"mainus":"p2","aqicn":19,"maincn":"p1"},
            "weather":{"ts":"2025-09-08T08:00:00.000Z","ic":"01n","hu":66,"pr":1017,"tp":20,"wd":146,"ws":1.57,"heatIndex":20}}}"""
        )
        val r = Reading.parse(data)
        assertEquals(36, r.aqi)
        assertEquals("PM2.5", r.pollutant)
        assertEquals(20, r.tempC)
        assertEquals(66, r.humidity)
        assertEquals(1.57, r.windMs, 0.0)
        assertEquals("Los Angeles", r.city)
        assertEquals("20°C · 💧66%", Aqi.weather(r))
    }
}
