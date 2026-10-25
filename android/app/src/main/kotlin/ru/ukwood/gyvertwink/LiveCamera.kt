package ru.ukwood.gyvertwink

import android.Manifest
import android.app.Activity
import android.content.pm.ActivityInfo
import android.content.pm.PackageManager
import android.graphics.ImageFormat
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import android.os.SystemClock
import android.util.Base64
import android.view.Surface
import android.view.WindowManager
import org.json.JSONObject
import ru.ukwood.gyvertwink.core.CameraPick
import ru.ukwood.gyvertwink.core.FrameStore
import ru.ukwood.gyvertwink.core.Gray
import ru.ukwood.gyvertwink.core.Resample

/**
 * Живая камера для калибровки. Снимает только яркость (плоскость Y), уменьшает кадры и отдаёт их странице как есть:
 * весь разбор (поиск вспышек, координаты) делает та же страница (web/calib.js), что и для готового видео.
 * Методы вызываются из потока моста WebView, поэтому общие поля защищены lock.
 */
class LiveCamera(private val activity: Activity) {
    @Volatile private var state = "off"          // off | asking | starting | ready | error
    @Volatile private var message = ""

    private val lock = Any()
    private var thread: HandlerThread? = null
    private var handler: Handler? = null
    private var device: CameraDevice? = null
    private var session: CameraCaptureSession? = null
    private var reader: ImageReader? = null
    private var request: CaptureRequest.Builder? = null
    private var rotation = 0
    private var latest: Gray? = null
    private var buf = ByteArray(0)

    private val store = FrameStore(MAX_BYTES)
    private var recording = false
    private var recStart = 0L
    private var lastStored = -1000L
    private var frameW = 0
    private var frameH = 0
    private var savedOrientation = ActivityInfo.SCREEN_ORIENTATION_UNSPECIFIED

    fun start() {
        if (state == "asking" || state == "starting" || state == "ready") return
        message = ""
        if (!activity.packageManager.hasSystemFeature(PackageManager.FEATURE_CAMERA_ANY)) {
            fail("На устройстве нет камеры")
            return
        }
        if (activity.checkSelfPermission(Manifest.permission.CAMERA) != PackageManager.PERMISSION_GRANTED) {
            state = "asking"
            activity.runOnUiThread { activity.requestPermissions(arrayOf(Manifest.permission.CAMERA), REQ_CAMERA) }
            return
        }
        open()
    }

    fun onPermissionResult(granted: Boolean) {
        if (state != "asking") return
        if (granted) open() else fail("Нет доступа к камере. Разреши его в настройках приложения (Приложения → GyverTwink → Разрешения).")
    }

    @Suppress("DEPRECATION")
    private fun open() {
        state = "starting"
        val latch = java.util.concurrent.CountDownLatch(1)
        // поворот экрана и «экран не гаснет» меняем из главного потока; на время съёмки фиксируем ориентацию, иначе кадры повернутся посреди записи
        activity.runOnUiThread {
            val rot = (activity.getSystemService(Activity.WINDOW_SERVICE) as WindowManager).defaultDisplay.rotation
            displayDeg = when (rot) { Surface.ROTATION_90 -> 90; Surface.ROTATION_180 -> 180; Surface.ROTATION_270 -> 270; else -> 0 }
            savedOrientation = activity.requestedOrientation
            activity.requestedOrientation = ActivityInfo.SCREEN_ORIENTATION_LOCKED
            activity.window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            latch.countDown()
        }
        latch.await()
        val t = HandlerThread("gt-camera").also { it.start() }
        thread = t
        val h = Handler(t.looper)
        handler = h
        h.post { openOnThread(h) }
    }

    @Volatile private var displayDeg = 0

    @Suppress("MissingPermission")
    private fun openOnThread(h: Handler) {
        try {
            val mgr = activity.getSystemService(Activity.CAMERA_SERVICE) as CameraManager
            val id = mgr.cameraIdList.firstOrNull { mgr.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK }
                ?: mgr.cameraIdList.firstOrNull()
            if (id == null) {
                fail("Камера не найдена")
                return
            }
            val ch = mgr.getCameraCharacteristics(id)
            val map = ch.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val sizes = map?.getOutputSizes(ImageFormat.YUV_420_888)?.map { it.width to it.height } ?: emptyList()
            val size = CameraPick.pickSize(sizes, 480)
            if (size == null) {
                fail("Камера не отдаёт кадры нужного формата")
                return
            }
            val sensor = ch.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 90
            val front = ch.get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_FRONT
            rotation = if (front) CameraPick.rotation(sensor, -displayDeg) else CameraPick.rotation(sensor, displayDeg)
            val ranges = ch.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
            val fps = ranges?.filter { it.upper <= 30 }?.maxByOrNull { it.upper * 100 + it.lower }

            val rd = ImageReader.newInstance(size.first, size.second, ImageFormat.YUV_420_888, 3)
            reader = rd
            rd.setOnImageAvailableListener({ r -> onImage(r) }, h)
            mgr.openCamera(id, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    device = camera
                    try {
                        val b = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                        b.addTarget(rd.surface)
                        if (fps != null) b.set(CaptureRequest.CONTROL_AE_TARGET_FPS_RANGE, fps)
                        request = b
                        @Suppress("DEPRECATION")
                        camera.createCaptureSession(listOf(rd.surface), object : CameraCaptureSession.StateCallback() {
                            override fun onConfigured(s: CameraCaptureSession) {
                                session = s
                                try {
                                    s.setRepeatingRequest(b.build(), null, h)
                                    state = "ready"
                                    h.postDelayed({ lockExposure() }, 1000)
                                } catch (e: Exception) {
                                    fail("Не удалось запустить камеру: " + e.message)
                                }
                            }

                            override fun onConfigureFailed(s: CameraCaptureSession) {
                                fail("Камера не настроилась")
                            }
                        }, h)
                    } catch (e: Exception) {
                        fail("Не удалось запустить камеру: " + e.message)
                    }
                }

                override fun onDisconnected(camera: CameraDevice) {
                    fail("Камера отключена (её мог занять другой процесс)")
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    fail("Ошибка камеры $error")
                }
            }, h)
        } catch (e: CameraAccessException) {
            fail("Нет доступа к камере: " + e.message)
        } catch (e: SecurityException) {
            fail("Нет доступа к камере")
        } catch (e: Exception) {
            fail("Камера: " + e.message)
        }
    }

    // после того как автоэкспозиция успокоилась, фиксируем её и баланс белого: вспышки светодиодов не должны менять яркость всего кадра
    private fun lockExposure() {
        val b = request ?: return
        val s = session ?: return
        try {
            b.set(CaptureRequest.CONTROL_AE_LOCK, true)
            b.set(CaptureRequest.CONTROL_AWB_LOCK, true)
            s.setRepeatingRequest(b.build(), null, handler)
        } catch (_: Exception) {
        }
    }

    private fun onImage(r: ImageReader) {
        val img = try { r.acquireLatestImage() } catch (_: Exception) { null } ?: return
        try {
            val plane = img.planes[0]
            val bb = plane.buffer
            val n = bb.remaining()
            if (buf.size < n) buf = ByteArray(n)
            bb.get(buf, 0, n)
            val g = Resample.fromYPlane(buf, img.width, img.height, plane.rowStride, plane.pixelStride, rotation, OUT_WIDTH)
            synchronized(lock) {
                latest = g
                frameW = g.w
                frameH = g.h
                if (recording) {
                    val t = SystemClock.elapsedRealtime() - recStart
                    if (t - lastStored >= STORE_INTERVAL_MS) {
                        if (store.add(t, g)) {
                            lastStored = t
                        } else {
                            recording = false
                            message = "Не хватило памяти телефона для записи: серия слишком длинная"
                        }
                    }
                }
            }
        } finally {
            img.close()
        }
    }

    private fun fail(msg: String) {
        message = msg
        state = "error"
        release()
    }

    fun stop() {
        synchronized(lock) { recording = false }
        release()
        state = "off"
        message = ""
    }

    /** Экран ушёл в фон: камеру отпускаем (запись прервана). Пока ждём разрешение, ничего не трогаем. */
    fun pause() {
        if (state == "starting" || state == "ready") {
            val rec = synchronized(lock) { recording }
            stop()
            if (rec) message = "Запись прервана: приложение ушло в фон"
        }
    }

    private fun release() {
        try { session?.close() } catch (_: Exception) {}
        try { device?.close() } catch (_: Exception) {}
        try { reader?.close() } catch (_: Exception) {}
        session = null
        device = null
        reader = null
        request = null
        thread?.quitSafely()
        thread = null
        handler = null
        activity.runOnUiThread {
            activity.window.clearFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
            activity.requestedOrientation = savedOrientation
        }
    }

    fun record(on: Boolean) {
        synchronized(lock) {
            if (on) {
                store.clear()
                recStart = SystemClock.elapsedRealtime()
                lastStored = -1000
                recording = state == "ready"
            } else {
                recording = false
            }
        }
    }

    fun clear() {
        synchronized(lock) {
            recording = false
            store.clear()
        }
    }

    fun statusJson(): String {
        synchronized(lock) {
            return JSONObject().apply {
                put("state", state)
                put("message", message)
                put("w", frameW)
                put("h", frameH)
                put("rec", recording)
                put("frames", store.size)
                put("ms", store.lastMs)
            }.toString()
        }
    }

    fun latestBase64(): String = encode(synchronized(lock) { latest })

    fun frameAtBase64(tSec: Double): String = encode(synchronized(lock) { store.nearest(Math.round(tSec * 1000.0)) })

    private fun encode(g: Gray?): String = if (g == null) "" else Base64.encodeToString(g.data, Base64.NO_WRAP)

    companion object {
        const val REQ_CAMERA = 2
        private const val OUT_WIDTH = 240          // ширина кадров для страницы (для разбора хватает: карта 60 клеток в ширину)
        private const val STORE_INTERVAL_MS = 45L  // не чаще ~22 кадров в секунду в записи
        private const val MAX_BYTES = 160L * 1024 * 1024
    }
}
