package ru.ukwood.gyvertwink

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.text.InputType
import android.view.ViewGroup
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import ru.ukwood.gyvertwink.core.Net

/** Стартовый экран: поиск гирлянд по сети, ввод адреса вручную, последняя гирлянда. Дальше интерфейс - веб-страница гирлянды. */
class ConnectActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var list: LinearLayout
    private lateinit var input: EditText
    private lateinit var error: TextView
    private var generation = 0
    private val found = LinkedHashSet<String>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val last = Prefs.lastHost(this)
        if (last != null && !intent.getBooleanExtra(EXTRA_NO_AUTO, false) && Net.normalizeHost(last) != null) {
            open(last)
            return
        }
        build()
        search()
    }

    private fun build() {
        val pad = Ui.dp(this, 16)
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
        }
        col.addView(Ui.label(this, "GyverTwink", 24f, Ui.fg, true))
        col.addView(Ui.gap(this, 4))
        status = Ui.label(this, "", 14f, Ui.muted)
        col.addView(status)
        col.addView(Ui.gap(this, 12))

        list = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        col.addView(list)
        col.addView(Ui.gap(this, 4))
        col.addView(Ui.button(this, "Искать снова", false) { search() })

        col.addView(Ui.gap(this, 24))
        col.addView(Ui.label(this, "Ввести адрес вручную", 16f, Ui.fg, true))
        col.addView(Ui.label(this, "IP-адрес (192.168.1.50) или имя (gyvertwink.local)", 13f, Ui.muted))
        col.addView(Ui.gap(this, 6))
        input = EditText(this).apply {
            setSingleLine()
            inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_URI
            imeOptions = EditorInfo.IME_ACTION_GO
            setTextColor(Ui.fg)
            setHintTextColor(Ui.muted)
            hint = "192.168.1.50"
            minHeight = Ui.dp(context, 48)
            setText(Prefs.lastHost(context) ?: "")
            setOnEditorActionListener { _, _, _ -> openTyped(); true }
        }
        col.addView(input)
        error = Ui.label(this, "", 13f, Ui.bad)
        col.addView(error)
        col.addView(Ui.gap(this, 6))
        col.addView(Ui.button(this, "Открыть", true) { openTyped() })

        col.addView(Ui.gap(this, 24))
        col.addView(Ui.label(this, "Гирлянда не в вашей сети? Подключитесь к её точке доступа GyverTwink и откройте 192.168.4.1.", 13f, Ui.muted))
        col.addView(Ui.gap(this, 6))
        col.addView(Ui.button(this, "Точка доступа (192.168.4.1)", false) { open("192.168.4.1") })

        val sv = ScrollView(this).apply {
            setBackgroundColor(Ui.bg)
            fitsSystemWindows = true
            addView(col, ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT))
        }
        setContentView(sv)
    }

    private fun search() {
        val gen = ++generation
        found.clear()
        list.removeAllViews()
        status.text = "Поиск гирлянд в сети…"
        Thread {
            Discovery.search(this, { gen != generation || isDestroyed }) { ip ->
                runOnUiThread { if (gen == generation) add(ip) }
            }
            runOnUiThread {
                if (gen == generation) {
                    status.text = if (found.isEmpty()) "Гирлянды не найдены. Проверьте, что телефон в той же сети WiFi, или введите адрес вручную."
                    else "Найдено: ${found.size}. Выберите гирлянду."
                }
            }
        }.start()
    }

    private fun add(ip: String) {
        if (!found.add(ip)) return
        list.addView(Ui.button(this, ip, true) { open(ip) }, LinearLayout.LayoutParams(
            ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT).apply { bottomMargin = Ui.dp(this@ConnectActivity, 8) })
        status.text = "Найдено: ${found.size}"
    }

    private fun openTyped() {
        val host = Net.normalizeHost(input.text.toString())
        if (host == null) {
            error.text = "Нужен IP-адрес или имя без http:// и пути"
            return
        }
        error.text = ""
        open(host)
    }

    private fun open(host: String) {
        generation++
        Prefs.setLastHost(this, host)
        startActivity(Intent(this, WebActivity::class.java).putExtra(WebActivity.EXTRA_HOST, host))
        finish()
    }

    override fun onDestroy() {
        generation++
        super.onDestroy()
    }

    companion object {
        const val EXTRA_NO_AUTO = "noAuto"
    }
}
