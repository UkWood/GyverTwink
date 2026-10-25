package ru.ukwood.gyvertwink

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.ActivityNotFoundException
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.view.View
import android.view.ViewGroup
import android.webkit.JavascriptInterface
import android.webkit.JsResult
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceError
import android.webkit.WebResourceRequest
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.FrameLayout
import android.widget.LinearLayout
import android.widget.TextView
import ru.ukwood.gyvertwink.core.Net

/** Весь интерфейс - страница самой гирлянды. Оболочка добавляет только то, чего у страницы нет: поиск устройства, выбор файлов, окна подтверждения. */
class WebActivity : Activity() {
    private lateinit var web: WebView
    private lateinit var errorBox: LinearLayout
    private lateinit var errorText: TextView
    private lateinit var host: String
    private var failed = false
    private var fileCallback: ValueCallback<Array<Uri>>? = null
    private val live = LiveCamera(this)

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val h = intent.getStringExtra(EXTRA_HOST)?.let { Net.normalizeHost(it) }
        if (h == null) {
            changeDevice()
            return
        }
        host = h

        val root = FrameLayout(this).apply {
            setBackgroundColor(Ui.bg)
            fitsSystemWindows = true
        }
        web = WebView(this).apply {
            setBackgroundColor(Ui.bg)
            settings.javaScriptEnabled = true
            settings.domStorageEnabled = true            // localStorage: тут страница хранит токен доступа
            settings.mediaPlaybackRequiresUserGesture = false
            settings.allowFileAccess = false
            settings.allowContentAccess = false
            settings.userAgentString = settings.userAgentString + " GyverTwinkApp/" + version()
            addJavascriptInterface(Bridge(), "GTApp")
            webViewClient = Client()
            webChromeClient = Chrome()
        }
        root.addView(web, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))

        errorBox = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setBackgroundColor(Ui.bg)
            val pad = Ui.dp(context, 24)
            setPadding(pad, pad, pad, pad)
            gravity = android.view.Gravity.CENTER
            visibility = View.GONE
        }
        errorBox.addView(Ui.label(this, "Нет связи с гирляндой", 20f, Ui.fg, true))
        errorText = Ui.label(this, "", 14f, Ui.muted)
        errorBox.addView(errorText)
        errorBox.addView(Ui.gap(this, 16))
        errorBox.addView(Ui.button(this, "Повторить", true) { reload() })
        errorBox.addView(Ui.gap(this, 8))
        errorBox.addView(Ui.button(this, "Другая гирлянда", false) { changeDevice() })
        root.addView(errorBox, FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
        setContentView(root)

        if (savedInstanceState != null) web.restoreState(savedInstanceState) else web.loadUrl(Net.pageUrl(host))
    }

    private fun version(): String = try {
        packageManager.getPackageInfo(packageName, 0).versionName ?: "0"
    } catch (_: Exception) {
        "0"
    }

    private fun reload() {
        failed = false
        errorBox.visibility = View.GONE
        web.loadUrl(Net.pageUrl(host))
    }

    private fun changeDevice() {
        startActivity(Intent(this, ConnectActivity::class.java).putExtra(ConnectActivity.EXTRA_NO_AUTO, true))
        finish()
    }

    /** То, что страница видит как window.GTApp. Вызывается из потока WebView, поэтому действия переносим в главный поток. */
    inner class Bridge {
        @JavascriptInterface
        fun info(): String = "{\"app\":\"android-shell\",\"version\":\"" + version() + "\",\"host\":\"" + host + "\"}"

        @JavascriptInterface
        fun changeDevice() {
            runOnUiThread { this@WebActivity.changeDevice() }
        }

        // Живая камера для калибровки (web/app.js, вкладка «Калибровка»): кадры серые, уменьшенные; разбор делает страница
        @JavascriptInterface
        fun cameraStart() = live.start()

        @JavascriptInterface
        fun cameraStop() = live.stop()

        /** JSON: state (off/asking/starting/ready/error), message, w, h, rec, frames, ms (длина записи). */
        @JavascriptInterface
        fun cameraStatus(): String = live.statusJson()

        /** Последний кадр: base64 яркости w*h байт (пусто - кадров нет). */
        @JavascriptInterface
        fun cameraLatest(): String = live.latestBase64()

        @JavascriptInterface
        fun cameraRecord(on: Boolean) = live.record(on)

        /** Кадр записи, ближайший к моменту t (секунды от начала записи); формат как у cameraLatest. */
        @JavascriptInterface
        fun cameraFrameAt(t: Double): String = live.frameAtBase64(t)

        @JavascriptInterface
        fun cameraClear() = live.clear()
    }

    private inner class Client : WebViewClient() {
        override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
            val url = request.url.toString()
            if (Net.sameHost(url, host)) return false
            try {
                startActivity(Intent(Intent.ACTION_VIEW, request.url))
            } catch (_: ActivityNotFoundException) {
            }
            return true
        }

        override fun onReceivedError(view: WebView, request: WebResourceRequest, error: WebResourceError) {
            if (!request.isForMainFrame) return
            failed = true
            errorText.text = "Адрес: $host\n${error.description}"
            errorBox.visibility = View.VISIBLE
        }

        override fun onPageFinished(view: WebView, url: String) {
            if (!failed) errorBox.visibility = View.GONE
        }
    }

    private inner class Chrome : WebChromeClient() {
        // Страница использует confirm() (удаление сцены, прошивка и др.): показываем свои окна
        override fun onJsConfirm(view: WebView, url: String, message: String, result: JsResult): Boolean {
            AlertDialog.Builder(this@WebActivity)
                .setMessage(message)
                .setPositiveButton("OK") { _, _ -> result.confirm() }
                .setNegativeButton("Отмена") { _, _ -> result.cancel() }
                .setOnCancelListener { result.cancel() }
                .show()
            return true
        }

        override fun onJsAlert(view: WebView, url: String, message: String, result: JsResult): Boolean {
            AlertDialog.Builder(this@WebActivity)
                .setMessage(message)
                .setPositiveButton("OK") { _, _ -> result.confirm() }
                .setOnCancelListener { result.confirm() }
                .show()
            return true
        }

        // Выбор файла: видео для калибровки, файл схемы «соты», прошивка
        override fun onShowFileChooser(view: WebView, callback: ValueCallback<Array<Uri>>, params: FileChooserParams): Boolean {
            fileCallback?.onReceiveValue(null)
            fileCallback = callback
            return try {
                @Suppress("DEPRECATION")
                startActivityForResult(params.createIntent(), REQ_FILE)
                true
            } catch (_: ActivityNotFoundException) {
                fileCallback = null
                callback.onReceiveValue(null)
                false
            }
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        if (requestCode == REQ_FILE) {
            fileCallback?.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(resultCode, data))
            fileCallback = null
        } else {
            @Suppress("DEPRECATION")
            super.onActivityResult(requestCode, resultCode, data)
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        @Suppress("DEPRECATION")
        if (::web.isInitialized && web.canGoBack()) web.goBack() else super.onBackPressed()
    }

    override fun onSaveInstanceState(outState: Bundle) {
        super.onSaveInstanceState(outState)
        if (::web.isInitialized) web.saveState(outState)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        if (requestCode == LiveCamera.REQ_CAMERA) {
            live.onPermissionResult(grantResults.isNotEmpty() && grantResults[0] == android.content.pm.PackageManager.PERMISSION_GRANTED)
        } else {
            super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        }
    }

    override fun onPause() {
        live.pause()
        if (::web.isInitialized) web.onPause()   // в фоне страница не опрашивает гирлянду
        super.onPause()
    }

    override fun onResume() {
        super.onResume()
        if (::web.isInitialized) web.onResume()
    }

    override fun onDestroy() {
        live.stop()
        if (::web.isInitialized) web.destroy()
        super.onDestroy()
    }

    companion object {
        const val EXTRA_HOST = "host"
        private const val REQ_FILE = 1
    }
}
