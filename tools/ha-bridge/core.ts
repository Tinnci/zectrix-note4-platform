import { Database } from "bun:sqlite";

export interface HAState { entity_id: string; state: string; attributes: { friendly_name?: string; unit_of_measurement?: string } }
export interface DeviceState { battery: number | null; voltage: number | null; charging: boolean | null; interval_seconds: number; offered_revision: number; sampled_at: string }

export async function readStates(base: string, token: string, entities: string[], allowHttp = false, budgetMs = 3000): Promise<HAState[]> {
  const origin = new URL(base);
  if ((origin.protocol !== "https:" && !(allowHttp && origin.protocol === "http:")) || origin.username || origin.password || origin.search || origin.hash || origin.pathname !== "/") throw new Error("Invalid HA origin");
  if (!token || !entities.length || entities.length > 8 || entities.some(id => !/^[a-z_]+\.[a-z0-9_]+$/.test(id))) throw new Error("Invalid HA source");
  const signal = AbortSignal.timeout(budgetMs);
  return Promise.all(entities.map(async id => {
    const response = await fetch(new URL(`/api/states/${id}`, origin), { headers: { Authorization: `Bearer ${token}` }, redirect: "error", signal });
    if (!response.ok || !response.body) throw new Error("HA state unavailable");
    const reader = response.body.getReader(); const chunks: Uint8Array[] = []; let bytes = 0;
    try {
      while (true) { const { value, done } = await reader.read(); if (done) break;
        bytes += value.length; if (bytes > 16384) throw new Error("HA state too large"); chunks.push(value); }
    } finally { await reader.cancel(); }
    const body = new Uint8Array(bytes); let offset = 0; for (const chunk of chunks) { body.set(chunk, offset); offset += chunk.length; }
    const state = JSON.parse(new TextDecoder().decode(body)) as HAState;
    if (state.entity_id !== id || typeof state.state !== "string" || state.state.length > 256 || !state.attributes || typeof state.attributes !== "object") throw new Error("Invalid HA state");
    const { friendly_name, unit_of_measurement } = state.attributes;
    return { entity_id: id, state: state.state, attributes: {
      friendly_name: typeof friendly_name === "string" ? friendly_name.slice(0, 64) : id,
      unit_of_measurement: typeof unit_of_measurement === "string" ? unit_of_measurement.slice(0, 16) : "",
    } };
  }));
}

export function deviceState(request: Request, page: Uint8Array, now = new Date()): DeviceState {
  const number = (name: string, max: number) => { const raw = request.headers.get(name); if (!raw || !/^\d{1,5}$/.test(raw)) return null; const value = Number(raw); return value <= max ? value : null; };
  const mv = number("X-Note4-Millivolts", 65535), charging = request.headers.get("X-Note4-Charging");
  const interval = number("X-Note4-Interval",86400);
  return { battery: number("X-Note4-Battery", 100), voltage: mv === null ? null : mv / 1000,
    charging: charging === "1" ? true : charging === "0" ? false : null,
    interval_seconds: interval !== null && interval >= 300 ? interval : 21600,
    offered_revision: new DataView(page.buffer, page.byteOffset, page.byteLength).getUint32(8, true), sampled_at: now.toISOString() };
}

export function discovery(id: string, expirySeconds = 43260) {
  if (!/^[a-z0-9_-]{1,32}$/.test(id) || !Number.isInteger(expirySeconds) || expirySeconds < 600 || expirySeconds > 604800) throw new Error("Invalid discovery settings");
  const state = `note4/note4/${id}/state`, device = { identifiers: [`note4_note4_${id}`], name: `Note4 ${id}`, manufacturer: "Note4", model: "Note4" };
  const fields = [
    ["sensor", "battery", "Battery", "battery", "%"],
    ["sensor", "voltage", "Battery voltage", "voltage", "V"],
    ["binary_sensor", "charging", "Charging", "battery_charging", ""],
    ["sensor", "offered_revision", "Offered page revision", "", ""],
  ];
  return fields.map(([component, key, name, device_class, unit]) => ({
    topic: `homeassistant/${component}/note4_note4_${id}/${key}/config`,
    payload: JSON.stringify({ name, unique_id: `note4_note4_${id}_${key}`, device,
      state_topic: state, expire_after: expirySeconds,
      value_template: key === "charging" ? "{{ 'ON' if value_json.charging == true else 'OFF' }}" : `{{ value_json.${key} | default(none) }}`,
      availability: [{ topic: state, value_template: `{{ 'online' if value_json.${key} != none and as_timestamp(now()) - as_timestamp(value_json.sampled_at, 0) < ${expirySeconds} else 'offline' }}` }],
      ...(device_class ? { device_class } : {}), ...(unit ? { unit_of_measurement: unit } : {}),
      ...(component === "sensor" && unit ? { state_class: "measurement" } : {}),
      ...(key === "offered_revision" ? { entity_category: "diagnostic" } : {}),
    }),
  }));
}

// A transaction, not a hash: revisions survive restart and wall-clock rollback.
export function revisionCounter(db: Database) {
  db.run("CREATE TABLE IF NOT EXISTS page_revision (id INTEGER PRIMARY KEY CHECK(id=1), value INTEGER NOT NULL)");
  return db.transaction((issued: number) => {
    const previous = db.query<{ value: number }, []>("SELECT value FROM page_revision WHERE id=1").get()?.value ?? 0;
    const value = Math.max(issued, previous + 1);
    if (!Number.isInteger(value) || value < 1 || value > 0xffffffff) throw new Error("Revision exhausted");
    db.query("INSERT INTO page_revision VALUES (1, ?) ON CONFLICT(id) DO UPDATE SET value=excluded.value").run(value);
    return value;
  });
}
