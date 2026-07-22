(() => {
  let csrfToken = "";
  let pollTimer = null;
  const byId = (id) => document.getElementById(id);
  const alertBox = byId("alert");

  class OcAlert extends HTMLElement {
    show(message) {
      this.hidden = !message;
      this.textContent = message || "";
    }
  }
  customElements.define("oc-alert", OcAlert);

  async function api(path, options = {}) {
    const headers = { "Content-Type": "application/json", ...(options.headers || {}) };
    if (csrfToken && options.method && options.method !== "GET") headers["X-OC-CSRF"] = csrfToken;
    const response = await fetch(path, { credentials: "same-origin", headers, ...options });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(data.error || "request_failed");
    return data;
  }

  function showError(error) {
    alertBox.show(error?.message || error || "");
  }

  async function login() {
    try {
      const result = await api("/api/v1/auth/login", {
        method: "POST",
        body: JSON.stringify({ password: byId("loginPassword").value }),
      });
      csrfToken = result.csrfToken;
      byId("loginPanel").hidden = true;
      byId("dashboard").hidden = false;
      await loadDashboard();
      pollTimer = setInterval(loadState, 2000);
    } catch (error) {
      showError(error);
    }
  }

  async function loadDashboard() {
    const [device, connectors] = await Promise.all([
      api("/api/v1/device"),
      api("/api/v1/connectors"),
    ]);
    byId("deviceName").textContent = device.friendlyName;
    byId("friendlyName").value = device.friendlyName;
    byId("mqttEnabled").checked = connectors.mqttEnabled;
    byId("mqttHost").value = connectors.mqttHost || "";
    byId("mqttPort").value = connectors.mqttPort;
    byId("mqttTls").checked = connectors.mqttTls;
    byId("matterField").hidden = !connectors.matterSupported;
    byId("matterEnabled").checked = connectors.matterEnabled;
    byId("commissionMatter").hidden = !connectors.matterSupported || !connectors.matterEnabled;
    await loadState();
  }

  async function loadState() {
    try {
      const state = await api("/api/v1/state");
      byId("status").textContent = state.reported.valid
        ? `Room ${state.reported.returnAirC.toFixed(1)} °C · reported setpoint ${state.reported.setTempC.toFixed(1)} °C`
        : state.available ? "Connected; waiting for reported state" : "Device unavailable";
      byId("powerOn").checked = state.desired.powerOn;
      byId("mode").value = state.desired.mode;
      byId("targetTempC").value = state.desired.targetTempC;
      byId("targetTempC").min = state.capabilities.minTempC;
      byId("targetTempC").max = state.capabilities.maxTempC;
      byId("fan").value = state.desired.fan;
    } catch (error) {
      if (pollTimer) clearInterval(pollTimer);
      showError(error);
    }
  }

  async function applyState() {
    try {
      await api("/api/v1/state", {
        method: "PUT",
        body: JSON.stringify({
          powerOn: byId("powerOn").checked,
          mode: byId("mode").value,
          targetTempC: Number(byId("targetTempC").value),
          fan: byId("fan").value,
        }),
      });
      alertBox.show("");
      setTimeout(loadState, 250);
    } catch (error) {
      showError(error);
    }
  }

  async function saveConnectors() {
    try {
      const payload = {
        friendlyName: byId("friendlyName").value,
        mqttEnabled: byId("mqttEnabled").checked,
        mqttHost: byId("mqttHost").value,
        mqttPort: Number(byId("mqttPort").value),
        mqttTls: byId("mqttTls").checked,
        matterEnabled: byId("matterEnabled").checked,
      };
      if (byId("mqttUsername").value) payload.mqttUsername = byId("mqttUsername").value;
      if (byId("mqttPassword").value) payload.mqttPassword = byId("mqttPassword").value;
      if (byId("adminPassword").value) payload.adminPassword = byId("adminPassword").value;
      await api("/api/v1/connectors", { method: "PUT", body: JSON.stringify(payload) });
      byId("subtitle").textContent = "Settings saved. The device is rebooting.";
    } catch (error) {
      showError(error);
    }
  }

  async function commissionMatter() {
    try {
      await api("/api/v1/matter/commissioning-window", { method: "POST", body: "{}" });
      byId("subtitle").textContent = "Matter commissioning is open.";
    } catch (error) {
      showError(error);
    }
  }

  async function saveWifi() {
    try {
      await api("/api/v1/wifi", {
        method: "PUT",
        body: JSON.stringify({ ssid: byId("wifiSsid").value, password: byId("wifiPassword").value }),
      });
      byId("subtitle").textContent = "Wi‑Fi saved. Reconnect after the device reboots.";
    } catch (error) {
      showError(error);
    }
  }

  byId("loginButton").addEventListener("click", login);
  byId("loginPassword").addEventListener("keydown", (event) => {
    if (event.key === "Enter") login();
  });
  byId("applyState").addEventListener("click", applyState);
  byId("saveConnectors").addEventListener("click", saveConnectors);
  byId("commissionMatter").addEventListener("click", commissionMatter);
  byId("saveWifi").addEventListener("click", saveWifi);
})();
