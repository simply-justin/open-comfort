(() => {
  // Builds DOM nodes; text always goes through textContent.
  function h(tag, props = {}, ...children) {
    const el = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null || value === false) continue;
      if (key.startsWith("on")) el.addEventListener(key.slice(2), value);
      else if (key === "html") el.innerHTML = value; // static icon markup only
      else el.setAttribute(key, value === true ? "" : value);
    }
    for (const child of children.flat(Infinity)) {
      if (child === null || child === undefined || child === false) continue;
      el.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return el;
  }

  const ICON_POWER = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" aria-hidden="true"><path d="M12 3v8"/><path d="M6.3 6.8a8 8 0 1 0 11.4 0"/></svg>';
  const ICON_SETTINGS = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" aria-hidden="true"><path d="M4 8h9M17 8h3M4 16h3M11 16h9"/><circle cx="15" cy="8" r="2"/><circle cx="9" cy="16" r="2"/></svg>';
  const ICON_BACK = '<svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M15 5l-7 7 7 7"/></svg>';
  const ICON_MINUS = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" aria-hidden="true"><path d="M5 12h14"/></svg>';
  const ICON_PLUS = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" aria-hidden="true"><path d="M5 12h14M12 5v14"/></svg>';

  const MESSAGES = {
    invalid_credentials: "That password is not right.",
    invalid_admin_password: "Use 8 to 64 characters for the portal password.",
    invalid_connector_config: "Check the MQTT broker and port.",
    invalid_state: "The device did not accept that setting.",
    ssid_required: "Enter a network name.",
    commissioning_unavailable: "Matter is not running yet. Try again in a moment.",
    request_failed: "The device did not answer.",
  };

  const MODES = [["auto", "Auto"], ["cool", "Cool"], ["heat", "Heat"], ["dry", "Dry"], ["fan", "Fan"]];
  const FANS = [["auto", "Auto"], ["low", "Low"], ["medium", "Medium"], ["high", "High"]];
  const STATUS = { cool: "Cooling to", heat: "Heating to", dry: "Drying at", fan: "Fan only", auto: "Keeping" };

  const CSRF_KEY = "oc.csrf";
  function storedCsrf() {
    try { return sessionStorage.getItem(CSRF_KEY) || ""; } catch { return ""; }
  }
  function storeCsrf(token) {
    try { token ? sessionStorage.setItem(CSRF_KEY, token) : sessionStorage.removeItem(CSRF_KEY); } catch { /* private mode */ }
  }

  const app = document.getElementById("app");
  const ui = {
    view: "login",
    csrf: storedCsrf(),
    error: "",
    notice: "",
    busy: false,
    device: null,
    connectors: null,
    state: null,
    desired: null,
    editedAt: 0,
    pollTimer: null,
    sendTimer: null,
    form: {},
  };

  async function api(path, options = {}) {
    const headers = { "Content-Type": "application/json" };
    if (ui.csrf && options.method && options.method !== "GET") headers["X-OC-CSRF"] = ui.csrf;
    const response = await fetch(path, { credentials: "same-origin", headers, ...options });
    const data = await response.json().catch(() => ({}));
    if (response.status === 401 || response.status === 403) {
      if (path !== "/api/v1/auth/login") signedOut();
    }
    if (!response.ok) throw new Error(MESSAGES[data.error] || data.error || MESSAGES.request_failed);
    return data;
  }

  function signedOut() {
    ui.csrf = "";
    storeCsrf("");
    clearInterval(ui.pollTimer);
    ui.view = "login";
    render();
  }

  function render() {
    const focused = document.activeElement?.id;
    const views = { login: viewLogin, control: viewControl, settings: viewSettings, rebooting: viewRebooting };
    app.className = ui.view === "login" || ui.view === "rebooting" ? "shell" : "shell shell--wide";
    app.replaceChildren(...[views[ui.view]()].flat(Infinity).filter(Boolean));
    if (focused) document.getElementById(focused)?.focus();
  }

  function messages() {
    return [
      ui.error ? h("p", { class: "alert", role: "alert" }, ui.error) : null,
      ui.notice ? h("p", { class: "alert alert--ok", role: "status" }, ui.notice) : null,
    ];
  }

  // Sign in
  function viewLogin() {
    return [
      h("p", { class: "eyebrow" }, "OpenComfort"),
      h("h1", {}, "Sign in"),
      h("form", { style: "display:contents", onsubmit: (e) => { e.preventDefault(); login(); } },
        h("h2", { class: "label" }, "Portal password"),
        h("div", { class: "list" },
          h("label", { class: "row" },
            h("input", { id: "password", type: "password", autocomplete: "current-password", "aria-label": "Portal password", required: true }))),
        h("p", { class: "note" }, "Set during setup. Forgot it? Hold the reset button for 5 seconds to start over."),
        ...messages(),
        h("div", { class: "actions" }, h("button", { class: "button", type: "submit", disabled: ui.busy }, ui.busy ? "Signing in…" : "Sign in"))),
    ];
  }

  async function login() {
    ui.busy = true;
    ui.error = "";
    const password = document.getElementById("password").value;
    render();
    try {
      const result = await api("/api/v1/auth/login", { method: "POST", body: JSON.stringify({ password }) });
      ui.csrf = result.csrfToken;
      storeCsrf(ui.csrf);
      await openDashboard();
    } catch (error) {
      ui.error = error.message;
    }
    ui.busy = false;
    render();
  }

  async function openDashboard() {
    const [device, connectors] = await Promise.all([api("/api/v1/device"), api("/api/v1/connectors")]);
    ui.device = device;
    ui.connectors = connectors;
    ui.view = "control";
    document.title = device.friendlyName;
    await refresh();
    clearInterval(ui.pollTimer);
    ui.pollTimer = setInterval(refresh, 2000);
  }

  async function refresh() {
    try {
      ui.state = await api("/api/v1/state");
      // Don't let a poll overwrite what the user is changing right now.
      if (Date.now() - ui.editedAt < 2500) return;
      ui.desired = { ...ui.state.desired };
      if (ui.view === "control") render();
    } catch (error) {
      ui.error = error.message;
      if (ui.view === "control") render();
    }
  }

  // Control
  function change(patch, delay = 0, rerender = true) {
    ui.desired = { ...ui.desired, ...patch };
    ui.editedAt = Date.now();
    if (rerender) render();
    clearTimeout(ui.sendTimer);
    ui.sendTimer = setTimeout(sendDesired, delay);
  }

  async function sendDesired() {
    try {
      await api("/api/v1/state", { method: "PUT", body: JSON.stringify(ui.desired) });
      ui.error = "";
    } catch (error) {
      ui.error = error.message;
      render();
    }
  }

  function segments(label, options, current, onPick) {
    return h("div", { class: "segments", role: "group", "aria-label": label },
      options.map(([value, text]) =>
        h("button", { type: "button", "aria-pressed": String(value === current), onclick: () => onPick(value) }, text)));
  }

  function viewControl() {
    const s = ui.state;
    const d = ui.desired;
    if (!s || !d) return [h("p", { class: "eyebrow" }, "Loading…")];
    const min = s.capabilities.minTempC;
    const max = s.capabilities.maxTempC;
    const on = d.powerOn && d.mode !== "off";
    const set = Math.round(d.targetTempC);
    const pct = (t) => `${Math.min(100, Math.max(0, ((t - min) / (max - min)) * 100)).toFixed(1)}%`;
    const room = s.reported.valid ? s.reported.returnAirC : null;
    const scale = [];
    for (let t = min; t <= max; t += 2) scale.push(h("span", {}, t));

    let status = on ? STATUS[d.mode] || "Set to" : `Off, last set to ${set}°`;
    if (!s.available) status = "Air conditioner not responding";
    else if (s.lastApplyError) status = "Last change was not applied";

    const temperature = h("section", { class: `panel${on ? "" : " is-off"}`, "data-mode": d.mode, "aria-label": "Temperature" },
      h("div", { class: "temp-head" },
        h("div", {},
          h("p", { class: "temp-status" }, status),
          h("p", { class: "temp-big" }, on ? `${set}°` : "Off")),
        h("div", { class: "temp-steps dim" },
          h("button", { class: "icon-button icon-button--big", type: "button", "aria-label": "One degree cooler", disabled: !on || set <= min, html: ICON_MINUS, onclick: () => change({ targetTempC: set - 1 }, 600) }),
          h("button", { class: "icon-button icon-button--big", type: "button", "aria-label": "One degree warmer", disabled: !on || set >= max, html: ICON_PLUS, onclick: () => change({ targetTempC: set + 1 }, 600) }))),
      h("div", { class: "dial dim", style: `--set:${pct(set)};--room:${pct(room ?? set)}` },
        room !== null ? h("span", { class: "room", "aria-hidden": "true" }) : null,
        h("span", { class: "rail" }), h("span", { class: "fill" }), h("span", { class: "knob", "aria-hidden": "true" }),
        h("div", { class: "scale", "aria-hidden": "true" }, scale),
        h("input", {
          id: "target", type: "range", min, max, step: 1, value: set, disabled: !on, "aria-label": "Target temperature",
          oninput: (e) => {
            // Patch in place: re-rendering would replace the input and end the drag.
            const t = Number(e.target.value);
            change({ targetTempC: t }, 600, false);
            e.target.parentElement.style.setProperty("--set", pct(t));
            app.querySelector(".temp-big").textContent = `${t}°`;
          },
          onchange: () => render(),
        })),
      h("p", { class: "temp-foot" },
        h("span", {}, room !== null ? `Room ${room.toFixed(1)}°` : "Room temperature unknown"),
        s.reported.valid ? h("span", {}, `Unit reports ${Math.round(s.reported.setTempC)}°`) : null));

    return [
      h("header", { class: "top" },
        h("div", { class: "title" },
          h("h1", {}, ui.device.friendlyName),
          h("p", { class: "note" }, "Air conditioner")),
        h("button", {
          class: "power", type: "button", "aria-pressed": String(on), "aria-label": on ? "Turn off" : "Turn on",
          html: `${ICON_POWER}${on ? "On" : "Off"}`,
          onclick: () => change({ powerOn: !on, mode: !on && d.mode === "off" ? "auto" : d.mode }),
        }),
        h("button", { class: "icon-button", type: "button", "aria-label": "Settings", html: ICON_SETTINGS, onclick: openSettings })),
      ...messages(),
      h("div", { class: "control-grid" },
        temperature,
        h("section", { class: `panel${on ? "" : " is-off"}`, "aria-labelledby": "mode-h" },
          h("h2", { id: "mode-h", class: "dim" }, "Mode"),
          h("div", { class: "dim" }, segments("Mode", MODES, d.mode, (mode) => change({ mode, powerOn: true })))),
        h("section", { class: `panel${on ? "" : " is-off"}`, "aria-labelledby": "fan-h" },
          h("h2", { id: "fan-h", class: "dim" }, "Fan speed"),
          h("div", { class: "dim" }, segments("Fan speed", FANS, d.fan, (fan) => change({ fan }))))),
    ];
  }

  // Settings
  function openSettings() {
    const c = ui.connectors;
    ui.form = {
      friendlyName: ui.device.friendlyName,
      mqttEnabled: c.mqttEnabled,
      mqttHost: c.mqttHost || "",
      mqttPort: c.mqttPort,
      mqttTls: c.mqttTls,
      mqttUsername: "",
      mqttPassword: "",
      matterEnabled: c.matterEnabled,
      adminPassword: "",
      wifiSsid: "",
      wifiPassword: "",
    };
    ui.error = "";
    ui.notice = "";
    ui.view = "settings";
    render();
  }

  function bind(id, extra = {}) {
    return { id, value: ui.form[id], oninput: (e) => { ui.form[id] = e.target.value; }, ...extra };
  }

  function field(key, id, extra) {
    return h("label", { class: "row" }, h("span", { class: "key" }, key), h("input", bind(id, extra)));
  }

  function toggle(id, title, hint) {
    return h("label", { class: "row row--toggle" },
      h("span", {}, title, hint ? h("small", {}, hint) : null),
      h("input", { type: "checkbox", id, checked: ui.form[id], onchange: (e) => { ui.form[id] = e.target.checked; render(); } }));
  }

  function viewSettings() {
    const c = ui.connectors;
    const f = ui.form;
    return [
      h("header", { class: "top" },
        h("button", { class: "icon-button", type: "button", "aria-label": "Back to controls", html: ICON_BACK, onclick: () => { ui.view = "control"; ui.error = ""; render(); } }),
        h("div", { class: "title" }, h("h1", {}, "Settings"))),
      ...messages(),
      h("div", { class: "settings-grid" },
        h("div", { style: "display:flex;flex-direction:column;gap:8px" },
          h("h2", { class: "label", style: "margin:0" }, "This device"),
          h("div", { class: "list" },
            field("Name", "friendlyName", { maxlength: 32 }),
            field("Password", "adminPassword", { type: "password", minlength: 8, maxlength: 64, autocomplete: "new-password", placeholder: "Unchanged" })),
          h("p", { class: "note" }, h("span", { class: "mono" }, ui.device.productId), ` · ${ui.device.module}`),

          h("h2", { class: "label" }, "Smart home"),
          h("div", { class: "list" },
            toggle("mqttEnabled", "Home Assistant", "Over MQTT with discovery"),
            f.mqttEnabled ? [
              field("Broker", "mqttHost", { maxlength: 127, autocomplete: "off" }),
              h("label", { class: "row" }, h("span", { class: "key" }, "Port"),
                h("input", { id: "mqttPort", type: "number", min: 1, max: 65535, value: f.mqttPort, oninput: (e) => { f.mqttPort = Number(e.target.value); } })),
              field("Username", "mqttUsername", { maxlength: 64, autocomplete: "off", placeholder: c.mqttCredentialsConfigured ? "Unchanged" : "" }),
              field("Password", "mqttPassword", { type: "password", maxlength: 128, autocomplete: "new-password", placeholder: c.mqttCredentialsConfigured ? "Unchanged" : "" }),
              toggle("mqttTls", "Verify TLS certificate"),
            ] : null,
            c.matterSupported ? toggle("matterEnabled", "Matter", c.matterRunning ? "Running" : "Apple Home, Google Home and others") : null),
          c.matterSupported && c.matterEnabled
            ? h("button", { class: "button button--quiet button--small", type: "button", style: "align-self:flex-start", onclick: openCommissioning }, "Pair with Matter")
            : null,
          h("div", { class: "actions" },
            h("button", { class: "button", type: "button", disabled: ui.busy, onclick: saveConnectors }, "Save and restart"))),

        h("div", { style: "display:flex;flex-direction:column;gap:8px" },
          h("h2", { class: "label", style: "margin:0" }, "Move to another Wi‑Fi"),
          h("div", { class: "list" },
            field("Name", "wifiSsid", { maxlength: 32, autocomplete: "off", placeholder: "Network name" }),
            field("Password", "wifiPassword", { type: "password", maxlength: 64, autocomplete: "new-password", placeholder: "Leave empty if open" })),
          h("p", { class: "note" }, "The device restarts and joins the new network. If it can't connect, hold the reset button to run setup again."),
          h("div", { class: "actions" },
            h("button", { class: "button button--quiet", type: "button", disabled: ui.busy || !f.wifiSsid.trim(), onclick: saveWifi }, "Switch network")),

          h("h2", { class: "label" }, "Session"),
          h("div", { class: "actions", style: "margin-top:0;padding-top:0" },
            h("button", { class: "button button--quiet", type: "button", onclick: logout }, "Sign out")))),
    ];
  }

  async function act(action) {
    ui.busy = true;
    ui.error = "";
    ui.notice = "";
    render();
    try {
      await action();
    } catch (error) {
      ui.error = error.message;
    }
    ui.busy = false;
    render();
  }

  function saveConnectors() {
    const f = ui.form;
    if (f.adminPassword && f.adminPassword.length < 8) {
      ui.error = MESSAGES.invalid_admin_password;
      render();
      return;
    }
    act(async () => {
      const payload = {
        friendlyName: f.friendlyName.trim(),
        mqttEnabled: f.mqttEnabled,
        mqttHost: f.mqttHost,
        mqttPort: f.mqttPort,
        mqttTls: f.mqttTls,
        matterEnabled: f.matterEnabled,
      };
      if (f.mqttUsername) payload.mqttUsername = f.mqttUsername;
      if (f.mqttPassword) payload.mqttPassword = f.mqttPassword;
      if (f.adminPassword) payload.adminPassword = f.adminPassword;
      await api("/api/v1/connectors", { method: "PUT", body: JSON.stringify(payload) });
      rebooting("Settings saved", "The device is restarting. This page reconnects when it is back.");
    });
  }

  function saveWifi() {
    act(async () => {
      await api("/api/v1/wifi", {
        method: "PUT",
        body: JSON.stringify({ ssid: ui.form.wifiSsid.trim(), password: ui.form.wifiPassword }),
      });
      rebooting("Switching network", `The device is joining ${ui.form.wifiSsid.trim()}. Connect to that network and open this address again.`);
    });
  }

  function openCommissioning() {
    act(async () => {
      await api("/api/v1/matter/commissioning-window", { method: "POST", body: "{}" });
      ui.notice = "Pairing is open. Add the device in your Matter app now.";
    });
  }

  async function logout() {
    await api("/api/v1/auth/logout", { method: "POST", body: "{}" }).catch(() => {});
    signedOut();
  }

  // Restart
  let rebootText = ["", ""];
  function rebooting(title, text) {
    clearInterval(ui.pollTimer);
    storeCsrf(""); // sessions live in RAM and do not survive the restart
    ui.csrf = "";
    rebootText = [title, text];
    ui.view = "rebooting";
    setTimeout(waitForDevice, 6000);
  }

  async function waitForDevice() {
    try {
      const response = await fetch("/api/v1/session", { cache: "no-store" });
      if (response.ok) { signedOut(); return; }
    } catch { /* still restarting */ }
    setTimeout(waitForDevice, 3000);
  }

  function viewRebooting() {
    return [h("p", { class: "eyebrow" }, "OpenComfort"), h("h1", {}, rebootText[0]), h("p", { class: "note", style: "font-size:15px" }, rebootText[1])];
  }

  async function boot() {
    try {
      const session = await api("/api/v1/session");
      if (session.authenticated && ui.csrf) {
        await openDashboard();
      }
    } catch (error) {
      ui.error = error.message;
    }
    render();
  }

  boot();
})();
