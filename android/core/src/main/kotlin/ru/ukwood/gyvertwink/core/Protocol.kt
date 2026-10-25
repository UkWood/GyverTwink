package ru.ukwood.gyvertwink.core

/** Поиск гирлянды по UDP (docs/UDP_protocol.txt): запрос {'G','T',0}, ответ {'G','T',0,последний_октет_ip}. */
object Protocol {
    const val UDP_PORT = 8888

    /** Пакет «запрос IP». */
    val discoverRequest: ByteArray = byteArrayOf('G'.code.toByte(), 'T'.code.toByte(), 0)

    /** Ответ ли это на поиск гирлянды. Адрес устройства берётся из отправителя, а не из пакета. */
    fun isDiscoverReply(data: ByteArray, len: Int): Boolean =
        len >= 4 && data[0] == 'G'.code.toByte() && data[1] == 'T'.code.toByte() && data[2] == 0.toByte()
}
