#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>

#define BLACK 0x0000

#include <Arduino_GFX_Library.h>

#include <AudioFileSourceFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

#include "MjpegClass.h"

#define FPS 24
#define MJPEG_BUFFER_SIZE (240 * 240 * 2 / 8)

// SD Card (記事の配線)
const int SD_MISO = 4;
const int SD_SCK  = 14;
const int SD_MOSI = 15;
const int SD_CS   = 13;

// Display: ST7789 240x240 (記事の配線)
const int LCD_SCK   = 18;
const int LCD_MISO  = 19;
const int LCD_MOSI  = 23;
const int LCD_DC    = 27;
const int LCD_RESET = 33;
const int LCD_CS    = 5;
// BLK → 3.3V に接続

// PCM5102 I2S (ESP32 デフォルトI2S0ピン)
// PCM5102 BCK  → GPIO26
// PCM5102 LRCK → GPIO25
// PCM5102 DIN  → GPIO22
// PCM5102 SCK  → GND (MCLKフリーモード)
// PCM5102 FMT  → GND (I2Sフォーマット)
// PCM5102 DEMP → GND
// PCM5102 XSMT → 3.3V (ミュート解除)

// ボタン (記事の配線)
const int BTN_NEXT   = 16;
const int BTN_PREV   = 17;
const int BTN_RANDOM = 21;

Arduino_ESP32SPI *bus = new Arduino_ESP32SPI(LCD_DC, LCD_CS, LCD_SCK, LCD_MOSI, LCD_MISO);
// row_offset=80 は1.54インチ 240x240 ST7789の標準値
// 表示がずれる場合は 0, 80 → 0, 0 に変更
Arduino_GFX *gfx = new Arduino_ST7789(bus, LCD_RESET, 0, true, 240, 240, 0, 0, 0, 0);

static AudioGeneratorMP3 *mp3   = NULL;
static AudioFileSourceFS *aFile = NULL;
static AudioOutputI2S    *out   = NULL;
static MjpegClass mjpeg;
static unsigned long total_show_video = 0;

int noFiles = 0;
String videoFilename;
String audioFilename;
int fileNo = 1;
bool buttonPressed   = false;
bool fullPlaythrough = true;
unsigned long lastDebounceTime  = 0;
const unsigned long debounceDelay = 50;

File root;

void IRAM_ATTR incrFileNo() {
  if ((millis() - lastDebounceTime) > debounceDelay) {
    fileNo           = (fileNo >= noFiles) ? 1 : fileNo + 1;
    buttonPressed    = true;
    fullPlaythrough  = false;
    lastDebounceTime = millis();
  }
}

void IRAM_ATTR decrFileNo() {
  if ((millis() - lastDebounceTime) > debounceDelay) {
    fileNo           = (fileNo <= 1) ? noFiles : fileNo - 1;
    buttonPressed    = true;
    fullPlaythrough  = false;
    lastDebounceTime = millis();
  }
}

void IRAM_ATTR randomFileNo() {
  if ((millis() - lastDebounceTime) > debounceDelay) {
    fileNo           = random(1, noFiles + 1);
    buttonPressed    = true;
    fullPlaythrough  = false;
    lastDebounceTime = millis();
  }
}

bool isAudioVideoPair(File const &entry) {
  String name = entry.path();
  if (!name.endsWith(".mjpeg")) return false;
  return SD.exists(name.substring(0, name.length() - 6) + ".mp3");
}

int getNoFiles(File dir) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && isAudioVideoPair(entry)) ++noFiles;
    entry.close();
  }
  return noFiles;
}

void getFilenames(File dir, int targetNo) {
  int counter = 0;
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    if (!entry.isDirectory() && isAudioVideoPair(entry)) {
      if (++counter == targetNo) {
        videoFilename = entry.path();
        audioFilename = videoFilename.substring(0, videoFilename.length() - 6) + ".mp3";
      }
    }
    entry.close();
  }
}

static int drawMCU(JPEGDRAW *pDraw) {
  unsigned long s = millis();
  gfx->draw16bitRGBBitmap(pDraw->x, pDraw->y, pDraw->pPixels, pDraw->iWidth, pDraw->iHeight);
  total_show_video += millis() - s;
  return 1;
}

void playVideo(String vName, String aName) {
  int next_frame = 0;
  unsigned long start_ms, curr_ms, next_frame_ms;
  gfx->fillScreen(BLACK);
  if (mp3 && mp3->isRunning()) mp3->stop();
  if (!aFile->open(aName.c_str()))
    Serial.println(F("Failed to open audio file"));

  File vFile = SD.open(vName);
  uint8_t *mjpeg_buf = (uint8_t *)malloc(MJPEG_BUFFER_SIZE);

  if (!mjpeg_buf) {
    Serial.println(F("mjpeg_buf malloc failed!"));
  } else {
    mjpeg.setup(&vFile, mjpeg_buf, drawMCU, false, 0, 0, 240, 240);
  }

  if (!vFile || vFile.isDirectory()) {
    Serial.println(F("ERROR: Failed to open video file"));
  } else {
    if (!mp3->begin(aFile, out))
      Serial.println(F("Failed to start audio!"));

    start_ms      = millis();
    next_frame_ms = start_ms + (++next_frame * 1000 / FPS);

    while (vFile.available() && !buttonPressed) {
      mjpeg.readMjpegBuf();
      curr_ms = millis();
      if (curr_ms < next_frame_ms) {
        mjpeg.drawJpg();
      }
      if (mp3->isRunning() && !mp3->loop()) mp3->stop();
      while (millis() < next_frame_ms) vTaskDelay(1);
      next_frame_ms = start_ms + (++next_frame * 1000 / FPS);
    }

    if (!fullPlaythrough) mp3->stop();
    buttonPressed = false;
    vFile.close();
    aFile->close();
  }

  if (mjpeg_buf) free(mjpeg_buf);
}

void setup() {
  WiFi.mode(WIFI_OFF);
  Serial.begin(115200);

  pinMode(BTN_NEXT,   INPUT_PULLUP);
  pinMode(BTN_PREV,   INPUT_PULLUP);
  pinMode(BTN_RANDOM, INPUT_PULLUP);
  attachInterrupt(BTN_NEXT,    incrFileNo,   RISING);
  attachInterrupt(BTN_PREV,    decrFileNo,   RISING);
  attachInterrupt(BTN_RANDOM,  randomFileNo, RISING);

  randomSeed(analogRead(12));

  out   = new AudioOutputI2S();
  out->SetPinout(26, 25, 22);  // BCK, WS(LRCK), DATA(DIN)
  mp3   = new AudioGeneratorMP3();
  aFile = new AudioFileSourceFS(SD);

  gfx->begin();
  gfx->fillScreen(BLACK);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, SPI, 40000000)) {
    Serial.println(F("ERROR: SD card mount failed!"));
    gfx->println(F("SD Error"));
  } else {
    root    = SD.open("/");
    noFiles = getNoFiles(root);
    Serial.printf("Found %d file pairs\n", noFiles);
  }
}

void loop() {
  root = SD.open("/");
  getFilenames(root, fileNo);
  playVideo(videoFilename, audioFilename);

  if (fullPlaythrough) {
    if (++fileNo > noFiles) fileNo = 1;
  } else {
    fullPlaythrough = true;
  }
}
