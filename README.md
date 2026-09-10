# Plant Health Monitor

A smart plant monitoring system built on an ESP32-S3. It tracks soil moisture,
temperature, humidity, air pressure, and light, waters plants automatically
when the soil gets dry, runs a grow light on a schedule, and classifies plant
health with a machine learning model. A React dashboard shows live readings
and history, and lets you control watering and lighting manually.

Everything meets in a Firebase Realtime Database: the board publishes sensor
data and reads back commands, a FastAPI backend runs the ML model and the
automatic watering logic, and the frontend reads and writes the same database
for a live view and manual control.

## Features

- Live sensor readings (soil moisture, temperature, humidity, pressure, and
  ambient light) pushed to Firebase every 30 seconds, plus a rolling history
- Automatic watering: the backend triggers the pump when soil moisture drops
  below threshold, with a standalone on-board fallback if the backend goes
  quiet while the soil stays dry
- Water tank level sensing: the pump refuses to run, and the dashboard flags
  it, when the tank is empty
- Grow light auto-control on a daily photoperiod, vetoed when the room is
  already bright enough, with manual on/off override from the dashboard
  (manual "on" expires back to auto after 2 hours)
- ML-based health classification (Random Forest: Healthy / Moderate Stress /
  High Stress) served by the backend and mirrored on the board's display
- OLED display with a face that reflects the plant's health, animated
  loading/scanning/pumping screens, and a live sensor readout
- RGB status LED for Wi-Fi and sensor state
- React dashboard: live status, VPD, historical charts with 1h/6h/12h/24h/7d
  ranges, a computed plant health score with trend summaries, per-plant
  presets, alert thresholds, manual "Water now", light control, and
  light/dark themes
- Multi-plant support in the dashboard (readings are tagged and stored per
  plant ID)

## Hardware

- ESP32-S3 DevKitC-1
- BME280, temperature, humidity, pressure (I2C)
- BH1750, ambient light (I2C)
- AZDelivery capacitive soil moisture sensor (analog)
- Water level switch, for tank-empty detection (digital)
- 2x Songle SRD-12VDC-SL-C relay module: one for the pump, one for the grow
  light
- QR50E water pump
- Grow light, relay-switched
- SH1106 128x64 I2C OLED display
- Adafruit NeoPixel (single RGB LED) for status

## Submission Materials

[`Submission/`](Submission/) holds the project write-up, slides, and demo:
- [`CPS_Project_Technical_Report.pdf`](Submission/CPS_Project_Technical_Report.pdf): the technical report
- [`Presentation_Autonomous_Plant_Keeper.pptx`](Submission/Presentation_Autonomous_Plant_Keeper.pptx): the presentation slides
- [`Autonomous_Plant_Keeper_Slides.pdf`](Submission/Autonomous_Plant_Keeper_Slides.pdf): the same presentation slides as a PDF, without the video
- [`PlantKeeper_Demo.mov`](Submission/PlantKeeper_Demo.mov): a video demo of the system in use

## Tech stack

- **Firmware**: C++ on the Arduino framework, targeting the ESP32-S3, flashed
  through the Arduino IDE
- **Backend**: Python, FastAPI, scikit-learn (Random Forest classifier)
- **Frontend**: React 19 + Vite
- **Data layer**: Firebase Realtime Database

## Setup

### 1. Firmware ([`ESP32S3PlantMonitor/`](ESP32S3PlantMonitor/))

1. Copy the secrets template ([`Secrets.example.h`](ESP32S3PlantMonitor/Secrets.example.h))
   and fill in your Wi-Fi credentials (2.4 GHz, since the ESP32-S3 can't join
   a 5 GHz-only network):

   ```
   copy ESP32S3PlantMonitor\Secrets.example.h ESP32S3PlantMonitor\Secrets.h
   ```

   ```c
   #define WIFI_SSID     "your-network"
   #define WIFI_PASSWORD "your-password"
   ```

2. In the Arduino IDE, add the ESP32 board package under File → Preferences
   → Additional boards manager URLs:

   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```

   Then install **esp32 by Espressif Systems** from Boards Manager.

3. Install these libraries from the Library Manager:
   - `Adafruit BME280 Library`
   - `Adafruit Unified Sensor`
   - `Adafruit NeoPixel`
   - `ArduinoJson` (v7)
   - `U8g2`

4. Open [`ESP32S3PlantMonitor/ESP32S3PlantMonitor.ino`](ESP32S3PlantMonitor/ESP32S3PlantMonitor.ino).

5. Under Tools, select board **ESP32S3 Dev Module**, pick the board's COM
   port, and set **USB CDC On Boot** to match the socket you're using
   (Disabled for the UART/CP210x port, Enabled for the native USB port).

6. Upload, then open Serial Monitor at 115200 baud.

### 2. Backend ([`Backend/`](Backend/))

```
python -m venv .venv
.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

Run it from the [`Backend`](Backend/) directory, since the model path is relative:

```
cd Backend
uvicorn main:app --reload
```

Serves on `http://localhost:8000`. Watering threshold and pump duration are
read at startup from [`ESP32S3PlantMonitor/Config.h`](ESP32S3PlantMonitor/Config.h),
so the backend and the board always agree on when to water.

### 3. Frontend ([`Frontend/`](Frontend/))

```
cd Frontend
npm install
npm run dev
```

Serves on `http://localhost:5173`. It talks to the backend at
`http://localhost:8000` by default; override with `VITE_API_BASE_URL`.

## Running it

Start the backend and frontend, each in its own terminal. The board runs
independently once flashed. It reconnects and resumes on its own after a
power cycle, no laptop required. The dashboard works with the board offline
too; it just shows the last readings Firebase has.

- `http://localhost:8000/docs`: API reference
- `http://localhost:8000/plant`: current sensors + health prediction
- `http://localhost:5173`: dashboard

## Repo structure

- [`ESP32S3PlantMonitor/`](ESP32S3PlantMonitor/): ESP32-S3 firmware (C++, Arduino framework)
- [`Backend/`](Backend/): FastAPI app, ML model, training script
- [`Frontend/`](Frontend/): React + Vite dashboard
- [`Submission/`](Submission/): project write-up, slides, and demo video
