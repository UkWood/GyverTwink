#!/usr/bin/env python3
"""Android-вариант скетча приложения: processing/GyverTwink (ПК и Android) -> processing/GyverTwink_android (только Android).

  python3 tools/make_android_sketch.py          пересоздать processing/GyverTwink_android
  python3 tools/make_android_sketch.py --check  проверить, что папка соответствует исходнику (для CI)

Зачем. Исходник приложения один и работает в двух режимах (флаг androidMode). Но для сборки APK в Processing (режим Android)
в нём приходилось каждый раз править файлы вручную: библиотека Video существует только для ПК и валит Android-сборку
(NullPointerException в Library.getApplicationExports), поэтому `import processing.video.*`, класс `Capture` и ветку камеры ПК надо убрать,
флаг переключить, режим скетча сменить на Android. Скрипт делает это один раз и всегда одинаково.

Папка processing/GyverTwink_android создаётся скриптом: руками её не правят, правят processing/GyverTwink и запускают скрипт.
Главная вкладка скетча называется как папка (требование Processing), остальные файлы копируются как есть.
"""
import hashlib
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "processing", "GyverTwink")
DST = os.path.join(ROOT, "processing", "GyverTwink_android")
MAIN_SRC = "GyverTwink.pde"
MAIN_DST = "GyverTwink_android.pde"

# camera.pde заменяется целиком (в нём остаётся только ветка Android). Если исходный camera.pde изменился,
# шаблон ниже надо пересмотреть вручную: тогда скрипт остановится, а не создаст молча устаревший вариант.
CAMERA_SRC_SHA256 = "6271a79a9e55aee687a801f9078aa4aa9add49c9ff33b799af2d77def53cc9e8"

CAMERA_ANDROID = """// Камера в Android-варианте (processing/GyverTwink_android): только KetaiCamera.
// Файл создан скриптом tools/make_android_sketch.py, правь processing/GyverTwink/camera.pde и запускай скрипт.
void initCam() {
  Acam = new KetaiCamera(this, 1280, 720, 30);
  frame = createImage(Acam.height, Acam.width, RGB);

  size = frame.width / X;
  Y = frame.height / size;
  brMap = new int[X * Y][3];
  ring = frame.copy();
}

void startCam() {
  if (Acam == null) initCam();
  if (!camStart) Acam.start();
  camStart = true;
}

void stopCam() {
  if (camStart) Acam.stop();
  camStart = false;
}

void readCam() {
  PImage buf = Acam;

  int am = frame.height * frame.width;
  for (int i = 0; i < am; i++) {
    frame.pixels[(i % frame.height) * frame.width + (am - i) / frame.height] = buf.pixels[i];
  }
  frame.updatePixels();
}

void onCameraPreviewEvent() {
  Acam.read();
  camReady = true;
}
"""

BANNER = """// ============================================================
// ЭТОТ СКЕТЧ СОЗДАН СКРИПТОМ tools/make_android_sketch.py из processing/GyverTwink - не правь его вручную.
// Он собирается в Processing в режиме Android (библиотеки: Ketai и UDP, Video не нужна).
// Менять приложение - в processing/GyverTwink, потом: python3 tools/make_android_sketch.py
// ============================================================
"""

ANDROID_PROPERTIES = "mode=Android\nmode.id=processing.mode.android.AndroidMode\n"


def read_text(name):
    with open(os.path.join(SRC, name), encoding="utf-8", newline="") as f:
        return f.read()


def replace_once(text, old, new, what):
    """Подстановка с проверкой: если исходник изменился и якорь пропал, скрипт останавливается."""
    if text.count(old) != 1:
        sys.exit("make_android_sketch: не найден (или не один) фрагмент для замены: %s\n  %r" % (what, old))
    return text.replace(old, new)


def convert_main(t):
    t = replace_once(t, "private static final boolean androidMode = false;", "private static final boolean androidMode = true;", "режим Android")
    t = replace_once(
        t,
        "void openKeyboard() {}\nvoid closeKeyboard() {}\n",
        "// (в Android-варианте эти две функции уже есть в Processing, поэтому закомментированы)\n//void openKeyboard() {}\n//void closeKeyboard() {}\n",
        "заглушки клавиатуры",
    )
    t = replace_once(t, "// - Video\n", "", "строка про библиотеку Video в шапке")
    t = replace_once(t, "import processing.video.*;\n", "", "import processing.video")
    t = replace_once(t, "Capture Wcam;\n", "", "переменная Wcam")
    return BANNER + t


def convert_ui(t):
    return replace_once(
        t,
        "    // Камера не стартовала в PC режиме\n    if (!androidMode && Wcam == null) return;\n\n",
        "",
        "проверка камеры ПК в UI.pde",
    )


def build():
    """Возвращает {относительный путь: bytes} всего Android-варианта."""
    out = {}
    cam = read_text("camera.pde")
    if hashlib.sha256(cam.encode("utf-8")).hexdigest() != CAMERA_SRC_SHA256:
        sys.exit("make_android_sketch: processing/GyverTwink/camera.pde изменился. Пересмотри CAMERA_ANDROID в этом скрипте и обнови CAMERA_SRC_SHA256.")
    for root, dirs, files in os.walk(SRC):
        dirs.sort()
        for name in sorted(files):
            full = os.path.join(root, name)
            rel = os.path.relpath(full, SRC).replace(os.sep, "/")
            if rel == "code/sketch.properties":
                continue   # режим скетча задаётся ниже
            if rel.endswith(".pde"):
                text = read_text(rel)
                if rel == MAIN_SRC:
                    text = convert_main(text)
                    rel = MAIN_DST
                elif rel == "UI.pde":
                    text = convert_ui(text)
                elif rel == "camera.pde":
                    text = CAMERA_ANDROID
                out[rel] = text.encode("utf-8")
            else:
                with open(full, "rb") as f:   # манифест, значки, data/, subnet.txt - как есть
                    out[rel] = f.read()
    out["code/sketch.properties"] = ANDROID_PROPERTIES.encode("utf-8")
    # послеусловия: ничего от камеры ПК остаться не должно, режим Android включён
    for rel, data in out.items():
        if rel.endswith(".pde"):
            text = data.decode("utf-8")
            for bad in ("Wcam", "Capture", "processing.video", "captureEvent"):
                if bad in text:
                    sys.exit("make_android_sketch: в %s остался %s" % (rel, bad))
    if "androidMode = true;" not in out[MAIN_DST].decode("utf-8"):
        sys.exit("make_android_sketch: режим Android не включён")
    return out


def existing():
    out = {}
    if not os.path.isdir(DST):
        return out
    for root, _dirs, files in os.walk(DST):
        for name in files:
            full = os.path.join(root, name)
            with open(full, "rb") as f:
                out[os.path.relpath(full, DST).replace(os.sep, "/")] = f.read()
    return out


def main():
    want = build()
    if "--check" in sys.argv:
        have = existing()
        bad = sorted(set(want) ^ set(have)) + sorted(k for k in want if k in have and want[k] != have[k])
        if bad:
            print("processing/GyverTwink_android не соответствует исходнику (запусти python3 tools/make_android_sketch.py):")
            for k in bad:
                print("  ", k, "(лишний файл)" if k not in want else "(нет файла)" if k not in have else "(отличается)")
            sys.exit(1)
        print("processing/GyverTwink_android актуален (%d файлов)" % len(want))
        return
    if os.path.isdir(DST):
        shutil.rmtree(DST)
    for rel, data in want.items():
        path = os.path.join(DST, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(data)
    print("processing/GyverTwink_android: %d файлов" % len(want))


if __name__ == "__main__":
    main()
