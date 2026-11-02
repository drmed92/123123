package com.drmed.datacounter;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class DataCounterTest {
    @Test public void delta() {
        assertEquals(500, DataCounter.delta(1000, 1500));
        assertEquals(0, DataCounter.delta(1500, 1500));
        assertEquals(300, DataCounter.delta(1500, 300)); // counters reset by a reboot
    }
}
