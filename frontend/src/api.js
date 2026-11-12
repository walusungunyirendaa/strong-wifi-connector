const API_BASE = "";

async function request(path, options = {}) {
  const response = await fetch(`${API_BASE}${path}`, {
    cache: "no-store",
    ...options,
  });

  const text = await response.text();
  let data = {};
  try {
    data = text ? JSON.parse(text) : {};
  } catch {
    data = { error: text || `HTTP ${response.status}` };
  }

  if (!response.ok && response.status !== 202) {
    throw new Error(data.error || `Request failed (HTTP ${response.status})`);
  }

  return { response, data };
}

export async function getStatus() {
  return (await request("/api/status")).data;
}

export async function scanNetworks() {
  return (await request("/api/scan")).data;
}

export async function getSavedNetworks() {
  return (await request("/api/networks")).data;
}

export async function saveNetwork(ssid, password) {
  return (await request("/save", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ ssid, password }),
  })).data;
}

export async function deleteNetwork(index) {
  return (await request("/delete", {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body: new URLSearchParams({ index: String(index) }),
  })).data;
}

export async function resetCredentials() {
  return (await request("/reset", { method: "POST" })).data;
}
