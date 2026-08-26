import { useEffect, useMemo, useRef, useState } from "react";
import { database } from "./firebase";
import { ref, onValue, set } from "firebase/database";
import {
  calcVPD,
  computePlantHealthScore,
  computeTrendSummaryCards,
  filterRecordsByTimeframe,
  getHistoryFetchLimit,
  getMemHistorySliceCount,
  getLowScoreWarning,
  normalizeHistoryRecords,
  recordsToChartData,
} from "./historyAnalytics";
import { fetchPlantHistory, fetchPlantStatus, triggerPumpBackend } from "./api";
import "./App.css";

const BACKEND_POLL_MS = 30_000; // matches backend UPDATE_INTERVAL_SECONDS

/* ── Icons ─────────────────────────────────────────────────── */
const I = {
  leaf:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M11 20A7 7 0 0 1 4 13c0-6 7-9 16-9 0 9-3 16-9 16Z"/><path d="M4 20 20 4"/></svg>,
  grid:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="3" y="3" width="7" height="7" rx="1"/><rect x="14" y="3" width="7" height="7" rx="1"/><rect x="3" y="14" width="7" height="7" rx="1"/><rect x="14" y="14" width="7" height="7" rx="1"/></svg>,
  chart:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M3 3v18h18"/><path d="M7 14l4-4 4 3 5-6"/></svg>,
  gear:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1 1.7 1.7 0 0 0-.3-1.8l-.1-.1A2 2 0 1 1 7 4.6l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1Z"/></svg>,
  drop:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2.7s6 7 6 11a6 6 0 1 1-12 0c0-4 6-11 6-11Z"/></svg>,
  therm:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M14 14.8V4a2 2 0 1 0-4 0v10.8a4 4 0 1 0 4 0Z"/></svg>,
  sun:     <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></svg>,
  wind:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M3 8h12a3 3 0 1 0-3-3"/><path d="M3 16h15a3 3 0 1 1-3 3"/><path d="M3 12h7"/></svg>,
  bulb:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M9 18h6"/><path d="M10 22h4"/><path d="M12 2a7 7 0 0 0-4 12.7c.7.6 1 1.4 1 2.3v1h6v-1c0-.9.3-1.7 1-2.3A7 7 0 0 0 12 2Z"/></svg>,
  plus:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 5v14M5 12h14"/></svg>,
  moon:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8Z"/></svg>,
  sunny:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><circle cx="12" cy="12" r="4"/><path d="M12 3v1M12 20v1M4 12H3M21 12h-1M5.6 5.6l.7.7M17.7 17.7l.7.7M5.6 18.4l.7-.7M17.7 6.3l.7-.7"/></svg>,
  history: <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M3 12a9 9 0 1 0 3-6.7"/><path d="M3 4v5h5"/><path d="M12 7v5l3 2"/></svg>,
  chip:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"/></svg>,
  wifi:    <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M5 12.5a10 10 0 0 1 14 0"/><path d="M2 8.8a14 14 0 0 1 20 0"/><path d="M8.5 16a5 5 0 0 1 7 0"/><circle cx="12" cy="19" r="1"/></svg>,
  alert:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M10.29 3.86 1.82 18a2 2 0 0 0 1.71 3h16.94a2 2 0 0 0 1.71-3L13.71 3.86a2 2 0 0 0-3.42 0Z"/><line x1="12" y1="9" x2="12" y2="13"/><line x1="12" y1="17" x2="12.01" y2="17"/></svg>,
  expand:  <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7"/></svg>,
  close:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M18 6 6 18M6 6l12 12"/></svg>,
  gauge:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2a10 10 0 1 0 0 20 10 10 0 0 0 0-20Z"/><path d="M12 12 8 8"/><circle cx="12" cy="12" r="1.5" fill="currentColor"/><path d="M7 17A7 7 0 0 1 8.5 7M17 17a7 7 0 0 0-1.5-10"/></svg>,
  vapor:   <svg viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round"><path d="M12 2v6M12 22v-6M8 6c0 2 4 3 4 6s-4 4-4 6M16 6c0 2-4 3-4 6s4 4 4 6"/></svg>,
};

/* ── Plants ─────────────────────────────────────────────────── */
const PLANTS = [
  { id: "basil-1",      defaultLabel: "Basil 1",      dot: "green" },
  { id: "strawberry-1", defaultLabel: "Strawberry 1", dot: "pink"  },
];

// Maps the board's published plant state onto a BMO expression. Slugs come
// from plantStateSlug() in ArduinoPlantMonitor.ino.
const BOARD_STATE_MOOD = {
  healthy:         "happy",
  moderate_stress: "neutral",
  high_stress:     "sad",
};

const DEFAULT_PLANT_SETTINGS = {
  name:          null,
  dotColor:      null,
  // Mirrors ArduinoPlantMonitor/Config.h. Keep them aligned: the board
  // publishes its own verdict and these only drive the per-sensor alert list.
  thresholds:    { moistureMin: 40, tempMin: 12, tempMax: 35, lightMin: 100 },
  waterDuration: 3,
  graphs:        { moisture: true, temperature: true, humidity: false, light: false, pressure: false, vpd: false },
  timeframe:     "24h",
};

function loadPlantSettings() {
  try { return JSON.parse(localStorage.getItem("pm.plantSettings")) || {}; } catch { return {}; }
}
function getPS(all, id) {
  const saved = all[id] || {};
  return {
    ...DEFAULT_PLANT_SETTINGS,
    ...saved,
    thresholds: { ...DEFAULT_PLANT_SETTINGS.thresholds, ...(saved.thresholds || {}) },
    graphs:     { ...DEFAULT_PLANT_SETTINGS.graphs,     ...(saved.graphs     || {}) },
  };
}

/* ── BMO ────────────────────────────────────────────────────── */
function BMO({ mood = "happy" }) {
  return (
    <div className="bmo">
      <svg viewBox="0 0 120 75" width="100%" height="100%" style={{ display: "block" }}>
        {/* Outer body */}
        <rect x="1" y="1" width="118" height="73" rx="10" fill="var(--bmo-body)" stroke="var(--bmo-line)" strokeWidth="2.5"/>
        {/* Screen — y=8, height=59 → ends at y=67, gap top=8, gap bottom=75-67=8 */}
        <rect x="10" y="8" width="100" height="59" rx="7" fill="var(--bmo-face)" opacity="0.6"/>
        {/* Eyes */}
        <circle cx="42" cy="32" r="7" fill="var(--bmo-line)"/>
        <circle cx="78" cy="32" r="7" fill="var(--bmo-line)"/>
        {/* Mouth */}
        {mood === "happy"   && <path d="M38 50 Q60 62 82 50" fill="none" stroke="var(--bmo-line)" strokeWidth="3" strokeLinecap="round"/>}
        {mood === "sad"     && <path d="M38 58 Q60 48 82 58" fill="none" stroke="var(--bmo-line)" strokeWidth="3" strokeLinecap="round"/>}
        {mood === "neutral" && <path d="M38 54 L82 54"        fill="none" stroke="var(--bmo-line)" strokeWidth="3" strokeLinecap="round"/>}
      </svg>
    </div>
  );
}

/* ── Sparkline ──────────────────────────────────────────────── */
function Sparkline({ data, accent, fill, height = 110 }) {
  const W = 320, H = height, P = 6;
  if (!data || data.length < 2) return <svg className="chart-svg" viewBox={`0 0 ${W} ${H}`}/>;
  const min = Math.min(...data), max = Math.max(...data), range = max - min || 1;
  const pts = data.map((v, i) => [
    P + (i / (data.length - 1)) * (W - 2*P),
    P + (1 - (v - min) / range) * (H - 2*P),
  ]);
  const line = pts.map((p, i) => (i===0?"M":"L")+p.join(" ")).join(" ");
  const area = `${line} L${pts[pts.length-1][0]} ${H} L${pts[0][0]} ${H} Z`;
  return (
    <svg className="chart-svg" viewBox={`0 0 ${W} ${H}`} preserveAspectRatio="none">
      <path d={area} fill={fill} opacity="0.9"/>
      <path d={line} fill="none" stroke={accent} strokeWidth="2" strokeLinejoin="round" strokeLinecap="round"/>
    </svg>
  );
}

/* ── Helpers ────────────────────────────────────────────────── */
function timeAgo(ts) {
  if (!ts) return "just now";
  const d = (Date.now()-ts)/1000;
  if (d < 60) return "just now";
  if (d < 3600) return `${Math.floor(d/60)} min ago`;
  if (d < 86400) return `${Math.floor(d/3600)} h ago`;
  return `${Math.floor(d/86400)} d ago`;
}

function getNotifications(sensors, thresholds, reservoirEmpty) {
  if (!sensors) return [];
  const n = [];
  if (reservoirEmpty)
    n.push({ id:"reservoir", msg:"Reservoir may be empty",   type:"alert", icon:"alert" });
  if (sensors.soil_moisture < thresholds.moistureMin)
    n.push({ id:"dry",       msg:"Soil moisture is too low", type:"warn",  icon:"drop"  });
  if (sensors.temperature > thresholds.tempMax)
    n.push({ id:"hot",       msg:"Temperature is too high",  type:"alert", icon:"therm" });
  if (sensors.temperature < thresholds.tempMin)
    n.push({ id:"cold",      msg:"Temperature is too low",   type:"warn",  icon:"therm" });
  if (sensors.light < thresholds.lightMin)
    n.push({ id:"light",     msg:"Light level is too low",   type:"warn",  icon:"sun"   });
  return n;
}

/* ── Grow light ─────────────────────────────────────────────── */
const LIGHT_STATE_LABEL = {
  on:      "Lamp on",
  off:     "Lamp off",
  no_time: "No clock — lamp held off",
};

// `ambient` is the room measured with the lamp dropped; `effective` is what the
// plant is receiving, lamp included. The board measures both every cycle. They
// are equal whenever the lamp is off, so the room figure is only worth showing
// when the lamp is actually adding something.
function LightControl({ mode, state, onSetMode, ambient, effective, compact = false }) {
  const lampAdds = ambient != null && effective != null && effective - ambient > 1;
  return (
    <div className={"light-control" + (compact ? " light-control--compact" : "")}>
      <div className="light-head">
        {I.bulb}
        <span className={"light-state" + (state === "no_time" ? " warn" : "")}>
          {LIGHT_STATE_LABEL[state] ?? "Lamp state unknown"}
        </span>
      </div>
      {effective != null && (
        <div className="light-note">
          {Math.round(effective)} lx at the plant
          {lampAdds && ` — the room alone is ${Math.round(ambient)} lx`}
        </div>
      )}
      <div className="light-modes">
        {[["auto", "Auto"], ["on", "On"], ["off", "Off"]].map(([val, lbl]) => (
          <button
            key={val}
            className={"light-mode-pill" + (mode === val ? " active" : "")}
            onClick={() => onSetMode(val)}
          >{lbl}</button>
        ))}
      </div>
      {mode === "on" && (
        <div className="light-note">Reverts to Auto after 2 hours.</div>
      )}
      {mode === "auto" && (
        <div className="light-note">06:00–22:00, unless the room is already bright.</div>
      )}
    </div>
  );
}

/* ── Sensor cell ────────────────────────────────────────────── */
function Sensor({ icon, label, value, unit, decimals = 0 }) {
  return (
    <div className="sensor">
      <div className="sensor-label">{icon}<span>{label}</span></div>
      <div><span className="sensor-value">{(value??0).toFixed(decimals)}</span><span className="sensor-unit"> {unit}</span></div>
    </div>
  );
}

/* ── ML prediction card ────────────────────────────────────── */
function MLPredictionCard({ prediction, error, loading }) {
  const health = prediction?.health ?? null;
  const isHealthy = health ? /healthy/i.test(health) : null;

  let statusText = "—";
  if (error) statusText = "Unavailable";
  else if (health) statusText = health;
  else if (loading) statusText = "Predicting…";

  return (
    <div className="card ml-card">
      <div className="status-head">
        <div>
          <div className="status-label">ML prediction</div>
          <div className={"status-value" + (error || isHealthy === false ? " warn" : "")}>
            {statusText}
          </div>
        </div>
        <span className="ml-card-icon">{I.chip}</span>
      </div>
      <div className="ml-card-caption">
        {error
          ? "Couldn't reach the prediction backend."
          : "Random-forest model reading live sensor data."}
      </div>
    </div>
  );
}

/* ── Chart modal ────────────────────────────────────────────── */
function ChartModal({ chart, onClose }) {
  useEffect(() => {
    const fn = e => e.key === "Escape" && onClose();
    window.addEventListener("keydown", fn);
    return () => window.removeEventListener("keydown", fn);
  }, [onClose]);
  return (
    <div className="modal-overlay" onClick={onClose}>
      <div className="modal-box" onClick={e => e.stopPropagation()}>
        <div className="modal-head">
          <span className="chart-title">{chart.label}</span>
          <button className="modal-close" onClick={onClose}>{I.close}</button>
        </div>
        <div className="modal-chart">
          <Sparkline data={chart.data} accent={chart.accent} fill={chart.fill} height={340}/>
        </div>
        {chart.stats && (
          <div className="history-stats">
            {[["Min",chart.stats.min],["Avg",chart.stats.avg],["Max",chart.stats.max]].map(([l,v]) => (
              <div key={l}>
                <span className="history-stat-label">{l}</span>
                <span className="history-stat-value">{v.toFixed(chart.decimals)}{chart.unit}</span>
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   App
   ══════════════════════════════════════════════════════════════ */
export default function App() {
  const [theme, setTheme] = useState(() =>
    localStorage.getItem("pm.theme") ||
    (window.matchMedia?.("(prefers-color-scheme: dark)").matches ? "dark" : "light")
  );
  const [isMobile,  setIsMobile]  = useState(() => window.matchMedia("(max-width: 720px)").matches);
  const [sensors,   setSensors]   = useState(null);
  const [lastUpdate,setLastUpdate]= useState(null);
  const [memHistory,setMemHistory]= useState({ moisture:[], temperature:[], humidity:[], light:[], pressure:[], vpd:[] });
  // What the lamp is doing, straight from the board. `mode` is intent, `state`
  // is the relay's actual position -- see LightControl.
  const [light,setLight]          = useState({ mode:"auto", state:null });
  const [history,   setHistory]   = useState(null);
  const [historyError, setHistoryError] = useState(null);
  const [activePlant,   setActivePlant]   = useState(PLANTS[0].id);
  const [activeNav,     setActiveNav]     = useState("overview");
  const [plantSettings, setPlantSettings] = useState(loadPlantSettings);
  const [reservoirEmpty,setReservoirEmpty]= useState(false);
  const [enlargedChart, setEnlargedChart] = useState(null); // key string or null
  const [plantStatus,        setPlantStatus]        = useState(null); // { sensors, health, pump }
  const [plantStatusError,   setPlantStatusError]   = useState(null);
  const [plantStatusLoading, setPlantStatusLoading] = useState(true);
  const moistureBefore = useRef(null);
  const waterPending   = useRef(false);

  const ps = getPS(plantSettings, activePlant);
  const activePlantLabel = ps.name || PLANTS.find(p=>p.id===activePlant)?.defaultLabel || "Plant";

  const updatePS = (key, value) => {
    setPlantSettings(prev => {
      const next = { ...prev, [activePlant]: { ...getPS(prev, activePlant), [key]: value } };
      localStorage.setItem("pm.plantSettings", JSON.stringify(next));
      return next;
    });
  };

  useEffect(() => {
    document.documentElement.dataset.theme = theme;
    localStorage.setItem("pm.theme", theme);
  }, [theme]);

  useEffect(() => {
    const mq = window.matchMedia("(max-width: 720px)");
    const fn = e => setIsMobile(e.matches);
    mq.addEventListener("change", fn);
    return () => mq.removeEventListener("change", fn);
  }, []);

  // The board owns the lamp; this only reads what it is doing and writes what
  // the person wants. Unlike the pump, no backend sits in between.
  useEffect(() => {
    return onValue(ref(database, "light"), snap => {
      const v = snap.val() ?? {};
      setLight({ mode: v.mode ?? "auto", state: v.state ?? null });
    });
  }, []);

  useEffect(() => {
    return onValue(ref(database, "sensors"), snap => {
      const v = snap.val(); if (!v) return;
      setSensors(v);
      setLastUpdate(Date.now());
      setMemHistory(h => ({
        moisture:    [...h.moisture,    v.soil_moisture??0].slice(-96),
        temperature: [...h.temperature, v.temperature??0 ].slice(-96),
        humidity:    [...h.humidity,    v.humidity??0     ].slice(-96),
        light:       [...h.light,       v.light??0        ].slice(-96),
        pressure:    [...h.pressure,    v.pressure??0     ].slice(-96),
        vpd:         [...h.vpd,         calcVPD(v.temperature??20, v.humidity??50)].slice(-96),
      }));
      if (waterPending.current && moistureBefore.current !== null) {
        if (v.soil_moisture > moistureBefore.current + 3) {
          setReservoirEmpty(false); waterPending.current = false;
        } else { setReservoirEmpty(true); }
      }
    });
  }, []);

  // Poll the FastAPI backend for stored history. It reads the same Firebase
  // records, but keeps the query/ordering logic on the backend.
  useEffect(() => {
    let cancelled = false;

    const loadHistory = async () => {
      try {
        const records = await fetchPlantHistory(activePlant, getHistoryFetchLimit(ps.timeframe));
        if (cancelled) return;
        setHistory(records);
        setHistoryError(null);
      } catch (err) {
        if (cancelled) return;
        setHistoryError(err.message || "Could not load history");
      }
    };

    loadHistory();
    const interval = setInterval(loadHistory, BACKEND_POLL_MS);
    return () => { cancelled = true; clearInterval(interval); };
  }, [activePlant, ps.timeframe]);

  // Poll the FastAPI backend for the ML health prediction (and pump/sensor snapshot).
  useEffect(() => {
    let cancelled = false;

    const loadPlantStatus = async () => {
      try {
        const data = await fetchPlantStatus();
        if (cancelled) return;
        setPlantStatus(data);
        setPlantStatusError(null);
      } catch (err) {
        if (cancelled) return;
        setPlantStatusError(err.message || "Could not reach backend");
      } finally {
        if (!cancelled) setPlantStatusLoading(false);
      }
    };

    loadPlantStatus();
    const interval = setInterval(loadPlantStatus, BACKEND_POLL_MS);
    return () => { cancelled = true; clearInterval(interval); };
  }, []);

  const notifications = useMemo(() =>
    getNotifications(sensors, ps.thresholds, reservoirEmpty),
    [sensors, ps.thresholds, reservoirEmpty]
  );

  // The board publishes `state` alongside its readings, so the face here is the
  // same verdict the OLED is showing rather than a second opinion derived from
  // the same numbers. Falls back to the local alert rules when the field is
  // absent -- older firmware, or a reading written before this existed.
  const mood = useMemo(() => {
    const fromBoard = BOARD_STATE_MOOD[sensors?.state];
    if (fromBoard) return fromBoard;
    if (notifications.length === 0) return "happy";
    return notifications.some(n => n.type === "alert") ? "sad" : "neutral";
  }, [sensors?.state, notifications]);

  // One headline for every place that shows plant status.
  const statusHeadline = mood === "happy" ? "Healthy" : "Needs attention";

  const triggerPump = async () => {
    moistureBefore.current = sensors?.soil_moisture ?? null;
    waterPending.current = true;
    setReservoirEmpty(false);
    try {
      await triggerPumpBackend(ps.waterDuration);
    } catch (err) {
      console.error("Failed to trigger pump:", err);
      waterPending.current = false;
    }
  };
  // Writes intent only. The board clears a manual "on" itself once it expires,
  // so nothing here has to remember to tidy up -- which is what went wrong with
  // the light/trigger flag this replaced.
  const setLightMode = (mode) => set(ref(database, "light/mode"), mode);

  const goToChart = (key) => { setActiveNav("history"); setEnlargedChart(key); };
  const updated = lastUpdate ? "Updated " + timeAgo(lastUpdate) : "Waiting…";

  const shared = {
    sensors, notifications, mood, statusHeadline, memHistory, history, historyError,
    ps, updatePS, triggerPump, light, setLightMode,
    updated, activePlant, setActivePlant, activePlantLabel,
    theme, setTheme,
    enlargedChart, setEnlargedChart,
    goToChart,
    plantSettings,
    plantStatus, plantStatusError, plantStatusLoading,
  };

  return (
    <div className="app">
      {isMobile
        ? <MobileShell {...shared}/>
        : <DesktopShell {...shared} activeNav={activeNav} setActiveNav={setActiveNav}/>}
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   Desktop shell
   ══════════════════════════════════════════════════════════════ */
function DesktopShell(p) {
  const { activePlant, setActivePlant, theme, setTheme, activeNav, setActiveNav } = p;
  return (
    <div className="shell-desktop">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-mark">{I.leaf}</div>
          <div className="brand-text">
            <div className="brand-title">Plant Monitor</div>
            <div className="brand-sub">Smart plant care system</div>
          </div>
        </div>

        <nav className="nav">
          {[["overview","Overview",I.grid],["history","Historical data",I.chart],["settings","Settings",I.gear]].map(([id,label,icon])=>(
            <button key={id} className={"nav-item"+(activeNav===id?" active":"")} onClick={()=>setActiveNav(id)}>
              {icon}{label}
            </button>
          ))}
        </nav>

        <div className="sidebar-spacer"/>

        <div className="plants-section">
          <div className="plants-label">Plants</div>
          {PLANTS.map(pl => {
            const name = (p.plantSettings?.[pl.id]?.name) || pl.defaultLabel;
            const customColor = p.plantSettings?.[pl.id]?.dotColor;
            return (
              <button key={pl.id} className={"plant-item"+(activePlant===pl.id?" active":"")} onClick={()=>setActivePlant(pl.id)}>
                <span className={"plant-dot"+(customColor ? "" : " "+pl.dot)}
                      style={customColor ? { background: customColor } : {}}/>
                {name}
              </button>
            );
          })}
          <button className="new-plant">{I.plus} New plant preset</button>
        </div>

        <button className="theme-toggle" onClick={()=>setTheme(theme==="dark"?"light":"dark")}>
          {theme==="dark"?I.sunny:I.moon}
          {theme==="dark"?"Light mode":"Dark mode"}
        </button>
      </aside>

      <main className={"main"+(activeNav==="overview"?" main--fit":"")}>
        {activeNav==="overview" && <OverviewTab  {...p}/>}
        {activeNav==="history"  && <HistoryTab   {...p}/>}
        {activeNav==="settings" && <SettingsTab  {...p}/>}
      </main>
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   Overview tab — no scroll, fits viewport
   ══════════════════════════════════════════════════════════════ */
const GRAPH_DEFS = [
  { key:"moisture",    label:"Moisture",    dataKey:"moisture",    accent:"var(--bmo-line)",  fill:"var(--bmo-body)",      unit:"%",   decimals:1 },
  { key:"temperature", label:"Temperature", dataKey:"temperature", accent:"var(--light-fg)",  fill:"var(--light-fill)",    unit:"°C",  decimals:1 },
  { key:"humidity",    label:"Humidity",    dataKey:"humidity",    accent:"var(--water-fg)",  fill:"var(--water-fill)",    unit:"%",   decimals:1 },
  { key:"light",       label:"Light",       dataKey:"light",       accent:"#a36b1a",          fill:"var(--light-fill)",    unit:"lux", decimals:0 },
  { key:"pressure",    label:"Pressure",    dataKey:"pressure",    accent:"#7c4bb5",          fill:"var(--pressure-fill)", unit:"hPa", decimals:0 },
  { key:"vpd",         label:"VPD",         dataKey:"vpd",         accent:"#1a8a7a",          fill:"var(--vpd-fill)",      unit:"kPa", decimals:2 },
];

function OverviewTab(p) {
  const { sensors, notifications, mood, statusHeadline, memHistory, ps, updatePS, triggerPump, light, setLightMode, updated, activePlantLabel, goToChart,
          plantStatus, plantStatusError, plantStatusLoading } = p;
  const activeGraphs = GRAPH_DEFS.filter(g => ps.graphs[g.key]);
  const gridCols = activeGraphs.map(()=>"1fr").join(" ");

  const statusCardRef = useRef(null);
  const [bmoMaxH, setBmoMaxH] = useState(null);
  useEffect(() => {
    if (!statusCardRef.current) return;
    const update = () => { if (statusCardRef.current) setBmoMaxH(statusCardRef.current.offsetHeight); };
    const ro = new ResizeObserver(update);
    ro.observe(statusCardRef.current);
    update();
    return () => ro.disconnect();
  }, []);

  return (
    <div className="overview-wrap">
      <div className="main-header">
        <h1 className="page-title">{activePlantLabel}</h1>
        <span className="live-pill"><span className="live-dot"/> Live</span>
      </div>

      <div className="top-row">
        <div className="card status-card" ref={statusCardRef}>
          <div className="status-head">
            <div>
              <div className="status-label">Plant status</div>
              <div className={"status-value"+(mood!=="happy"?" warn":"")}>
                {statusHeadline}
              </div>
            </div>
            <div className="status-updated">{updated}</div>
          </div>
          <div className="sensor-grid">
            <Sensor icon={I.drop}  label="Moisture"    value={sensors?.soil_moisture} unit="%" decimals={1}/>
            <Sensor icon={I.therm} label="Temperature" value={sensors?.temperature}   unit="°C" decimals={1}/>
            <Sensor icon={I.sun}   label="Light"       value={sensors?.light}         unit="lux"/>
            <Sensor icon={I.wind}  label="Humidity"    value={sensors?.humidity}      unit="%" decimals={1}/>
            <Sensor icon={I.gauge} label="Pressure"    value={sensors?.pressure}      unit="hPa"/>
            <Sensor icon={I.vapor} label="VPD"         value={sensors ? calcVPD(sensors.temperature??20, sensors.humidity??50) : 0} unit="kPa" decimals={2}/>
          </div>
        </div>

        <div className="card bmo-card" style={bmoMaxH ? { maxHeight: bmoMaxH, overflow:'hidden' } : {}}>
          <BMO mood={mood}/>
          <div className={"bmo-status"+(mood!=="happy"?" warn":"")}>
            {mood==="happy" ? "Thriving" : "Needs attention"}
          </div>
          <div className="bmo-alerts">
            {notifications.length===0
              ? <div className="bmo-sub">No issues detected</div>
              : notifications.map(n => (
                <span key={n.id} className={"bmo-alert bmo-alert--"+n.type}>
                  {I[n.icon]}{n.msg}
                </span>
              ))
            }
          </div>
          <div className="bmo-actions">
            <button className="btn-water" onClick={triggerPump}>{I.drop} Water now</button>
          </div>
          <LightControl
            mode={light.mode}
            state={light.state}
            onSetMode={setLightMode}
            ambient={sensors?.light}
            effective={sensors?.light_effective}
          />
        </div>

        <MLPredictionCard prediction={plantStatus} error={plantStatusError} loading={plantStatusLoading}/>
      </div>

      <div className="bottom-row" style={{ gridTemplateColumns: gridCols }}>
        {activeGraphs.map(g => (
          <div key={g.key} className="card chart-card chart-card--clickable" onClick={()=>goToChart(g.key)} title="Click to enlarge">
            <div className="chart-head">
              <div className="chart-title">{g.label}</div>
              {activeGraphs.length < 3 && (
                <div className="chart-pill-row">
                  <div className="chart-pill">Last 24 readings</div>
                  <span className="chart-expand-icon">{I.expand}</span>
                </div>
              )}
            </div>
            <Sparkline data={memHistory[g.dataKey]} accent={g.accent} fill={g.fill}/>
          </div>
        ))}
      </div>
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   History tab — clickable/expandable charts
   ══════════════════════════════════════════════════════════════ */
function HistoryTab(p) {
  const { memHistory, history, historyError, activePlant, activePlantLabel, ps, updatePS, enlargedChart, setEnlargedChart } = p;
  const range = ps.timeframe;

  const normalizedRecords = useMemo(() => normalizeHistoryRecords(history), [history]);

  const filteredRecords = useMemo(
    () => filterRecordsByTimeframe(normalizedRecords, range),
    [normalizedRecords, range]
  );

  const storedData = useMemo(
    () => (filteredRecords.length ? recordsToChartData(filteredRecords) : null),
    [filteredRecords]
  );

  const healthScore = useMemo(
    () => computePlantHealthScore(filteredRecords, ps.thresholds, range),
    [filteredRecords, ps.thresholds, range]
  );

  const trendSummary = useMemo(
    () => computeTrendSummaryCards(filteredRecords, ps.thresholds, range),
    [filteredRecords, ps.thresholds, range]
  );

  const lowScoreWarning = useMemo(
    () => getLowScoreWarning(normalizedRecords, ps.thresholds, healthScore.score ?? -1),
    [normalizedRecords, ps.thresholds, healthScore.score]
  );

  const data = storedData || (() => {
    const n = getMemHistorySliceCount(range);
    return {
      moisture:    memHistory.moisture.slice(-n),
      temperature: memHistory.temperature.slice(-n),
      humidity:    memHistory.humidity.slice(-n),
      light:       memHistory.light.slice(-n),
      pressure:    memHistory.pressure.slice(-n),
      vpd:         memHistory.vpd.slice(-n),
    };
  })();

  const stat = arr => {
    if (!arr.length) return { min:0, max:0, avg:0 };
    return { min:Math.min(...arr), max:Math.max(...arr), avg:arr.reduce((a,b)=>a+b,0)/arr.length };
  };

  const enlarged = enlargedChart ? GRAPH_DEFS.find(g=>g.key===enlargedChart) : null;
  const enlargedData = enlarged ? { ...enlarged, data: data[enlarged.dataKey], stats: stat(data[enlarged.dataKey]) } : null;

  return (
    <div className="history-wrap">
      <div className="main-header">
        <div>
          <div className="status-label">Historical data</div>
          <h1 className="page-title">{activePlantLabel}</h1>
        </div>
        <div className="range-pills">
          {[["1h","1h"],["6h","6h"],["12h","12h"],["24h","24h"],["7d","7d"]].map(([k,l])=>(
            <button key={k} className={"range-pill"+(range===k?" active":"")} onClick={()=>updatePS("timeframe",k)}>{l}</button>
          ))}
        </div>
      </div>

      {historyError && (
        <div className="history-error">
          Couldn't load stored history from the backend — showing live readings only.
        </div>
      )}

      <div className="history-summary-row">
        <div className="card history-trend-card">
            <div className="history-trend-head">
              <div>
                <div className="status-label history-trend-title">Trend Summary</div>
                <div className="history-trend-caption">Comparing the recent half of the {healthScore.windowLabel} with the previous half.</div>
              </div>
            </div>

            {trendSummary.hasEnoughData ? (
              <div className="history-trend-list">
                {trendSummary.cards.map((card) => (
                  <div key={card.key} className="history-trend-item">
                    <div className="history-trend-item-head">
                      <span className="history-trend-item-title">{card.title}</span>
                      <span className={"history-trend-pill history-trend-pill--" + card.direction}>
                        {card.direction[0].toUpperCase() + card.direction.slice(1)}
                      </span>
                    </div>
                    <div className="history-trend-delta">
                      {card.delta > 0 ? "+" : ""}
                      {card.delta.toFixed(card.decimals)}
                      {card.unit}
                    </div>
                    <div className="history-trend-text">{card.interpretation}</div>
                  </div>
                ))}
              </div>
            ) : (
              <div className="history-score-empty">Not enough history to compare recent and previous periods.</div>
            )}
          </div>

        <div className="card history-score-card">
            <div className="history-score-head">
              <div>
                <div className="status-label history-score-title">Plant health score</div>
                <div className="history-score-caption">Based on the {healthScore.windowLabel}</div>
              </div>
              {healthScore.hasEnoughData ? (
                <span className={"history-score-badge history-score-badge--" + healthScore.label.toLowerCase().replace(/\s+/g, "-")}>
                  {healthScore.label}
                </span>
              ) : null}
            </div>

            {healthScore.hasEnoughData ? (
              <>
                <div className={"history-score-main history-score-main--" + healthScore.label.toLowerCase().replace(/\s+/g, "-")}>
                  <div className="history-score-value">{healthScore.score}</div>
                  <div className="history-score-out-of">/ 100</div>
                </div>
                <div className="history-score-breakdown">
                  <div className="history-score-item">
                    <span className="history-score-item-label">Moisture</span>
                    <span className="history-score-item-value">{healthScore.breakdown.moisture}%</span>
                  </div>
                  <div className="history-score-item">
                    <span className="history-score-item-label">Temperature</span>
                    <span className="history-score-item-value">{healthScore.breakdown.temperature}%</span>
                  </div>
                  <div className="history-score-item">
                    <span className="history-score-item-label">Light</span>
                    <span className="history-score-item-value">{healthScore.breakdown.light}%</span>
                  </div>
                </div>
                {lowScoreWarning && <div className="history-score-warning">{lowScoreWarning}</div>}
              </>
            ) : (
              <div className="history-score-empty">Not enough history for this timeframe.</div>
            )}
        </div>
      </div>

      <div className="history-grid">
        {GRAPH_DEFS.map(g => {
          const d = data[g.dataKey];
          const st = stat(d);
          return (
            <div key={g.key} className="card history-card chart-card--clickable" onClick={()=>setEnlargedChart(g.key)} title="Click to enlarge">
              <div className="chart-head">
                <div className="chart-title">{g.label}</div>
                <div className="chart-pill-row">
                  <div className="chart-pill">{d.length} readings</div>
                  <span className="chart-expand-icon">{I.expand}</span>
                </div>
              </div>
              <Sparkline data={d} accent={g.accent} fill={g.fill} height={140}/>
              <div className="history-stats">
                {[["Min",st.min],["Avg",st.avg],["Max",st.max]].map(([l,v])=>(
                  <div key={l}>
                    <span className="history-stat-label">{l}</span>
                    <span className="history-stat-value">{v.toFixed(g.decimals)}{g.unit}</span>
                  </div>
                ))}
              </div>
            </div>
          );
        })}
      </div>

      {enlargedData && (
        <ChartModal chart={enlargedData} onClose={()=>setEnlargedChart(null)}/>
      )}
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   Settings tab
   ══════════════════════════════════════════════════════════════ */
function SettingsTab(p) {
  const { ps, updatePS, theme, setTheme, updated, activePlantLabel, activePlant } = p;
  const t = ps.thresholds;
  const setT = (k,v) => updatePS("thresholds", { ...t, [k]:v });
  const [nameInput, setNameInput] = useState(ps.name || activePlantLabel);

  useEffect(() => { setNameInput(ps.name || activePlantLabel); }, [activePlant]);

  const GRAPH_OPTIONS = [
    { key:"moisture", label:"Moisture" }, { key:"temperature", label:"Temperature" },
    { key:"humidity", label:"Humidity" }, { key:"light",       label:"Light"       },
    { key:"pressure", label:"Pressure" }, { key:"vpd",         label:"VPD"         },
  ];

  const activeGraphCount = Object.values(ps.graphs).filter(Boolean).length;

  const DOT_COLORS = [
    "#4f8a3a","#7cc05a","#de5d92","#e36da2",
    "#2462a8","#7abcf5","#d4620f","#e0b060",
    "#7c4bb5","#1a8a7a",
  ];

  const activeDot = ps.dotColor || null;

  return (
    <>
      <div className="main-header">
        <div>
          <div className="status-label">Preferences</div>
          <h1 className="page-title">Settings</h1>
        </div>
      </div>

      <div className="settings-grid">
        <div className="card settings-card">
          <div className="settings-section-title">Plant preset</div>
          <div className="settings-section-sub">Customize this plant's identity.</div>
          <div className="setting-row">
            <div className="setting-row-head">
              <span className="setting-row-label">Preset name</span>
            </div>
            <input
              className="preset-name-input"
              value={nameInput}
              onChange={e => setNameInput(e.target.value)}
              onBlur={() => updatePS("name", nameInput || null)}
              placeholder="e.g. Basil 1"
            />
          </div>
          <div className="setting-row">
            <div className="setting-row-head">
              <span className="setting-row-label">Dot color</span>
            </div>
            <div className="color-palette">
              {DOT_COLORS.map(hex => (
                <button
                  key={hex}
                  className={"color-swatch" + (activeDot === hex ? " active" : "")}
                  style={{ background: hex }}
                  onClick={() => updatePS("dotColor", activeDot === hex ? null : hex)}
                  title={hex}
                />
              ))}
            </div>
          </div>
          <div className="settings-section-sub" style={{marginTop:8}}>
            Overview graphs
            <span className="graph-count-badge">{activeGraphCount} / 4</span>
          </div>
          <div className="graph-toggles">
            {GRAPH_OPTIONS.map(({key,label})=>{
              const isActive = ps.graphs[key];
              const isDisabled = !isActive && activeGraphCount >= 4;
              return (
                <button key={key}
                  className={"graph-toggle"+(isActive?" active":"")+(isDisabled?" disabled":"")}
                  onClick={()=>{ if (!isDisabled) updatePS("graphs",{...ps.graphs,[key]:!isActive}); }}
                  title={isDisabled ? "Deselect another graph first" : ""}
                >
                  {label}
                </button>
              );
            })}
          </div>
        </div>

        <div className="card settings-card">
          <div className="settings-section-title">Alert thresholds</div>
          <div className="settings-section-sub">BMO alerts when readings cross these.</div>
          <SettingSlider icon={I.drop}  label="Moisture minimum"    suffix="%"   min={0}  max={100} step={1}  value={t.moistureMin} onChange={v=>setT("moistureMin",v)}/>
          <SettingSlider icon={I.therm} label="Temperature minimum" suffix="°C"  min={0}  max={30}  step={1}  value={t.tempMin}     onChange={v=>setT("tempMin",v)}/>
          <SettingSlider icon={I.therm} label="Temperature maximum" suffix="°C"  min={20} max={50}  step={1}  value={t.tempMax}     onChange={v=>setT("tempMax",v)}/>
          <SettingSlider icon={I.sun}   label="Light minimum"       suffix=" lux" min={0} max={2000} step={10} value={t.lightMin}   onChange={v=>setT("lightMin",v)}/>
        </div>

        <div className="card settings-card">
          <div className="settings-section-title">Watering</div>
          <div className="settings-section-sub">
            Duration used when you tap the Water now button.
          </div>
          <div className="duration-pills duration-pills-lg">
            {[1,2,3,4,5].map(d=>(
              <button key={d} className={"duration-pill"+(ps.waterDuration===d?" active":"")} onClick={()=>updatePS("waterDuration",d)}>{d}s</button>
            ))}
          </div>
        </div>

        <div className="card settings-card">
          <div className="settings-section-title">Appearance</div>
          <div className="settings-section-sub">Theme is saved on this device.</div>
          <div className="theme-choices">
            {[["light","Light"],["dark","Dark"]].map(([val,lbl])=>(
              <button key={val} className={"theme-choice"+(theme===val?" active":"")} onClick={()=>setTheme(val)}>
                <div className={"theme-choice-swatch "+val}><div/><div/><div/></div>
                <span>{lbl}</span>
              </button>
            ))}
          </div>
        </div>

        <div className="card settings-card">
          <div className="settings-section-title">Device</div>
          <div className="info-rows">
            <div className="info-row"><span>{I.chip} Hardware</span><span>ESP32-S3</span></div>
            <div className="info-row"><span>{I.wifi} Connection</span><span className="info-good">Online</span></div>
            <div className="info-row"><span>{I.drop} Last reading</span><span>{updated.replace("Updated ","")}</span></div>
          </div>
        </div>
      </div>
    </>
  );
}

function SettingSlider({ icon, label, suffix, min, max, step, value, onChange }) {
  return (
    <div className="setting-row">
      <div className="setting-row-head">
        <span className="setting-row-label">{icon}{label}</span>
        <span className="setting-row-value">{value}{suffix}</span>
      </div>
      <input type="range" min={min} max={max} step={step} value={value}
             onChange={e=>onChange(Number(e.target.value))}
             className="setting-slider"
             style={{["--pct"]:((value-min)/(max-min)*100)+"%"}}/>
    </div>
  );
}

/* ══════════════════════════════════════════════════════════════
   Mobile shell
   ══════════════════════════════════════════════════════════════ */
function MobileShell(p) {
  const { sensors, notifications, mood, statusHeadline, memHistory, ps, triggerPump, light, setLightMode, updated, activePlantLabel, theme, setTheme, goToChart,
          plantStatus, plantStatusError, plantStatusLoading } = p;
  const [tab, setTab] = useState("overview");
  const activeGraphs = GRAPH_DEFS.filter(g => ps.graphs[g.key]);

  const [plantPickerOpen, setPlantPickerOpen] = useState(false);

  return (
    <div className="shell-mobile">
      <div className="mobile-content">
        <div className="mobile-topbar">
          <div className="mobile-brand">{I.leaf} Plant Monitor</div>
          <span className="live-pill"><span className="live-dot"/> Live</span>
        </div>

        {tab==="overview" && (
          <>
            <div className="mobile-title-row">
              <h1 className="mobile-title">{activePlantLabel}</h1>
              <span className="mobile-updated">{updated.replace("Updated ","")}</span>
            </div>
            <div className="card mobile-status-card">
              <BMO mood={mood}/>
              <div className="mobile-status-text">
                <div className={"status-value"+(mood!=="happy"?" warn":"")}>{statusHeadline}</div>
                {notifications.length===0
                  ? <div className="status-updated">All readings normal</div>
                  : notifications.map(n=>(
                    <div key={n.id} className="mobile-notif">{I[n.icon]} {n.msg}</div>
                  ))
                }
              </div>
            </div>
            <div className="mobile-actions">
              <button className="btn-water" onClick={triggerPump}>{I.drop} Water now</button>
            </div>
            <div className="card">
              <LightControl
                mode={light.mode}
                state={light.state}
                onSetMode={setLightMode}
                ambient={sensors?.light}
                effective={sensors?.light_effective}
                compact
              />
            </div>
            <MLPredictionCard prediction={plantStatus} error={plantStatusError} loading={plantStatusLoading}/>
            <div className="mobile-sensor-grid">
              {[{icon:I.drop,label:"Moisture",value:sensors?.soil_moisture,unit:"%"},{icon:I.therm,label:"Temp",value:sensors?.temperature,unit:"°C"},{icon:I.sun,label:"Light",value:sensors?.light,unit:"lx"},{icon:I.wind,label:"Humidity",value:sensors?.humidity,unit:"%"}].map(s=>(
                <div key={s.label} className="mobile-sensor sensor">
                  <div className="sensor-label">{s.icon} {s.label}</div>
                  <div><span className="sensor-value">{(s.value??0).toFixed(0)}</span><span className="sensor-unit"> {s.unit}</span></div>
                </div>
              ))}
            </div>
            {activeGraphs.map(g => (
              <div key={g.key} className="card chart-card mobile-chart-card chart-card--clickable" onClick={()=>{goToChart(g.key);setTab("history");}}>
                <div className="chart-head">
                  <div className="chart-title">{g.label}</div>
                  <div className="chart-pill">Last 24</div>
                </div>
                <Sparkline data={memHistory[g.dataKey]} accent={g.accent} fill={g.fill}/>
              </div>
            ))}
          </>
        )}
        {tab==="history"  && <HistoryTab  {...p}/>}
        {tab==="settings" && <SettingsTab {...p}/>}
      </div>

      {plantPickerOpen && (
        <div className="plant-picker-overlay" onClick={()=>setPlantPickerOpen(false)}>
          <div className="plant-picker-sheet" onClick={e=>e.stopPropagation()}>
            <div className="plant-picker-title">Switch plant</div>
            {PLANTS.map(pl => {
              const name = p.plantSettings?.[pl.id]?.name || pl.defaultLabel;
              const color = p.plantSettings?.[pl.id]?.dotColor ||
                (pl.dot==="green" ? "var(--green-dot)" : "var(--pink-dot)");
              const isActive = p.activePlant === pl.id;
              return (
                <button key={pl.id} className={"plant-picker-item"+(isActive?" active":"")}
                  onClick={()=>{ p.setActivePlant(pl.id); setPlantPickerOpen(false); }}>
                  <span className="mobile-plant-dot" style={{ background: color }}/>
                  {name}
                  {isActive && <span className="plant-picker-check">✓</span>}
                </button>
              );
            })}
          </div>
        </div>
      )}

      <nav className="mobile-tabbar">
        {[["overview","Overview",I.grid],["history","History",I.chart],["settings","Settings",I.gear]].map(([id,lbl,icon])=>(
          <button key={id} className={"mobile-tab"+(tab===id?" active":"")} onClick={()=>setTab(id)}>{icon}{lbl}</button>
        ))}
        <button className={"mobile-tab mobile-tab--plant"+(plantPickerOpen?" active":"")}
          onClick={()=>setPlantPickerOpen(o=>!o)}>
          <span className="mobile-plant-dot" style={{
            background: p.plantSettings?.[p.activePlant]?.dotColor ||
              (PLANTS.find(pl=>pl.id===p.activePlant)?.dot==="green" ? "var(--green-dot)" : "var(--pink-dot)")
          }}/>
          {p.activePlantLabel}
        </button>
      </nav>
    </div>
  );
}
