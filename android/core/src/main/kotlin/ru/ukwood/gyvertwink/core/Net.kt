package ru.ukwood.gyvertwink.core

/** Работа с адресами IPv4 без зависимостей от Android. */
object Net {
    /** Широковещательный адрес подсети по адресу интерфейса и длине префикса; null при неверных данных. */
    fun broadcast(ip: ByteArray, prefix: Int): ByteArray? {
        if (ip.size != 4 || prefix !in 1..30) return null
        val mask = (-1 shl (32 - prefix))
        var v = 0
        for (b in ip) v = (v shl 8) or (b.toInt() and 0xFF)
        val bc = v or mask.inv()
        return ByteArray(4) { i -> (bc ushr (24 - 8 * i)).toByte() }
    }

    fun format(ip: ByteArray): String = ip.joinToString(".") { (it.toInt() and 0xFF).toString() }

    /**
     * Проверяет и приводит к виду «хост» или «хост:порт» адрес, введённый вручную: IPv4 или имя (например gyvertwink.local).
     * Схема и путь не допускаются. Возвращает null, если ввод недопустим.
     */
    fun normalizeHost(input: String): String? {
        val s = input.trim().lowercase()
        if (s.isEmpty() || s.length > 64) return null
        val parts = s.split(':')
        if (parts.size > 2) return null
        val host = parts[0]
        if (parts.size == 2) {
            val port = parts[1].toIntOrNull() ?: return null
            if (port !in 1..65535 || parts[1].length > 5) return null
        }
        val octets = host.split('.')
        if (octets.size == 4 && octets.all { it.isNotEmpty() && it.all(Char::isDigit) }) {
            if (octets.any { it.length > 3 || it.toInt() > 255 }) return null
            return s
        }
        val label = Regex("^[a-z0-9]([a-z0-9-]{0,61}[a-z0-9])?$")
        if (octets.isEmpty() || !octets.all { label.matches(it) }) return null
        if (octets.last().all(Char::isDigit)) return null   // «1.2.3» - неполный IP, а не имя
        return s
    }

    /** Адрес страницы гирлянды. */
    fun pageUrl(host: String): String = "http://$host/"

    /** Своя ли это страница: переход на другой хост приложение открывает в браузере, а не в оболочке. */
    fun sameHost(url: String, host: String): Boolean {
        val rest = url.removePrefix("http://")
        if (rest == url) return false
        val authority = rest.substringBefore('/').substringBefore('?').substringBefore('#')
        return authority.lowercase() == host
    }
}
