(() => {
  // Builds DOM nodes; text always goes through textContent, so scanned SSIDs can't inject markup.
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

  const svg = (body, size = 20, extra = "") =>
    `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" ${extra}>${body}</svg>`;
  const ICON = {
    wifi: `<svg width="26" height="26" viewBox="0 0 24 18" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round" aria-hidden="true"><path d="M2 6.5a15 15 0 0 1 20 0"/><path d="M5.5 10.2a10 10 0 0 1 13 0"/><path d="M9 13.8a5 5 0 0 1 6 0"/><circle cx="12" cy="16.5" r="1.3" fill="currentColor" stroke="none"/></svg>`,
    ac: svg('<rect x="2.5" y="5" width="19" height="9" rx="2.5"/><path d="M6 11h12M8 17.5c0 1.2-1 1.5-1 2.5M12 17.5c0 1.2-1 1.5-1 2.5M16 17.5c0 1.2-1 1.5-1 2.5"/>', 26),
    lock: svg('<rect x="5" y="11" width="14" height="10" rx="2.5"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>', 24),
    smallLock: svg('<rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>', 14, 'role="img" aria-label="Secured"'),
    check: svg('<path d="M4.5 12.5l4.5 4.5L19.5 7"/>', 26),
    tick: svg('<path d="M4.5 12.5l4.5 4.5L19.5 7"/>', 12, 'stroke-width="3.5"'),
    back: svg('<path d="M15 5l-7 7 7 7"/>', 18),
    eye: svg('<path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>', 20),
    rescan: svg('<path d="M20 12a8 8 0 1 1-2.3-5.7"/><path d="M20 4v5h-5"/>', 15),
    plus: svg('<path d="M12 5v14M5 12h14"/>', 22),
  };
  function signal(rssi) {
    const bars = rssi >= -60 ? 3 : rssi >= -72 ? 2 : 1;
    const c = (n) => (bars >= n ? "var(--ink)" : "var(--line)");
    return `<svg width="22" height="17" viewBox="0 0 24 18" fill="none" stroke-width="2.4" stroke-linecap="round" role="img" aria-label="Signal ${bars} of 3"><path d="M2 6.5a15 15 0 0 1 20 0" style="stroke:${c(3)}"/><path d="M5.5 10.2a10 10 0 0 1 13 0" style="stroke:${c(2)}"/><path d="M9 13.8a5 5 0 0 1 6 0" style="stroke:${c(1)}"/><circle cx="12" cy="16.5" r="1.3" style="fill:var(--ink)"/></svg>`;
  }

  const MESSAGES = {
    choose_one_integration: "Choose Home Assistant or Matter, not both.",
    mqtt_not_supported: "This firmware has no Home Assistant (MQTT) support.",
    wifi_connect_failed: "Could not join. Check the password and try again.",
    wifi_wrong_password: "The network refused the password. Check it and try again.",
    wifi_not_found: "The network is out of reach. Move the device closer to the router, or check that it is a 2.4 GHz network.",
    wifi_security_unsupported: "This network uses security the device does not support. Use WPA2 or WPA3 (personal).",
    request_failed: "The device did not answer. Check that you are still on the OpenComfort network.",
    profiles_unavailable: "Could not download the list of air conditioners. Check that this Wi-Fi network has internet, then try again.",
  };

  async function api(path, options = {}) {
    const response = await fetch(path, { headers: { "Content-Type": "application/json" }, ...options });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) throw new Error(MESSAGES[data.error] || data.error || MESSAGES.request_failed);
    return data;
  }

  const app = document.getElementById("app");
  const state = {
    step: 0,
    busy: false,
    error: "",
    session: null,
    schema: null,
    networks: [],
    scanning: false,
    open: null, // SSID whose password row is open, or "__other"
    joined: null,
    joinError: "",
    password: "",
    showPassword: false,
    otherName: "",
    model: "",
    friendlyName: "Living room",
    adminPassword: "",
    showAdmin: false,
    mqttEnabled: false,
    // Home Assistant's Mosquitto add-on: plain MQTT on 1883 at the Home Assistant host.
    mqttHost: "homeassistant.local",
    mqttPort: 1883,
    mqttTls: false,
    mqttUsername: "",
    mqttPassword: "",
    matterEnabled: false,
  };

  // Re-rendering replaces inputs, so keep focus and caret on the field being typed in.
  function render() {
    primaryReady = null;
    const active = document.activeElement;
    const focused = active?.id;
    const caret = active && "selectionStart" in active ? active.selectionStart : null;
    app.className = "shell setup";
    app.replaceChildren(...[[viewWifi, viewProduct, viewAccess, viewDone][state.step]()].flat(Infinity).filter(Boolean));
    const again = focused && document.getElementById(focused);
    if (again) {
      again.focus();
      if (caret !== null) try { again.setSelectionRange(caret, caret); } catch { /* not a text input */ }
    }
  }

  // Typing must not re-render (the keyboard would close), so only the primary
  // button of the current step is re-evaluated after each keystroke.
  let primaryReady = null;
  function syncPrimary() {
    const button = document.getElementById("primary");
    if (button && primaryReady) button.disabled = !primaryReady() || state.busy;
  }

  function bind(id, extra = {}) {
    return { id, value: state[id], oninput: (e) => { state[id] = e.target.value; syncPrimary(); }, ...extra };
  }

  function progress(step) {
    return h("div", { class: "progress", role: "progressbar", "aria-label": "Setup progress", "aria-valuemin": 1, "aria-valuemax": 3, "aria-valuenow": step },
      [1, 2, 3].map((n) => h("span", { class: n <= step ? "on" : "" })));
  }

  function header({ step, icon, eyebrow, title, lede, back, ok }) {
    return [
      back ? h("button", { class: "back", type: "button", onclick: back, html: `${ICON.back}Back` }) : null,
      progress(step),
      h("span", { class: ok ? "badge ok" : "badge", html: icon }),
      h("p", { class: "eyebrow", style: ok ? "color:var(--ok)" : null }, eyebrow),
      h("h1", {}, title),
      lede ? h("p", { class: "lede" }, lede) : null,
    ];
  }

  function field({ id, label, type = "text", value, oninput, eye, extra = {} }) {
    return h("div", { class: "field" },
      h("span", { class: "grow" },
        h("label", { for: id }, label),
        h("input", { id, type, value, oninput, ...extra })),
      eye ? h("button", { class: "eye", type: "button", "aria-label": eye.shown ? "Hide password" : "Show password", "aria-pressed": String(eye.shown), onclick: eye.toggle, html: ICON.eye }) : null);
  }

  function alertBox() {
    return state.error ? h("p", { class: "alert", role: "alert" }, state.error) : null;
  }

  async function run(action) {
    state.busy = true;
    state.error = "";
    render();
    try {
      await action();
    } catch (error) {
      state.error = error.message;
    }
    state.busy = false;
    render();
  }

  // Step 1: network, with the password opening right under the tapped network
  function viewWifi() {
    const rows = state.networks.map((n) => {
      const isOpen = state.open === n.ssid;
      const joined = state.joined === n.ssid;
      return h("div", {},
        h("button", {
          class: "row net", type: "button", "aria-expanded": String(isOpen),
          onclick: () => pickNetwork(n),
        },
        h("span", { html: signal(n.rssi) }),
        h("span", { class: "grow" },
          h("span", { class: "name", style: isOpen || joined ? "font-weight:600" : null }, n.ssid),
          joined ? h("small", { style: "color:var(--ok);font-weight:500" }, "Connected · password checked") : null),
        n.authmode !== 0 ? h("span", { class: "lock", html: ICON.smallLock }) : null,
        joined ? h("span", { class: "check", html: ICON.tick }) : null),
        isOpen ? passwordPanel(n.ssid) : null);
    });
    if (!rows.length) {
      rows.push(h("p", { class: "row", style: "color:var(--muted)" }, state.scanning ? "Looking for networks…" : "No networks found"));
    }
    const otherOpen = state.open === "__other";
    rows.push(h("div", {},
      h("button", { class: "row net-other", type: "button", "aria-expanded": String(otherOpen), onclick: () => { state.open = otherOpen ? null : "__other"; state.password = ""; state.joinError = ""; render(); }, html: `${ICON.plus}Other network…` }),
      otherOpen ? otherPanel() : null));

    return [
      ...header({ step: 1, icon: ICON.wifi, eyebrow: "STEP 1 OF 3", title: "Choose a network", lede: "Tap your home Wi‑Fi. Only 2.4 GHz networks work with this device." }),
      h("div", { class: "top", style: "margin:20px 4px 8px" },
        h("h2", { class: "grow" }, "Networks nearby"),
        h("button", { class: "text-btn", type: "button", onclick: scan, disabled: state.scanning, html: `${ICON.rescan}${state.scanning ? "Scanning…" : "Scan again"}` })),
      h("div", { class: "list" }, rows),
      alertBox(),
      h("div", { class: "spacer" }),
      state.joined ? h("button", { class: "btn block", type: "button", style: "margin-top:16px", onclick: () => { state.step = 1; state.error = ""; render(); } }, "Continue") : null,
    ];
  }

  function joinButton(label) {
    const ready = state.open === "__other" ? state.otherName.trim() !== "" : state.password.length >= 8;
    return h("button", { class: "btn block", type: "button", style: "height:48px;border-radius:12px", disabled: !ready || state.busy, onclick: join }, state.busy ? "Checking…" : label);
  }

  function passwordPanel(ssid) {
    return h("div", { class: "net-open" },
      h("div", { class: "field filled" },
        h("span", { class: "grow" },
          h("label", { for: "wifiPassword" }, "Password"),
          h("input", { id: "wifiPassword", type: state.showPassword ? "text" : "password", value: state.password, autocomplete: "current-password", maxlength: 64, oninput: (e) => { state.password = e.target.value; refreshJoin(); } })),
        h("button", { class: "eye", type: "button", "aria-label": state.showPassword ? "Hide password" : "Show password", "aria-pressed": String(state.showPassword), onclick: () => { state.showPassword = !state.showPassword; render(); }, html: ICON.eye })),
      state.joinError ? h("p", { class: "alert", role: "alert" }, state.joinError) : null,
      joinButton(`Join ${ssid}`));
  }

  function otherPanel() {
    return h("div", { class: "net-open", style: "gap:8px" },
      h("div", { class: "field filled" }, h("span", { class: "grow" },
        h("label", { for: "otherName" }, "Network name"),
        h("input", { id: "otherName", value: state.otherName, autocomplete: "off", maxlength: 32, oninput: (e) => { state.otherName = e.target.value; refreshJoin(); } }))),
      h("div", { class: "field filled" }, h("span", { class: "grow" },
        h("label", { for: "wifiPassword" }, "Password · empty if open"),
        h("input", { id: "wifiPassword", type: "password", value: state.password, maxlength: 64, oninput: (e) => { state.password = e.target.value; } }))),
      state.joinError ? h("p", { class: "alert", role: "alert" }, state.joinError) : null,
      joinButton("Join network"));
  }

  // Only the join button depends on typing; updating it in place keeps the keyboard up.
  function refreshJoin() {
    const button = app.querySelector(".net-open .btn");
    if (!button) return;
    const ready = state.open === "__other" ? state.otherName.trim() !== "" : state.password.length >= 8;
    button.disabled = !ready || state.busy;
  }

  function pickNetwork(n) {
    state.joinError = "";
    if (n.authmode === 0) {
      state.open = null;
      state.password = "";
      join(n.ssid);
      return;
    }
    state.open = state.open === n.ssid ? null : n.ssid;
    state.password = "";
    state.showPassword = false;
    render();
  }

  async function join(openSsid) {
    const ssid = typeof openSsid === "string" ? openSsid : state.open === "__other" ? state.otherName.trim() : state.open;
    state.busy = true;
    state.joinError = "";
    render();
    try {
      await api("/api/v1/wifi", { method: "PUT", body: JSON.stringify({ ssid, password: state.password }) });
      state.joined = ssid;
      state.open = null;
      await loadSchema();
      if (!state.networks.some((n) => n.ssid === ssid)) state.networks.unshift({ ssid, rssi: -60, authmode: state.password ? 3 : 0 });
    } catch (error) {
      state.joinError = error.message;
    }
    state.busy = false;
    render();
  }

  // The list comes from the online catalog, so it can only load once Wi-Fi works.
  async function loadSchema() {
    try {
      state.schema = await api("/api/v1/setup/schema");
      state.schemaError = "";
    } catch (error) {
      state.schemaError = error.message;
    }
  }

  async function scan() {
    state.scanning = true;
    state.error = "";
    render();
    try {
      const data = await api("/api/v1/wifi/networks");
      const best = new Map();
      for (const n of data.networks || []) {
        if (n.ssid && (!best.has(n.ssid) || best.get(n.ssid).rssi < n.rssi)) best.set(n.ssid, n);
      }
      state.networks = [...best.values()].sort((a, b) => b.rssi - a.rssi);
    } catch (error) {
      state.error = error.message;
    }
    state.scanning = false;
    render();
  }

  // Step 2: air conditioner
  function productReady() {
    return Boolean(state.model) && state.friendlyName.trim() !== "";
  }

  function viewProduct() {
    primaryReady = productReady;
    const field_ = state.schema?.steps?.[0]?.fields?.find((f) => f.id === "model");
    const options = field_?.options || [];
    if (!state.model && options[0]) state.model = options[0].value;
    return [
      ...header({ step: 2, icon: ICON.ac, eyebrow: "STEP 2 OF 3", title: "Your air conditioner", lede: "Pick the indoor unit this device is plugged into.", back: () => { state.step = 0; render(); } }),
      h("div", { class: "list", role: "radiogroup", "aria-label": "Model", style: "margin-top:20px" },
        options.map((o) => h("button", { class: "row", type: "button", role: "radio", "aria-checked": String(o.value === state.model), onclick: () => { state.model = o.value; render(); } },
          h("span", { class: "grow" }, h("span", { class: "name" }, o.label)),
          h("span", { class: "radio", "aria-hidden": "true" })))),
      options.length === 0 && state.schemaError ? h("p", { class: "alert", role: "alert" }, state.schemaError) : null,
      options.length === 0 ? h("button", { class: "btn block", type: "button", style: "height:48px;border-radius:12px", disabled: state.busy, onclick: () => run(loadSchema) }, "Load the list again") : null,
      h("p", { class: "note" }, "Unit not listed? It needs a profile first; see the project docs."),
      h("div", { style: "margin-top:24px" }, field({ id: "friendlyName", label: "Device name", ...bind("friendlyName"), extra: { maxlength: 32 } })),
      h("p", { class: "note" }, "Shown here, in Home Assistant and in Matter apps."),
      alertBox(),
      h("div", { class: "spacer" }),
      h("button", { id: "primary", class: "btn block", type: "button", style: "margin-top:16px", disabled: !productReady() || state.busy, onclick: submitProduct }, state.busy ? "Saving…" : "Continue"),
    ];
  }

  function submitProduct() {
    run(async () => {
      await api("/api/v1/setup/module", { method: "PUT", body: JSON.stringify({ model: state.model, friendlyName: state.friendlyName.trim() }) });
      state.step = 2;
    });
  }

  // Step 3: access and integrations
  function toggle(id, title, hint) {
    return h("label", { class: "row" },
      h("span", { class: "grow" }, h("span", {}, title), hint ? h("small", {}, hint) : null),
      h("input", { class: "switch", type: "checkbox", id, checked: state[id], onchange: (e) => { state[id] = e.target.checked; render(); } }));
  }

  function listField(id, label, extra = {}) {
    return h("label", { class: "row", for: id },
      h("span", { style: "width:88px;flex:0 0 auto" }, label),
      h("input", { ...bind(id, extra), style: "flex:1 1 auto;min-width:0;border:0;background:transparent;font-size:16px;outline:none;height:52px" }));
  }

  function accessReady() {
    return state.adminPassword.length >= 8;
  }

  // One integration at a time; both would show the unit twice in Home Assistant.
  function integrationChoice() {
    const current = state.mqttEnabled ? "ha" : state.matterEnabled ? "matter" : "none";
    const options = [["none", "None"]];
    if (state.session?.mqttSupported !== false) options.push(["ha", "Home Assistant"]);
    if (state.session?.matterSupported) options.push(["matter", "Matter"]);
    return h("div", { class: "segments", role: "radiogroup", "aria-label": "Smart home" },
      options.map(([value, label]) => h("button", {
        type: "button", role: "radio", "aria-checked": String(current === value), "aria-pressed": String(current === value),
        onclick: () => { state.mqttEnabled = value === "ha"; state.matterEnabled = value === "matter"; render(); },
      }, label)));
  }

  function viewAccess() {
    primaryReady = accessReady;
    return [
      ...header({ step: 3, icon: ICON.lock, eyebrow: "STEP 3 OF 3", title: "Secure and connect", lede: "Anyone on your network needs this password to control the air conditioner.", back: () => { state.step = 1; render(); } }),
      h("div", { style: "margin-top:20px" }, field({
        id: "adminPassword", label: "Portal password · at least 8 characters", type: state.showAdmin ? "text" : "password",
        ...bind("adminPassword"), extra: { minlength: 8, maxlength: 64, autocomplete: "new-password" },
        eye: { shown: state.showAdmin, toggle: () => { state.showAdmin = !state.showAdmin; render(); } },
      })),
      h("p", { class: "note" }, "Forgot it later? Switch the power off and on 5 times in a row, each time within 10 seconds, to start over."),
      h("h2", { class: "section-title", style: "margin-top:24px" }, "Smart home ", h("span", { style: "font-weight:400" }, "(optional)")),
      integrationChoice(),
      state.mqttEnabled ? h("div", { class: "list", style: "margin-top:12px" },
        listField("mqttHost", "Broker", { placeholder: "homeassistant.local", maxlength: 127, autocomplete: "off" }),
        h("label", { class: "row", for: "mqttPort" }, h("span", { style: "width:88px;flex:0 0 auto" }, "Port"),
          h("input", { id: "mqttPort", type: "number", min: 1, max: 65535, value: state.mqttPort, oninput: (e) => { state.mqttPort = Number(e.target.value); }, style: "flex:1 1 auto;min-width:0;border:0;background:transparent;font-size:16px;outline:none;height:52px" })),
        listField("mqttUsername", "Username", { maxlength: 64, autocomplete: "off" }),
        listField("mqttPassword", "Password", { type: "password", maxlength: 128, autocomplete: "new-password" }),
        toggle("mqttTls", "Verify TLS certificate")) : null,
      h("p", { class: "note" }, state.mqttEnabled
        ? "Over MQTT. The air conditioner appears in Home Assistant by itself."
        : state.matterEnabled
          ? "After setup, open Settings and tap Pair with a home app. Home Assistant can pair over Matter too."
          : "You can add one later in Settings."),
      alertBox(),
      h("div", { class: "spacer" }),
      h("button", { id: "primary", class: "btn block", type: "button", style: "margin-top:16px", disabled: !accessReady() || state.busy, onclick: finish }, state.busy ? "Saving…" : "Finish setup"),
    ];
  }

  function finish() {
    run(async () => {
      await api("/api/v1/setup/connectors", {
        method: "PUT",
        body: JSON.stringify({
          mqttEnabled: state.mqttEnabled, mqttHost: state.mqttHost, mqttPort: state.mqttPort, mqttTls: state.mqttTls,
          mqttUsername: state.mqttUsername, mqttPassword: state.mqttPassword, matterEnabled: state.matterEnabled,
          adminPassword: state.adminPassword,
        }),
      });
      await api("/api/v1/setup/commit", { method: "POST", body: "{}" });
      state.step = 3;
    });
  }

  // Done
  function viewDone() {
    const host = state.session?.hostname || "opencomfort-xxxx.local";
    return [
      h("div", { class: "progress", style: "margin-top:40px", "aria-hidden": "true" }, [1, 2, 3].map(() => h("span", { class: "on" }))),
      h("span", { class: "badge ok", html: ICON.check }),
      h("p", { class: "eyebrow", style: "color:var(--ok)" }, "ALL SET"),
      h("h1", {}, `${state.friendlyName.trim()} is set up`),
      h("p", { class: "lede" }, `The device restarts and joins ${state.joined || "your Wi‑Fi"}. This takes about 20 seconds.`),
      h("h2", { class: "section-title", style: "margin-top:24px" }, "Device address"),
      h("div", { class: "list" },
        h("div", { class: "row", style: "padding-right:4px" },
          h("span", { class: "grow mono", style: "font-size:15px" }, host),
          h("button", { class: "text-btn", type: "button", style: "height:44px;padding:0 12px", onclick: (e) => copy(`http://${host}`, e.currentTarget) }, "Copy"))),
      h("p", { class: "note" }, `Switch your phone back to ${state.joined || "your Wi‑Fi"} and open this address. Sign in with the portal password you just chose.`),
      h("div", { class: "spacer" }),
      h("a", { class: "btn block", href: `http://${host}/`, style: "margin-top:16px" }, "Open controls"),
    ];
  }

  // navigator.clipboard only exists on https; the portal is plain http on the LAN, so fall
  // back to a selected textarea and execCommand, which browsers allow inside a click.
  async function copyText(text) {
    if (window.isSecureContext && navigator.clipboard) {
      try { await navigator.clipboard.writeText(text); return true; } catch { /* fall back */ }
    }
    const area = document.createElement("textarea");
    area.value = text;
    area.setAttribute("readonly", "");
    area.style.cssText = "position:fixed;top:0;left:0;opacity:0;pointer-events:none";
    document.body.append(area);
    area.select();
    area.setSelectionRange(0, text.length);
    let ok = false;
    try { ok = document.execCommand("copy"); } catch { ok = false; }
    area.remove();
    return ok;
  }

  async function copy(text, button) {
    const ok = await copyText(text);
    button.textContent = ok ? "Copied" : "Select and copy the address";
    if (ok) setTimeout(() => { button.textContent = "Copy"; }, 2000);
  }

  async function boot() {
    try {
      state.session = await api("/api/v1/session");
      if (state.session.wifiConfigured) await loadSchema();  // page reloaded after joining
    } catch (error) {
      state.error = error.message;
    }
    render();
    scan();
  }

  boot();
})();
