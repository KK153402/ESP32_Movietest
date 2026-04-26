# ESP32 動画プレイヤー 接続・使い方ガイド
![配線](images/20260425_214529.JPG)
## 使用部品

| 部品 | 型番・備考 |
|------|-----------|
| マイコン | ESP32-DevKitC |
| ディスプレイ | 1.54インチ 240×240 TFT LCD (ST7789) |
| SDカードモジュール | 汎用SPIタイプ |
| DACモジュール | PCM5102 |
| ボタン | タクトスイッチ × 3 |

---

## 配線

### ディスプレイ（ST7789 240×240）

| ディスプレイ | ESP32 |
|------------|-------|
| GND | GND |
| VCC | 3.3V |
| SCL | GPIO18 |
| SDA | GPIO23 |
| RES | GPIO33 |
| DC | GPIO27 |
| CS | GPIO5 |
| BLK | 3.3V |

### SDカードモジュール

| SDカード | ESP32 |
|---------|-------|
| VCC | 3.3V（または5V、モジュール次第） |
| GND | GND |
| CS | GPIO13 |
| MOSI | GPIO15 |
| CLK | GPIO14 |
| MISO | GPIO4 |

### PCM5102（I2S DAC）

| PCM5102 | ESP32 / 接続先 |
|---------|---------------|
| VIN | **5V** |
| GND | GND |
| BCK | GPIO26 |
| LRCK | GPIO25 |
| DIN | GPIO22 |
| SCK | GND（MCLKフリーモード） |
| FMT | GND（I2Sフォーマット） |
| DEMP | GND（デエンファシスOFF） |
| XSMT | 3.3V（ソフトミュート解除） |

> イヤホンはPCM5102のLOUT/ROUTに接続する
> PCM5102のロジック（BCK/LRCK/DIN）は3.3Vで動作するが、VINは5V給電が必要

### ボタン

| 機能 | ESP32 | 接続方法 |
|------|-------|---------|
| 進む（次の動画） | GPIO16 | 片端をGPIO16、もう片端をGND |
| 戻る（前の動画） | GPIO17 | 片端をGPIO17、もう片端をGND |
| ランダム | GPIO21 | 片端をGPIO21、もう片端をGND |

> コード内でINPUT_PULLUPを設定しているので外付けプルアップ抵抗は不要

---

## PlatformIO プロジェクト構成

```
ESP32-Movie-Player/
├── platformio.ini
├── src/
│   ├── main.cpp
│   └── MjpegClass.h    ← 下記URLから取得してここに置く
├── tools/
│   └── videoConvert.py
└── README.md
```

**MjpegClass.h の入手：**
https://github.com/moononournation/Arduino_GFX/blob/master/examples/VideoPlayer/MjpegClass.h
（ページ右上の「Raw」→ 右クリック → 名前を付けて保存）

### platformio.ini

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32dev
framework = arduino
lib_deps =
    moononournation/GFX Library for Arduino@^1.6.5
    earlephilhower/ESP8266Audio@^2.4.1
    bitbank2/JPEGDEC@^1.8.4
```

> 標準の `espressif32` プラットフォームではビルドエラーが出るため、pioarduino版を使用する

---

## 動画変換手順（Pythonスクリプト）

### 1. 仮想環境のセットアップ（初回のみ）

```bash
cd tools
python -m venv .venv

# 有効化（Windows）
.venv\Scripts\activate.bat

pip install imageio-ffmpeg
```

### 2. 動画ファイルを配置

```
tools/
└── input_videos/   ← ここにmp4などを入れる（スペースなしのファイル名推奨）
```

対応フォーマット: `.mp4` `.mkv` `.avi` `.mov` `.webm`

### 3. スクリプト実行

```bash
# 仮想環境が有効な状態で
python videoConvert.py
```

### 4. 出力確認

```
tools/
└── output_sd/
    ├── 動画名.mjpeg   ← 映像（240×240, 24fps, MJPEG）
    └── 動画名.mp3     ← 音声（22050Hz, モノラル, 128kbps）
```

### 5. SDカードにコピー

- SDカードをFAT32でフォーマット
- `output_sd/` 内の `.mjpeg` と `.mp3` をSDカードのルートにコピー
- **ペアになっている2ファイルは必ず同名にすること**（例: `video.mjpeg` と `video.mp3`）

---

## トラブルシューティング

| 症状 | 確認箇所 |
|------|---------|
| 画面が表示されない | BLKが3.3Vに繋がっているか確認 |
| 映像の位置がずれる | main.cpp の ST7789 コンストラクタのオフセットを `0, 0, 0, 0` にする |
| 音が出ない | PCM5102のVINが**5V**、XSMTが3.3V、SCKがGNDに繋がっているか確認 |
| SDが認識しない | FAT32フォーマット確認、SDカードモジュールのVCCを確認 |
| 動画が再生されない | .mjpegと.mp3が同名ペアでSDルートにあるか確認 |
| 音がゆっくり再生される | videoConvert.py の `-ar` が `22050` になっているか確認 |
