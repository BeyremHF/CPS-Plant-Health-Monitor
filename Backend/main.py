from fastapi import FastAPI, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field
import pandas as pd
import joblib
import requests
import os
import threading
import time
import re
import sys
import pathlib


#
# Configuration
FIREBASE_URL = (
    "https://plant-health-monitor-esp32-default-rtdb"
    ".europe-west1.firebasedatabase.app"
)

MODEL_PATH = "Model/plant_health_rf_model.pkl"
ENCODER_PATH = "Model/label_encoder.pkl"

# Watering settings are NOT defined here. ArduinoPlantMonitor/Config.h is the
# single source of truth and this file parses them out of it
CONFIG_H = (
    pathlib.Path(__file__).resolve().parent.parent
    / "ArduinoPlantMonitor"
    / "Config.h"
)

# Set to False by read_firmware_define() if anything went wrong, so the warning
# can be repeated later instead of scrolling away behind uvicorn's banner.
firmware_config_ok = True


def loud_warning(*lines):
    """Print an unmissable banner to stderr.

    A silently wrong watering threshold either floods a plant or lets it dry
    out, and neither shows up until damage is done -- so this is deliberately
    hard to scroll past.
    """
    bar = "!" * 74
    print("", file=sys.stderr)
    print(bar, file=sys.stderr)
    print("!!" + " FIRMWARE CONFIG NOT LOADED ".center(70, " ") + "!!", file=sys.stderr)
    print(bar, file=sys.stderr)
    for line in lines:
        print("!! " + line.ljust(69) + "!!", file=sys.stderr)
    print(bar, file=sys.stderr)
    print("", file=sys.stderr)
    sys.stderr.flush()


def read_firmware_define(name, default, critical=True):
    """Read a #define out of the firmware's Config.h.

    Falls back to `default` and shouts about it, rather than raising: a backend
    that refuses to boot is worse than one that waters on a stale number and
    says so every 30 seconds.

    `critical=False` is for values that only affect what gets displayed. Those
    still fall back, but quietly -- raising the watering banner over a wrong
    display number would teach everyone to ignore the banner.
    """
    global firmware_config_ok

    try:
        text = CONFIG_H.read_text(encoding="utf-8")
    except OSError as exc:
        if not critical:
            print(f"[config] {name} unavailable ({exc}); using {default}")
            return default
        firmware_config_ok = False
        loud_warning(
            f"Could not read {CONFIG_H}",
            f"  {exc}",
            "",
            f"FALLING BACK TO {name} = {default}",
            "This value is a GUESS. If the firmware uses a different one, the",
            "board's serial log and this backend will disagree about watering.",
            "Fix: run the backend from a full checkout of the repo.",
        )
        return default

    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+([0-9]+(?:\.[0-9]+)?)",
        text,
        re.MULTILINE,
    )

    if not match:
        if not critical:
            print(f"[config] {name} not found in {CONFIG_H.name}; using {default}")
            return default
        firmware_config_ok = False
        loud_warning(
            f"'{name}' was not found in {CONFIG_H.name}",
            "",
            f"FALLING BACK TO {name} = {default}",
            "Either the #define was renamed or removed, or its formatting",
            "changed enough that the parser no longer matches it.",
            "Fix: check the define still reads '#define NAME <number>'.",
        )
        return default

    value = float(match.group(1))
    print(f"[config] {name} = {value} (from {CONFIG_H.name})")
    return value


SOIL_MOISTURE_THRESHOLD = read_firmware_define("WATERING_THRESHOLD", 40.0)
PUMP_DURATION = int(read_firmware_define("WATERING_PUMP_SECONDS", 2))

UPDATE_INTERVAL_SECONDS = 30
MIN_PUMP_DURATION = 1
MAX_PUMP_DURATION = 30


# FastAPI
app = FastAPI(
    title="Plant Health API",
    description="Backend API for the ESP32 plant health monitoring system",
    version="1.0.0",
)

app.add_middleware(
    CORSMiddleware,
    # Vite's default dev server ports. Add your deployed frontend origin here too.
    allow_origins=[
        "http://localhost:5173",
        "http://127.0.0.1:5173",
    ],
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
)

class PumpRequest(BaseModel):
    duration: int = Field(
        default=PUMP_DURATION,
        ge=MIN_PUMP_DURATION,
        le=MAX_PUMP_DURATION,
    )


if not os.path.exists(MODEL_PATH):
    raise FileNotFoundError(f"Model not found: {MODEL_PATH}")
if not os.path.exists(ENCODER_PATH):
    raise FileNotFoundError(f"Encoder not found: {ENCODER_PATH}")
model = joblib.load(MODEL_PATH)
label_encoder = joblib.load(ENCODER_PATH)


# Avoid confusion with the model's class names ("Healthy", "Moderate Stress", "High Stress")
MODEL_LABEL_PREFIX = "Random Forest: "


def predict_plant_health(sensor_data):
    input_data = pd.DataFrame([sensor_data])
    prediction = model.predict(input_data)[0]
    label = label_encoder.inverse_transform([prediction])[0]
    return f"{MODEL_LABEL_PREFIX}{label}"


# Firebase
def read_firebase():
    response = requests.get(
        f"{FIREBASE_URL}/.json",
        timeout=5
    )
    response.raise_for_status()
    return response.json()

def set_pump_trigger(duration):
    print(f"Manually triggered the pump for {duration}")
    data = {
        "trigger": True,
        "duration": duration
    }
    response = requests.put(
        f"{FIREBASE_URL}/pump.json",
        json=data,
        timeout=5
    )

    response.raise_for_status()

# Firebase -> Model Data
def firebase_to_model_input(firebase_data):
    sensors = firebase_data["sensors"]

    return {
        "Soil_Moisture": sensors["soil_moisture"],
        "Ambient_Temperature": sensors["temperature"],
        "Soil_Temperature": sensors["temperature"],
        "Humidity": sensors["humidity"],
        "Light_Intensity": sensors["light"],
    }


# Plant Status
def get_plant_status():
    firebase_data = read_firebase()
    sensor_data = firebase_to_model_input(firebase_data)
    health_status = predict_plant_health(sensor_data)
    pump = firebase_data.get("pump", {})
    return {
        "sensors": sensor_data,
        "health": health_status,
        "pump": {
            "trigger": pump.get("trigger", False),
            "duration": pump.get("duration", 0),
        }
    }


# Plant History
def get_plant_history(plant_id, n):
    response = requests.get(
        f"{FIREBASE_URL}/history/{plant_id}.json",
        params={
            "orderBy": '"$key"',
            "limitToLast": n
        },
        timeout=5
    )

    response.raise_for_status()

    history = response.json()

    if not history:
        return []

    records = list(history.values())

    # Newest first
    records.sort(
        key=lambda x: x.get("timestamp", 0),
        reverse=True
    )

    return records


# API Endpoints
@app.get("/")
def root():
    return {
        "message": "Plant Health API is running"
    }

@app.get("/plant")
def plant():
    try:
        return get_plant_status()
    except requests.RequestException as e:
        raise HTTPException(
            status_code=503,
            detail=f"Firebase unavailable: {e}"
        )
    except Exception as e:
        raise HTTPException(
            status_code=500,
            detail=str(e)
        )
    
@app.get("/plant/history")
def plant_history(plant_id: str, n: int = 100):
    try:
        return get_plant_history(plant_id, n)

    except requests.RequestException as e:
        raise HTTPException(
            status_code=503,
            detail=f"Firebase unavailable: {e}"
        )


@app.post("/pump")
def activate_pump(pump_request: PumpRequest = PumpRequest()):
    try:
        set_pump_trigger(pump_request.duration)
        print(f"Manually triggered for {pump_request.duration}s")
        return {
            "success": True,
            "duration": pump_request.duration
        }
    
    except requests.RequestException as e:
        raise HTTPException(
            status_code=503,
            detail=f"Could not communicate with Firebase: {e}"
        )


# Automatic Watering
def automatic_watering_loop():
    while True:
        try:
            if not firmware_config_ok:
                print(
                    "!! WATERING ON GUESSED CONFIG -- "
                    f"threshold {SOIL_MOISTURE_THRESHOLD}% / "
                    f"{PUMP_DURATION}s were NOT read from Config.h",
                    file=sys.stderr,
                )

            firebase_data = read_firebase()
            sensor_data = firebase_to_model_input(firebase_data)
            moisture = sensor_data["Soil_Moisture"]
            pump = firebase_data.get("pump", {})
            pump_trigger = pump.get("trigger", False)
            if moisture < SOIL_MOISTURE_THRESHOLD:
                if not pump_trigger:
                    print(
                        f"Moisture {moisture}% < "
                        f"{SOIL_MOISTURE_THRESHOLD}%"
                    )
                    set_pump_trigger(PUMP_DURATION)
                    print(f"Pump triggered for {PUMP_DURATION}")
                else:
                    print("Pump already triggered.")
        except Exception as e:
            print(f"Automatic watering error: {e}")

        time.sleep(UPDATE_INTERVAL_SECONDS)

@app.on_event("startup")
def start_background_tasks():
    thread = threading.Thread(
        target=automatic_watering_loop,
        daemon=True
    )
    thread.start()
    print("Automatic watering started.")

    if firmware_config_ok:
        print(
            f"[config] watering below {SOIL_MOISTURE_THRESHOLD}% "
            f"for {PUMP_DURATION}s, in sync with the firmware"
        )
    else:
        # Repeated here on purpose: the import-time banner has by now scrolled
        # away behind uvicorn's startup output.
        loud_warning(
            "The backend is running on FALLBACK watering values.",
            "",
            f"threshold = {SOIL_MOISTURE_THRESHOLD}%   duration = {PUMP_DURATION}s",
            "",
            "These were NOT read from ArduinoPlantMonitor/Config.h, so they",
            "may not match what the board is actually doing. See the earlier",
            "banner for the cause.",
        )
