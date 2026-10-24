// Камера в Android-варианте (processing/GyverTwink_android): только KetaiCamera.
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
