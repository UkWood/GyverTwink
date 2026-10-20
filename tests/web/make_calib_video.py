#!/usr/bin/env python3
"""Синтетическое видео для e2e-теста калибровки по видео: «съёмка» серии vcalib.ino.

  make_calib_video.py spec.json out.webm

spec.json: {"w","h","fps","t0","seed","params":{"n","step","flash","gap","dark","marker","delay"},
            "points": [[x, y], ...]   положения светодиодов в пикселях кадра,
            "hidden": [номера],       светодиоды вне кадра (не видны),
            "no_leds": false}         true - в кадре вообще ничего не светится (проверка ошибки)
Кадр: тёмная комната с шумом, статичная лампа, медленно движущееся тусклое пятно, дрейф экспозиции.
"""
import json
import subprocess
import sys

import numpy as np

spec = json.load(open(sys.argv[1]))
out = sys.argv[2]
W, H, FPS = spec["w"], spec["h"], spec.get("fps", 30)
P = spec["params"]
pts = np.array(spec["points"], dtype=float)
hidden = set(spec.get("hidden", []))
no_leds = spec.get("no_leds", False)
rng = np.random.default_rng(spec.get("seed", 1))
n, step = P["n"], P["step"] / 1000.0
t0 = spec.get("t0", 1.0)
flash_start = t0 + P["delay"] + P["dark"] / 1000.0
seq_start = flash_start + (P["flash"] + P["gap"]) / 1000.0
seq_end = seq_start + n * step
duration = seq_end + 1.5 + 1.0
m = min(n, P["marker"])
marker_idx = [k * n // m for k in range(m)]

sigma = 2.2 * W / 320.0
R = int(sigma * 3) + 1
yy, xx = np.mgrid[-R:R + 1, -R:R + 1]


def blob(img, cx, cy, amp, sg):
    r = int(sg * 3) + 1
    y0, y1 = max(0, int(cy) - r), min(H, int(cy) + r + 1)
    x0, x1 = max(0, int(cx) - r), min(W, int(cx) + r + 1)
    if y0 >= y1 or x0 >= x1:
        return
    ys, xs = np.mgrid[y0:y1, x0:x1]
    img[y0:y1, x0:x1] += amp * np.exp(-((xs - cx) ** 2 + (ys - cy) ** 2) / (2 * sg * sg))


ff = subprocess.Popen(
    ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "gray", "-s", f"{W}x{H}", "-r", str(FPS), "-i", "-",
     "-c:v", "libvpx", "-g", "3", "-b:v", "2M", "-pix_fmt", "yuv420p", out],
    stdin=subprocess.PIPE,
)
frames = int(duration * FPS)
for f in range(frames):
    t = f / FPS
    img = np.full((H, W), 22.0)
    blob(img, W * 0.8, H * 0.15, 90, 6 * W / 320.0)                                  # лампа
    blob(img, W * 0.2 + 3.5 * t, H * 0.75 - 1.0 * t, 35, 9 * W / 320.0)             # тусклое движущееся пятно
    lit = []
    if not no_leds:
        if flash_start <= t < flash_start + P["flash"] / 1000.0:
            lit += marker_idx
        if seq_start <= t < seq_end:
            lit.append(int((t - seq_start) / step))
    for i in lit:
        if i not in hidden:
            blob(img, pts[i][0], pts[i][1], 150, sigma)
    img = img * (1 + 0.08 * np.sin(t / 4)) + rng.normal(0, 2.5, img.shape)
    ff.stdin.write(np.clip(img, 0, 255).astype(np.uint8).tobytes())
ff.stdin.close()
if ff.wait() != 0:
    sys.exit("ffmpeg завершился с ошибкой")
