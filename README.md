# Plant-Health-Monitor
A smart plant monitoring system that tracks soil moisture, temperature, humidity, and light levels in real time, and automatically waters your plants when they need it.


---

## Getting Started

Setting this up from a fresh clone. There are three parts to set up — the
**board**, the **backend**, and the **frontend** — plus Firebase, which needs no
setup at all because it already runs on Google's servers and is where the other
three meet.

The board is the one that works differently: you don't start a program on it each
session. You flash the firmware onto it once, and from then on it runs by itself
whenever it has power, even with your laptop closed. You only reflash when the
firmware changes.

All paths below are relative to the repository root, so `cd` into your clone
first.

### Prerequisites

| Tool | For | Where |
|---|---|---|
| [Arduino IDE](https://www.arduino.cc/en/software) 2.x | the board | arduino.cc |
| [Node.js](https://nodejs.org/) (LTS) | the frontend | nodejs.org |
| [Python](https://www.python.org/downloads/) 3.10+ | the backend | python.org |
| A **data** USB-C cable | flashing the board | not included with the board |

A charge-only USB cable is the single most common setup failure — the board
powers up but no COM port ever appears. If in doubt, use one you've used to copy
files off a phone.

---

### Part 1 — Board (ESP32-S3)

**1. Create your `Secrets.h`.** Required, and the step people forget. WiFi
credentials are kept out of git so nobody's personal password lands in the
repository, which means a fresh clone has no network details in it at all.

```powershell
copy ArduinoPlantMonitor\Secrets.example.h ArduinoPlantMonitor\Secrets.h
```

On macOS or Linux, `cp` instead of `copy`. Then open `Secrets.h` and fill in
your own network:

```c
#define WIFI_SSID     "your-network"
#define WIFI_PASSWORD "your-password"
```

Use a **2.4 GHz** network since the ESP32 cannot see 5 GHz-only networks. 

**2. Add ESP32 board support.** File → Preferences → *Additional boards manager
URLs*:

```
https://espressif.github.io/arduino-esp32/package_esp32_index.json
```

Then Tools → Board → Boards Manager, search `esp32`, install **esp32 by
Espressif Systems**. It's a large download (it includes the whole compiler
toolchain) and only happens once.

**3. Install the libraries.** Tools → Manage Libraries, then install each:

| Library | Used for |
|---|---|
| `Adafruit BME280 Library` | temperature, humidity, pressure |
| `Adafruit Unified Sensor` | dependency of the above — accept when prompted |
| `Adafruit NeoPixel` | the onboard status LED |
| `ArduinoJson` | talking to Firebase (**must be version 7**) |
| `U8g2` | the OLED screen |

ArduinoJson v7 is not optional: the code uses the bare `JsonDocument` type, which
v6 doesn't have. There is no library for the BH1750 light sensor — `Sensors.cpp`
talks to it directly over I2C.

**4. Open the sketch.** File → Open → `ArduinoPlantMonitor/ArduinoPlantMonitor.ino`.
The other files appear as tabs automatically; that only works because the folder
and the `.ino` share a name, so don't rename either.

**5. Pick a USB port — the board has two.** They are labelled `UART` and `USB` in
white silkscreen next to each socket. Not sure which is which? Plug in and look
in Device Manager under *Ports (COM & LPT)*:

| What appears | Which socket |
|---|---|
| `Silicon Labs CP210x` or `USB-SERIAL CH340` | **UART** — recommended |
| `USB JTAG/serial debug unit` or `ESP32-S3` | native **USB** |

UART is the more forgiving one: it handles the auto-reset handshake, so uploads
work without touching buttons, and it enumerates whether or not the firmware is
healthy. The native port depends on the running firmware to present itself, so a
sketch that crashes early makes it disappear and the board looks bricked when it
isn't.

**6. Set the board.** Tools → Board → esp32 → **ESP32S3 Dev Module**. Then,
still under Tools:

- **Port** — the `COM*` that appeared in step 5
- **USB CDC On Boot** — this must match your socket:

| Socket | USB CDC On Boot |
|---|---|
| UART | **Disabled** |
| USB (native) | **Enabled** |

Get this pairing wrong and everything uploads fine but Serial Monitor stays
completely blank — a confusing way to lose an hour.

**7. Upload** with the → arrow, then open Serial Monitor and set the baud
dropdown (bottom right) to **115200**. Press RESET on the board. You should see:

```
Plant Monitor
OLED initialized.
BME280 found at 0x76
Connecting to WiFi...
WiFi connected
System ready!
```

and a full sensor report every 30 seconds. The OLED runs a loading bar, then
settles into an animated face.

The onboard LED is a status light: blinking red means it's still trying to join
WiFi, green for two seconds means it just connected, blue flashes during an
upload to Firebase, steady orange means the sensors weren't found.

#### If the board misbehaves

| Symptom | Cause |
|---|---|
| No `COM` port appears | Charge-only USB cable, or try the other socket |
| `Secrets.h is missing` | You skipped step 1 |
| Garbled text like `R~?gB??k??` | Serial Monitor isn't at **115200** |
| Uploads fine, no serial output | `USB CDC On Boot` doesn't match your socket (step 6) |
| Upload fails to connect | Hold **BOOT**, tap **RESET**, release **BOOT**, upload again |
| `Sensor initialization failed`, orange LED | BME280 not answering — check 3V3/GND and that SDA/SCL are on GPIO 8/9 |
| `text section exceeds available space` | Tools → Partition Scheme → **Huge APP (3MB No OTA)** |
| Board reboots whenever the pump runs | The 12 V side is browning out the 5 V rail — the pump needs its own supply, sharing only ground |

Wiring, pinout and calibration values are in [ESP_Scripts/Wiring.md](ESP_Scripts/Wiring.md).

---

### Part 2 — Backend (FastAPI)

Runs on your machine at `http://localhost:8000`. It loads the ML model, reads the
latest sensors from Firebase, predicts plant health, and runs the auto-watering
loop.

**One-time setup**, from the repository root:

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -r requirements.txt
```

On macOS or Linux the activate line is `source .venv/bin/activate`.

> If `Activate.ps1` gives a red "running scripts is disabled" error, run
> PowerShell once as Administrator and enter:
> `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`

**Every session:**

```powershell
.\.venv\Scripts\Activate.ps1
cd Backend
uvicorn main:app --reload
```

**The `cd Backend` is not optional.** `main.py` looks for the model at the
relative path `Model/plant_health_rf_model.pkl`, so you must be *inside*
`Backend` when you start, or you get `FileNotFoundError: Model not found`.

You'll know it worked when you see `Uvicorn running on http://127.0.0.1:8000`.

---

### Part 3 — Frontend (React + Vite)

Runs at `http://localhost:5173`. Note that `localhost:5173` is only the delivery
service — the app itself then runs inside your browser, which is why it can reach
the backend on `localhost:8000`.

**One-time setup:**

```powershell
cd Frontend
npm install
```

This downloads into `node_modules`, which is deliberately not committed — that's
why you have to run it yourself after cloning.

**Every session:**

```powershell
cd Frontend
npm run dev
```

Then open **http://localhost:5173**.

Start the backend first if you can. If you don't, the page still loads but shows
"Unavailable" where the ML prediction goes — it retries every 30 seconds, so it
fixes itself once the backend is up.

The backend URL can be overridden with a `VITE_API_BASE_URL` environment
variable; it defaults to `http://localhost:8000`.

---

### Checking it all works

The backend and frontend each need their own terminal, because each command runs
until you stop it with **Ctrl+C**. That's normal — a server is supposed to sit
there and wait.

| URL | What it should show |
|---|---|
| http://localhost:8000 | `{"message":"Plant Health API is running"}` |
| http://localhost:8000/plant | current sensors + a health prediction |
| http://localhost:8000/docs | clickable page for every endpoint (free with FastAPI) |
| http://localhost:5173 | the dashboard |

| Problem | Meaning |
|---|---|
| `FileNotFoundError: Model not found` | You forgot `cd Backend` |
| `'uvicorn' is not recognized` | Virtual environment isn't active |
| `'vite' is not recognized` | You never ran `npm install` in `Frontend` |
| `[Errno 10048] address already in use` | Already running in another terminal |
| ML shows "Unavailable" | Backend isn't running |
| History tab empty on "1h" | Normal if the board has been off — try 24h or 7d |

**You don't need the hardware to develop.** The board writes to Firebase and
Firebase keeps that data, so the app has real readings to display even with the
board switched off. The numbers just stop updating, frozen at the last reading
sent.
