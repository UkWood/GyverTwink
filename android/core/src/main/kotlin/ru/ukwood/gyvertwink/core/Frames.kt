package ru.ukwood.gyvertwink.core

/** Кадр в оттенках серого: яркость 0..255, строки подряд без отступов. */
class Gray(val w: Int, val h: Int, val data: ByteArray)

object Resample {
    /**
     * Из плоскости Y кадра камеры делает кадр нужной ширины: поворачивает на [rotation] градусов по часовой стрелке
     * (0/90/180/270, чтобы изображение стало «прямым») и уменьшает усреднением блоков (светодиод остаётся пятном, а не пропадает).
     * rowStride и pixelStride - как у Image.Plane; байты за концом буфера считаются чёрными.
     */
    fun fromYPlane(y: ByteArray, srcW: Int, srcH: Int, rowStride: Int, pixelStride: Int, rotation: Int, dstW: Int): Gray {
        require(rotation % 90 == 0 && srcW > 0 && srcH > 0 && dstW > 0)
        val r = ((rotation % 360) + 360) % 360
        val rw = if (r == 0 || r == 180) srcW else srcH
        val rh = if (r == 0 || r == 180) srcH else srcW
        val w = minOf(dstW, rw)
        val h = maxOf(1, (rh.toLong() * w / rw).toInt())
        val out = ByteArray(w * h)
        for (dy in 0 until h) {
            val y0 = (dy.toLong() * rh / h).toInt()
            val y1 = maxOf(y0 + 1, ((dy + 1).toLong() * rh / h).toInt())
            for (dx in 0 until w) {
                val x0 = (dx.toLong() * rw / w).toInt()
                val x1 = maxOf(x0 + 1, ((dx + 1).toLong() * rw / w).toInt())
                var sum = 0
                var cnt = 0
                for (ry in y0 until y1) {
                    for (rx in x0 until x1) {
                        val sx: Int
                        val sy: Int
                        when (r) {
                            0 -> { sx = rx; sy = ry }
                            90 -> { sx = ry; sy = srcH - 1 - rx }
                            180 -> { sx = srcW - 1 - rx; sy = srcH - 1 - ry }
                            else -> { sx = srcW - 1 - ry; sy = rx }
                        }
                        val i = sy * rowStride + sx * pixelStride
                        if (i in y.indices) sum += y[i].toInt() and 0xFF
                        cnt++
                    }
                }
                out[dy * w + dx] = (sum / cnt).toByte()
            }
        }
        return Gray(w, h, out)
    }
}

/** Кадры записи в памяти: время от начала записи (мс) -> кадр; поиск ближайшего по времени. Общий объём ограничен. */
class FrameStore(private val maxBytes: Long) {
    private val times = ArrayList<Long>()
    private val frames = ArrayList<Gray>()
    private var bytes = 0L

    val size: Int get() = frames.size
    val lastMs: Long get() = if (times.isEmpty()) 0 else times[times.size - 1]

    /** false - не поместился в лимит памяти (кадр не добавлен). Время должно расти. */
    fun add(tMs: Long, g: Gray): Boolean {
        if (bytes + g.data.size > maxBytes) return false
        if (times.isNotEmpty() && tMs < times[times.size - 1]) return false
        times.add(tMs)
        frames.add(g)
        bytes += g.data.size
        return true
    }

    fun clear() {
        times.clear()
        frames.clear()
        bytes = 0
    }

    /** Кадр, снятый ближе всего к моменту tMs (до первого и после последнего - крайние). null - кадров нет. */
    fun nearest(tMs: Long): Gray? {
        if (frames.isEmpty()) return null
        var lo = 0
        var hi = times.size - 1
        while (lo < hi) {
            val mid = (lo + hi) ushr 1
            if (times[mid] < tMs) lo = mid + 1 else hi = mid
        }
        if (lo > 0 && tMs - times[lo - 1] <= times[lo] - tMs) lo--
        return frames[lo]
    }
}

object CameraPick {
    /** Размер кадра камеры: самый маленький, у которого большая сторона не меньше [minLong]; иначе самый большой. */
    fun pickSize(sizes: List<Pair<Int, Int>>, minLong: Int): Pair<Int, Int>? {
        if (sizes.isEmpty()) return null
        val ok = sizes.filter { maxOf(it.first, it.second) >= minLong }
        return if (ok.isNotEmpty()) ok.minByOrNull { it.first.toLong() * it.second } else sizes.maxByOrNull { it.first.toLong() * it.second }
    }

    /** Угол поворота кадра датчика по часовой стрелке, чтобы изображение стало прямым (основная камера). displayDeg: 0/90/180/270. */
    fun rotation(sensorOrientation: Int, displayDeg: Int): Int = ((sensorOrientation - displayDeg) % 360 + 360) % 360
}
