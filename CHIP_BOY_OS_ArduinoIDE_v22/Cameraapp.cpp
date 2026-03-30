#include "CameraApp.h"
#include "RadioApp.h"
#include <esp_camera.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"
#include <Adafruit_SSD1306.h>

// ============================================================================
// CAMERA APP IMPLEMENTATION
//
// Capture flow (BTN2):
//   1. Re-init camera in QQVGA GRAYSCALE.
//   2. Flush 5 frames so exposure settles on the actual scene.
//   3. Grab one frame → scale to 128×64, threshold → render on OLED.
//   4. Save that exact 128×64 raw buffer as /images/imageN.bmp
//      (8192 bytes, row-major, 1 byte per pixel: 0=black, 1=white).
//   5. Re-init JPEG, flush a couple of frames, save full-res imageN.jpg.
//
// The OLED preview == the saved sidecar pixel-for-pixel because both come
// from the same grayscale frame buffer captured in step 3.
//
// Image browser (BTN1 from viewfinder):
//   - Lists /images/image*.jpg sorted numerically.
//   - Selecting one loads the matching .bmp sidecar → renders on OLED.
//   - If no sidecar, shows "No saved preview".
//   - BTN3 in list → back to viewfinder.
//   - Any button in viewer → back to list.
//
// BTN1 WB cycling removed — always AUTO.
// ============================================================================

extern bool inMenu;
extern bool uiDirty;
extern uint8_t lastSecondDrawn;
extern unsigned long menuEnableButtonsAt;
extern bool screenEnabled;
extern bool displayReady;
extern Adafruit_SSD1306 display;

#define BUZZER_PIN D2
#define ENC_A_PIN  D8
#define ENC_B_PIN  D7
extern uint8_t encPrevAB;
extern int8_t  encAccum;

struct BtnState;
extern BtnState b1, b2, b3, bUp, bDown;
bool btnWasShortPressed(const BtnState &b_in);
void redrawMenuNow();
void radioRecoverAfterCamera();

#ifndef SCREEN_WIDTH
#define SCREEN_WIDTH  128
#endif
#ifndef SCREEN_HEIGHT
#define SCREEN_HEIGHT 64
#endif

#define CAM_PREV_W     128
#define CAM_PREV_H      64
#define CAM_PREV_BYTES (CAM_PREV_W * CAM_PREV_H)   // 8192

// ── Module globals ────────────────────────────────────────────────────────

bool inCamera   = false;
bool camera_ok  = false;
bool sd_ok      = false;
int  imageCount = 1;
camera_config_t camcfg;

static bool cameraPausedRadio = false;

enum CamSubState { CAM_VIEWFINDER, CAM_IMG_LIST, CAM_IMG_VIEW };
static CamSubState camSubState = CAM_VIEWFINDER;

static const int CAM_MAX_IMAGES   = 64;
static const int CAM_LIST_VISIBLE = 6;
static int camImageNums[CAM_MAX_IMAGES];
static int camImageTotal = 0;
static int camListCursor = 0;
static int camListFirst  = 0;

// ── Camera config ─────────────────────────────────────────────────────────

static void camApplyPins() {
  camcfg.ledc_channel = LEDC_CHANNEL_0; camcfg.ledc_timer = LEDC_TIMER_0;
  camcfg.pin_d0 = Y2_GPIO_NUM; camcfg.pin_d1 = Y3_GPIO_NUM;
  camcfg.pin_d2 = Y4_GPIO_NUM; camcfg.pin_d3 = Y5_GPIO_NUM;
  camcfg.pin_d4 = Y6_GPIO_NUM; camcfg.pin_d5 = Y7_GPIO_NUM;
  camcfg.pin_d6 = Y8_GPIO_NUM; camcfg.pin_d7 = Y9_GPIO_NUM;
  camcfg.pin_xclk = XCLK_GPIO_NUM; camcfg.pin_pclk  = PCLK_GPIO_NUM;
  camcfg.pin_vsync = VSYNC_GPIO_NUM; camcfg.pin_href = HREF_GPIO_NUM;
  camcfg.pin_sscb_sda = SIOD_GPIO_NUM; camcfg.pin_sscb_scl = SIOC_GPIO_NUM;
  camcfg.pin_pwdn = PWDN_GPIO_NUM; camcfg.pin_reset = RESET_GPIO_NUM;
  camcfg.xclk_freq_hz = 20000000;
}

static void camApplyAutoSettings() {
  if (!camera_ok) return;
  sensor_t *s = esp_camera_sensor_get();
  if (!s) return;
  s->set_whitebal(s,1); s->set_awb_gain(s,1); s->set_wb_mode(s,0);
  s->set_aec2(s,0); s->set_aec_value(s,300); s->set_agc_gain(s,8);
  s->set_gain_ctrl(s,0); s->set_exposure_ctrl(s,1);
  s->set_brightness(s,0); s->set_saturation(s,2);
  s->set_contrast(s,1); s->set_sharpness(s,0); s->set_denoise(s,0);
  s->set_lenc(s,1); s->set_hmirror(s,0); s->set_vflip(s,0);
}

static bool camInitGrayscale() {
  camApplyPins();
  camcfg.pixel_format = PIXFORMAT_GRAYSCALE;
  camcfg.frame_size   = FRAMESIZE_QQVGA;
  camcfg.jpeg_quality = 10;
  camcfg.fb_count     = 1;
  return (esp_camera_init(&camcfg) == ESP_OK);
}

static bool camInitJpeg() {
  camApplyPins();
  camcfg.pixel_format = PIXFORMAT_JPEG;
  camcfg.frame_size   = FRAMESIZE_SXGA;
  camcfg.jpeg_quality = 8;
  camcfg.fb_count     = 1;
  camera_ok = (esp_camera_init(&camcfg) == ESP_OK);
  if (camera_ok) camApplyAutoSettings();
  return camera_ok;
}

// ── Deinit ────────────────────────────────────────────────────────────────

void deinitCameraModule() {
  esp_camera_deinit();
  camera_ok = false;
  if (sd_ok) { SD.end(); sd_ok = false; }
  SPI.end();
  pinMode(21, INPUT_PULLUP);
  pinMode(ENC_A_PIN, INPUT_PULLUP);
  pinMode(ENC_B_PIN, INPUT_PULLUP);
  encAccum  = 0;
  encPrevAB = (((digitalRead(ENC_A_PIN) == HIGH) ? 1 : 0) << 1) |
               ((digitalRead(ENC_B_PIN) == HIGH) ? 1 : 0);
}

// ── SD helpers ────────────────────────────────────────────────────────────

int findNextImageNumber_cam() {
  File root = SD.open("/images");
  if (!root || !root.isDirectory()) return 1;
  int highest = 0;
  File f = root.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      const char *nm = f.name();
      if (strncmp(nm, "image", 5) == 0) {
        int n = atoi(nm + 5);
        if (n > highest) highest = n;
      }
    }
    f = root.openNextFile();
  }
  return highest + 1;
}

static void camScanImages() {
  camImageTotal = 0; camListCursor = 0; camListFirst = 0;
  if (!sd_ok) return;
  File root = SD.open("/images");
  if (!root || !root.isDirectory()) return;
  File f = root.openNextFile();
  while (f && camImageTotal < CAM_MAX_IMAGES) {
    if (!f.isDirectory()) {
      const char *nm = f.name();
      int len = (int)strlen(nm);
      if (strncmp(nm, "image", 5) == 0 && len > 4 &&
          strcmp(nm + len - 4, ".jpg") == 0) {
        int n = atoi(nm + 5);
        if (n > 0) camImageNums[camImageTotal++] = n;
      }
    }
    f = root.openNextFile();
  }
  // Sort ascending
  for (int i = 0; i < camImageTotal - 1; i++)
    for (int j = i + 1; j < camImageTotal; j++)
      if (camImageNums[j] < camImageNums[i]) {
        int t = camImageNums[i]; camImageNums[i] = camImageNums[j]; camImageNums[j] = t;
      }
}

// ── Draw helpers ──────────────────────────────────────────────────────────

void cameraDrawIdle() {
  display.clearDisplay();
  display.drawLine(64,29,64,35,WHITE); display.drawLine(61,32,67,32,WHITE);
  int len=10;
  display.drawLine(0,0,len,0,WHITE);   display.drawLine(0,0,0,len,WHITE);
  display.drawLine(127,0,127-len,0,WHITE); display.drawLine(127,0,127,len,WHITE);
  display.drawLine(0,63,0,63-len,WHITE);   display.drawLine(0,63,len,63,WHITE);
  display.drawLine(127,63,127-len,63,WHITE); display.drawLine(127,63,127,63-len,WHITE);
  display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(0,55);   display.print("IMAGES");
  display.setCursor(104,55); display.print("EXIT");
  display.display();
}

static void cameraDrawFlash() {
  display.clearDisplay();
  display.drawLine(64,29,64,35,WHITE); display.drawLine(61,32,67,32,WHITE);
  display.drawCircle(64,32,14,WHITE);
  int len=10;
  display.drawLine(0,0,len,0,WHITE);   display.drawLine(0,0,0,len,WHITE);
  display.drawLine(127,0,127-len,0,WHITE); display.drawLine(127,0,127,len,WHITE);
  display.drawLine(0,63,0,63-len,WHITE);   display.drawLine(0,63,len,63,WHITE);
  display.drawLine(127,63,127-len,63,WHITE); display.drawLine(127,63,127,63-len,WHITE);
  display.display();
}

static void camDrawImageList() {
  display.clearDisplay();
  display.setTextSize(1); display.setTextColor(WHITE);
  if (camImageTotal == 0) {
    display.setCursor(4,24); display.print("No images found");
    display.setCursor(104,55); display.print("EXIT");
    display.display(); return;
  }
  int last = min(camImageTotal-1, camListFirst+CAM_LIST_VISIBLE-1);
  for (int i = camListFirst; i <= last; i++) {
    int y = (i - camListFirst) * 9;
    display.setCursor(0,y);
    display.print((camListCursor==i) ? ">" : " ");
    char label[20];
    snprintf(label, sizeof(label), "image%d", camImageNums[i]);
    display.print(label);
  }
  display.setCursor(104,55); display.print("EXIT");
  display.display();
}

// Load and render a .bmp sidecar. Returns false if missing/unreadable.
static bool camDrawSavedPreview(int imgNum) {
  char path[40];
  snprintf(path, sizeof(path), "/images/image%d.bmp", imgNum);
  File f = SD.open(path, FILE_READ);
  if (!f || (int)f.size() < CAM_PREV_BYTES) {
    if (f) f.close();
    return false;
  }
  display.clearDisplay();
  for (int py = 0; py < CAM_PREV_H; py++)
    for (int px = 0; px < CAM_PREV_W; px++)
      if (f.read()) display.drawPixel(px, py, WHITE);
  f.close();
  display.display();
  return true;
}

// ── Capture pipeline ──────────────────────────────────────────────────────

// Capture a grayscale QQVGA frame, scale to 128×64, threshold.
// Returns heap-allocated buffer (caller must free), or nullptr on failure.
static uint8_t *camCapturePreview() {
  esp_camera_deinit();
  if (!camInitGrayscale()) return nullptr;

  sensor_t *s = esp_camera_sensor_get();
  if (s) s->set_contrast(s, 2);

  // Flush frames — this is what ensures the exposure matches the scene
  for (int i = 0; i < 5; i++) {
    camera_fb_t *fl = esp_camera_fb_get();
    if (fl) esp_camera_fb_return(fl);
    delay(30);
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb || fb->format != PIXFORMAT_GRAYSCALE) {
    if (fb) esp_camera_fb_return(fb);
    return nullptr;
  }

  const int SW = fb->width, SH = fb->height;
  uint8_t *buf = (uint8_t *)malloc(CAM_PREV_BYTES);
  if (!buf) { esp_camera_fb_return(fb); return nullptr; }

  for (int dy = 0; dy < CAM_PREV_H; dy++) {
    int sy = (dy * SH) / CAM_PREV_H; if (sy >= SH) sy = SH-1;
    for (int dx = 0; dx < CAM_PREV_W; dx++) {
      int sx = (dx * SW) / CAM_PREV_W; if (sx >= SW) sx = SW-1;
      buf[dy * CAM_PREV_W + dx] = (fb->buf[sy * SW + sx] > 128) ? 1 : 0;
    }
  }

  esp_camera_fb_return(fb);
  return buf;
}

static void camRenderBuf(const uint8_t *buf) {
  display.clearDisplay();
  for (int py = 0; py < CAM_PREV_H; py++)
    for (int px = 0; px < CAM_PREV_W; px++)
      if (buf[py * CAM_PREV_W + px]) display.drawPixel(px, py, WHITE);
}

static bool camSaveSidecar(const uint8_t *buf, int num) {
  char fn[40]; snprintf(fn, sizeof(fn), "/images/image%d.bmp", num);
  File f = SD.open(fn, FILE_WRITE);
  if (!f) return false;
  f.write(buf, CAM_PREV_BYTES);
  f.close();
  return true;
}

static bool camSaveJpeg(int num) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  char fn[40]; snprintf(fn, sizeof(fn), "/images/image%d.jpg", num);
  File f = SD.open(fn, FILE_WRITE);
  if (!f) { esp_camera_fb_return(fb); return false; }
  f.write(fb->buf, fb->len);
  f.close();
  esp_camera_fb_return(fb);
  return true;
}

// ── Init ──────────────────────────────────────────────────────────────────

void initCameraModule() {
  camInitJpeg();
  sd_ok = SD.begin(21) && (SD.cardType() != CARD_NONE);
  if (sd_ok) imageCount = findNextImageNumber_cam();
}

// ── App lifecycle ─────────────────────────────────────────────────────────

void startCameraApp() {
  cameraPausedRadio = false;
  camSubState = CAM_VIEWFINDER;
  if (radioActive && !radioPaused && radioReady) {
    dfp.pause(); radioPaused = true; cameraPausedRadio = true;
    radioPersistFromGlobals();
  }
  deinitCameraModule();
  initCameraModule();
  cameraDrawIdle();
}

void updateCameraApp() {

  // ── IMAGE VIEWER ─────────────────────────────────────────────────────────
  if (camSubState == CAM_IMG_VIEW) {
    if (btnWasShortPressed(b1) || btnWasShortPressed(b2) || btnWasShortPressed(b3)) {
      camSubState = CAM_IMG_LIST;
      camDrawImageList();
    }
    return;
  }

  // ── IMAGE LIST ───────────────────────────────────────────────────────────
  if (camSubState == CAM_IMG_LIST) {
    bool changed = false;
    if (btnWasShortPressed(bUp)) {
      if (camListCursor > 0) {
        camListCursor--;
        if (camListCursor < camListFirst) camListFirst = camListCursor;
        changed = true;
      }
    }
    if (btnWasShortPressed(bDown)) {
      if (camListCursor < camImageTotal-1) {
        camListCursor++;
        if (camListCursor >= camListFirst+CAM_LIST_VISIBLE)
          camListFirst = camListCursor-CAM_LIST_VISIBLE+1;
        changed = true;
      }
    }
    if (changed) { camDrawImageList(); return; }

    if (btnWasShortPressed(b2)) {
      if (camImageTotal > 0) {
        bool ok = camDrawSavedPreview(camImageNums[camListCursor]);
        if (!ok) {
          display.clearDisplay();
          display.setTextSize(1); display.setTextColor(WHITE);
          display.setCursor(4,24); display.print("No saved preview");
          display.display();
        }
        camSubState = CAM_IMG_VIEW;
      }
      return;
    }

    if (btnWasShortPressed(b3)) {
      camSubState = CAM_VIEWFINDER;
      cameraDrawIdle();
    }
    return;
  }

  // ── VIEWFINDER ───────────────────────────────────────────────────────────

  // BTN3 = exit app
  if (btnWasShortPressed(b3)) {
    deinitCameraModule();
    radioRecoverAfterCamera();
    if (cameraPausedRadio && radioActive) {
      radioPaused = false;
      radioPlayTrack(radioStation, radioTrack);
      radioPersistFromGlobals();
    }
    inCamera = false; inMenu = true; uiDirty = true;
    lastSecondDrawn = 255; menuEnableButtonsAt = millis()+120;
    redrawMenuNow(); return;
  }

  // BTN1 = open image browser
  if (btnWasShortPressed(b1)) {
    camSubState = CAM_IMG_LIST;
    camScanImages();
    camDrawImageList();
    return;
  }

  // BTN2 = capture
  if (btnWasShortPressed(b2)) {
    tone(BUZZER_PIN, 3000, 50); delay(60);
    cameraDrawFlash();

    // Step 1 — capture grayscale preview (exposure flush happens inside)
    uint8_t *prev = camCapturePreview();
    if (!prev) {
      display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
      display.setCursor(10,28); display.print("Capture failed");
      display.display(); delay(700);
      esp_camera_deinit(); camInitJpeg();
      cameraDrawIdle(); return;
    }

    // Step 2 — render preview on OLED (this IS the saved image)
    camRenderBuf(prev);
    display.display();

    // Step 3 — save sidecar .bmp
    if (sd_ok) camSaveSidecar(prev, imageCount);
    free(prev);

    // Step 4 — re-init JPEG and save full-res photo
    esp_camera_deinit();
    if (camInitJpeg()) {
      // Flush so JPEG exposure settles
      for (int i = 0; i < 3; i++) {
        camera_fb_t *fl = esp_camera_fb_get();
        if (fl) esp_camera_fb_return(fl);
        delay(30);
      }
      if (sd_ok) {
        if (camSaveJpeg(imageCount)) {
          imageCount++;
        } else {
          display.clearDisplay(); display.setTextColor(WHITE); display.setTextSize(1);
          display.setCursor(10,28); display.print("Save failed");
          display.display(); delay(700);
        }
      }
    }

    // Preview stays visible for 2 s then viewfinder returns
    delay(2000);
    cameraDrawIdle();
    return;
  }

  cameraDrawIdle();
}