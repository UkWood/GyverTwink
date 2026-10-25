package ru.ukwood.gyvertwink.core

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue

class FramesTest {
    // кадр 4x2:
    //  1 2 3 4
    //  5 6 7 8
    private val src = byteArrayOf(1, 2, 3, 4, 5, 6, 7, 8)

    private fun Gray.rows(): List<List<Int>> = (0 until h).map { y -> (0 until w).map { x -> data[y * w + x].toInt() and 0xFF } }

    @Test
    fun rotate0() {
        val g = Resample.fromYPlane(src, 4, 2, 4, 1, 0, 4)
        assertEquals(listOf(listOf(1, 2, 3, 4), listOf(5, 6, 7, 8)), g.rows())
    }

    @Test
    fun rotate90Clockwise() {
        val g = Resample.fromYPlane(src, 4, 2, 4, 1, 90, 2)
        // левый верхний угол исходного кадра уходит в правый верхний
        assertEquals(listOf(listOf(5, 1), listOf(6, 2), listOf(7, 3), listOf(8, 4)), g.rows())
    }

    @Test
    fun rotate180() {
        val g = Resample.fromYPlane(src, 4, 2, 4, 1, 180, 4)
        assertEquals(listOf(listOf(8, 7, 6, 5), listOf(4, 3, 2, 1)), g.rows())
    }

    @Test
    fun rotate270() {
        val g = Resample.fromYPlane(src, 4, 2, 4, 1, 270, 2)
        // левый верхний угол исходного кадра уходит в левый нижний
        assertEquals(listOf(listOf(4, 8), listOf(3, 7), listOf(2, 6), listOf(1, 5)), g.rows())
    }

    @Test
    fun rowStrideAndPixelStride() {
        // строка занимает 6 байт (отступ 2), шаг пикселя 1; в хвосте мусор
        val padded = byteArrayOf(1, 2, 3, 4, 99, 99, 5, 6, 7, 8, 99, 99)
        assertEquals(listOf(listOf(1, 2, 3, 4), listOf(5, 6, 7, 8)), Resample.fromYPlane(padded, 4, 2, 6, 1, 0, 4).rows())
        // шаг пикселя 2 (через один байт)
        val spaced = byteArrayOf(1, 0, 2, 0, 3, 0, 4, 0, 5, 0, 6, 0, 7, 0, 8, 0)
        assertEquals(listOf(listOf(1, 2, 3, 4), listOf(5, 6, 7, 8)), Resample.fromYPlane(spaced, 4, 2, 8, 2, 0, 4).rows())
    }

    @Test
    fun downscaleAveragesBlocks() {
        // 4x2 -> 2x1: средние по блокам 2x2: (1+2+5+6)/4 = 3, (3+4+7+8)/4 = 5
        val g = Resample.fromYPlane(src, 4, 2, 4, 1, 0, 2)
        assertEquals(listOf(listOf(3, 5)), g.rows())
    }

    @Test
    fun shortBufferIsBlackNotCrash() {
        val g = Resample.fromYPlane(byteArrayOf(10, 10, 10, 10), 4, 2, 4, 1, 0, 4)
        assertEquals(listOf(listOf(10, 10, 10, 10), listOf(0, 0, 0, 0)), g.rows())
    }

    @Test
    fun brightSpotSurvivesDownscale() {
        // яркая точка 255 среди 0 в блоке 4x4 даёт 15 (255/16), а не пропадает
        val big = ByteArray(16)
        big[5] = 255.toByte()
        val g = Resample.fromYPlane(big, 4, 4, 4, 1, 0, 1)
        assertEquals(15, g.data[0].toInt() and 0xFF)
    }

    @Test
    fun frameStoreNearest() {
        val s = FrameStore(1000)
        fun f(v: Int) = Gray(1, 1, byteArrayOf(v.toByte()))
        assertNull(s.nearest(5))
        assertTrue(s.add(0, f(0)))
        assertTrue(s.add(50, f(1)))
        assertTrue(s.add(100, f(2)))
        assertEquals(0, s.nearest(-10)!!.data[0].toInt())
        assertEquals(0, s.nearest(20)!!.data[0].toInt())
        assertEquals(1, s.nearest(30)!!.data[0].toInt())
        assertEquals(1, s.nearest(74)!!.data[0].toInt())
        assertEquals(2, s.nearest(76)!!.data[0].toInt())
        assertEquals(2, s.nearest(10_000)!!.data[0].toInt())
        assertEquals(100, s.lastMs)
        assertEquals(3, s.size)
    }

    @Test
    fun frameStoreLimitsAndOrder() {
        val s = FrameStore(10)
        assertTrue(s.add(0, Gray(1, 1, ByteArray(6))))
        assertFalse(s.add(10, Gray(1, 1, ByteArray(6))))   // не лезет в лимит
        assertTrue(s.add(10, Gray(1, 1, ByteArray(4))))
        assertFalse(s.add(5, Gray(1, 1, ByteArray(0))))    // время назад
        s.clear()
        assertEquals(0, s.size)
        assertTrue(s.add(0, Gray(1, 1, ByteArray(10))))
    }

    @Test
    fun pickSize() {
        val sizes = listOf(1920 to 1080, 640 to 480, 320 to 240, 1280 to 720, 800 to 600)
        assertEquals(640 to 480, CameraPick.pickSize(sizes, 480))
        assertEquals(320 to 240, CameraPick.pickSize(sizes, 100))
        assertEquals(1920 to 1080, CameraPick.pickSize(sizes, 5000))
        assertNull(CameraPick.pickSize(emptyList(), 480))
    }

    @Test
    fun rotationFromSensor() {
        assertEquals(90, CameraPick.rotation(90, 0))      // телефон в портрете, датчик повёрнут на 90
        assertEquals(0, CameraPick.rotation(90, 90))      // телефон повёрнут в альбом
        assertEquals(180, CameraPick.rotation(90, 270))
        assertEquals(270, CameraPick.rotation(90, 180))
        assertEquals(270, CameraPick.rotation(270, 0))
    }
}
