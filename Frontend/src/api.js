const API_BASE_URL = import.meta.env.VITE_API_BASE_URL || "http://localhost:8000";

async function request(path, options) {
  const res = await fetch(`${API_BASE_URL}${path}`, options);
  if (!res.ok) {
    let detail = res.statusText;
    try { detail = (await res.json()).detail ?? detail; } catch { /* ignore */ }
    throw new Error(detail);
  }
  return res.json();
}

// GET /plant -> { sensors, health, pump: { trigger, duration } }
export function fetchPlantStatus() {
  return request("/plant");
}

// GET /plant/history?plant_id=basil-1&n=100
export function fetchPlantHistory(plantId, n = 100) {
  return request(
    `/plant/history?plant_id=${encodeURIComponent(plantId)}&n=${n}`
  );
}

// GET /plant/light?plant_id=basil-1
//   -> { dli, hours_covered, samples, target, floor }
// The day's light added up, not the current brightness. Computed by the
// backend from stored history -- the board does not track it.
export function fetchDailyLight(plantId) {
  return request(`/plant/light?plant_id=${encodeURIComponent(plantId)}`);
}

// POST /pump -> { success, duration }
export function triggerPumpBackend(duration) {
  return request("/pump", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(duration != null ? { duration } : {}),
  });
}