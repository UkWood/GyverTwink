package ru.ukwood.gyvertwink

import android.content.Context

/** Запоминает последнюю открытую гирлянду. Токен доступа хранит сама веб-страница (localStorage своего адреса). */
object Prefs {
    private fun sp(ctx: Context) = ctx.getSharedPreferences("gt", Context.MODE_PRIVATE)

    fun lastHost(ctx: Context): String? = sp(ctx).getString("host", null)

    fun setLastHost(ctx: Context, host: String) {
        sp(ctx).edit().putString("host", host).apply()
    }
}
