package ru.ukwood.gyvertwink.core

import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class CoreTest {
    private fun ip(a: Int, b: Int, c: Int, d: Int) = byteArrayOf(a.toByte(), b.toByte(), c.toByte(), d.toByte())

    @Test
    fun discoverRequestIsGtZero() {
        assertContentEquals(byteArrayOf(0x47, 0x54, 0), Protocol.discoverRequest)
    }

    @Test
    fun discoverReply() {
        assertTrue(Protocol.isDiscoverReply(byteArrayOf(0x47, 0x54, 0, 42), 4))
        assertFalse(Protocol.isDiscoverReply(byteArrayOf(0x47, 0x54, 0, 42), 3))      // короткий пакет
        assertFalse(Protocol.isDiscoverReply(byteArrayOf(0x47, 0x54, 1, 42), 4))      // другая команда
        assertFalse(Protocol.isDiscoverReply(byteArrayOf(0x48, 0x54, 0, 42), 4))      // чужой заголовок
        assertFalse(Protocol.isDiscoverReply(byteArrayOf(0x47, 0x54, 0, 42), 0))
    }

    @Test
    fun broadcast() {
        assertEquals("192.168.1.255", Net.format(Net.broadcast(ip(192, 168, 1, 37), 24)!!))
        assertEquals("192.168.3.255", Net.format(Net.broadcast(ip(192, 168, 2, 10), 23)!!))
        assertEquals("10.255.255.255", Net.format(Net.broadcast(ip(10, 1, 2, 3), 8)!!))
        assertEquals("192.168.1.127", Net.format(Net.broadcast(ip(192, 168, 1, 5), 25)!!))
        assertNull(Net.broadcast(ip(1, 2, 3, 4), 0))
        assertNull(Net.broadcast(ip(1, 2, 3, 4), 31))
        assertNull(Net.broadcast(byteArrayOf(1, 2, 3), 24))
    }

    @Test
    fun normalizeHostAccepts() {
        assertEquals("192.168.1.50", Net.normalizeHost(" 192.168.1.50 "))
        assertEquals("192.168.1.50:8080", Net.normalizeHost("192.168.1.50:8080"))
        assertEquals("gyvertwink.local", Net.normalizeHost("GyverTwink.local"))
        assertEquals("garland", Net.normalizeHost("garland"))
    }

    @Test
    fun normalizeHostRejects() {
        for (bad in listOf("", "  ", "http://192.168.1.1", "192.168.1.1/", "192.168.1.256", "1.2.3", "a b", "host:", "host:0",
            "host:65536", "host:1:2", "-bad", "bad-", "a..b", "x".repeat(65), "192.168.1.1?x=1", "user@host")) {
            assertNull(Net.normalizeHost(bad), "должно отклоняться: '$bad'")
        }
    }

    @Test
    fun sameHost() {
        assertTrue(Net.sameHost("http://192.168.1.50/", "192.168.1.50"))
        assertTrue(Net.sameHost("http://192.168.1.50/api/state?x=1", "192.168.1.50"))
        assertTrue(Net.sameHost("http://192.168.1.50:8080/x", "192.168.1.50:8080"))
        assertFalse(Net.sameHost("http://192.168.1.51/", "192.168.1.50"))
        assertFalse(Net.sameHost("https://192.168.1.50/", "192.168.1.50"))
        assertFalse(Net.sameHost("http://192.168.1.50.evil.com/", "192.168.1.50"))
        assertFalse(Net.sameHost("http://192.168.1.50@evil.com/", "192.168.1.50"))
        assertFalse(Net.sameHost("javascript:alert(1)", "192.168.1.50"))
    }
}
