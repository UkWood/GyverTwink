package ru.ukwood.gyvertwink

import android.content.Context
import android.net.ConnectivityManager
import ru.ukwood.gyvertwink.core.Net
import ru.ukwood.gyvertwink.core.Protocol
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetAddress

/** Поиск гирлянд по UDP: широковещательный запрос {'G','T',0}, ответ приходит от самой гирлянды (адрес берём у отправителя). */
object Discovery {
    /** Широковещательный адрес подсети активной сети; null - сети нет. */
    fun localBroadcast(ctx: Context): InetAddress? {
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as ConnectivityManager
        val net = cm.activeNetwork ?: return null
        val props = cm.getLinkProperties(net) ?: return null
        for (la in props.linkAddresses) {
            val a = la.address
            if (a is Inet4Address && !a.isLoopbackAddress) {
                val bc = Net.broadcast(a.address, la.prefixLength) ?: continue
                return InetAddress.getByAddress(bc)
            }
        }
        return null
    }

    /**
     * Ищет гирлянды около 2,5 с (гирлянда отвечает с задержкой до ~0,5 с, чтобы ответы не накладывались) и сообщает о каждой найденной.
     * Блокирует поток; вызывать не из главного. stop() == true прерывает поиск.
     */
    fun search(ctx: Context, stop: () -> Boolean, onFound: (String) -> Unit) {
        val targets = mutableListOf<InetAddress>()
        localBroadcast(ctx)?.let { targets.add(it) }
        targets.add(InetAddress.getByAddress(byteArrayOf(-1, -1, -1, -1)))
        val seen = HashSet<String>()
        val buf = ByteArray(32)
        DatagramSocket().use { sock ->
            sock.broadcast = true
            sock.soTimeout = 200
            val end = System.currentTimeMillis() + 2500
            var nextSend = 0L
            var sent = 0
            while (System.currentTimeMillis() < end && !stop()) {
                if (sent < 3 && System.currentTimeMillis() >= nextSend) {
                    for (t in targets) {
                        try {
                            sock.send(DatagramPacket(Protocol.discoverRequest, Protocol.discoverRequest.size, t, Protocol.UDP_PORT))
                        } catch (_: Exception) {
                        }
                    }
                    sent++
                    nextSend = System.currentTimeMillis() + 600
                }
                try {
                    val p = DatagramPacket(buf, buf.size)
                    sock.receive(p)
                    if (Protocol.isDiscoverReply(buf, p.length)) {
                        val ip = p.address.hostAddress ?: continue
                        if (seen.add(ip)) onFound(ip)
                    }
                } catch (_: java.net.SocketTimeoutException) {
                } catch (_: Exception) {
                    return
                }
            }
        }
    }
}
