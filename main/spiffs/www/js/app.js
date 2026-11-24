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

  const ICON_CHECK = '<svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="3" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M4.5 12.5l4.5 4.5L19.5 7"/></svg>';
  const ICON_LOCK = '<svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="#565C63" stroke-width="2.2" stroke-linecap="round" role="img" aria-label="Secured"><rect x="5" y="11" width="14" height="10" rx="2"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/></svg>';

  function signalIcon(rssi) {
    const bars = rssi >= -60 ? 3 : rssi >= -72 ? 2 : 1;
    const c = (n) => (bars >= n ? "#14161A" : "#C9CDC6");
    return `<svg width="20" height="16" viewBox="0 0 24 18" fill="none" stroke-width="2.4" stroke-linecap="round" role="img" aria-label="Signal ${bars} of 3"><path d="M2 6.5a15 15 0 0 1 20 0" stroke="${c(3)}"/><path d="M5.5 10.2a10 10 0 0 1 13 0" stroke="${c(2)}"/><path d="M9 13.8a5 5 0 0 1 6 0" stroke="${c(1)}"/><circle cx="12" cy="16.5" r="1.3" fill="#14161A"/></svg>`;
  }

  const MESSAGES = {
    request_failed: "The device did not answer. Check that you are still on the OpenComfort network.",
  };

  async function api(path, options = {}) {
    const response = await fetch(path, {
      headers: { "Content-Type": "application/json" },
      ...options,
    });
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
    ssid: "",
    manual: false,
    manualSsid: "",
    wifiPassword: "",
    model: "",
    friendlyName: "Living room",
    adminPassword: "",
    mqttEnabled: false,
    mqttHost: "",
    mqttPort: 8883,
    mqttTls: true,
    mqttUsername: "",
    mqttPassword: "",
    matterEnabled: false,
  };

  // Re-rendering replaces inputs, so keep focus on the field the user is typing in.
  function render() {
    const focused = document.activeElement?.id;
    app.replaceChildren(...[[viewWifi, viewProduct, viewAccess, viewDone][state.step]()].flat(Infinity).filter(Boolean));
    if (focused) document.getElementById(focused)?.focus();
  }

  function bind(id) {
    return {
      id,
      value: state[id],
      oninput: (event) => { state[id] = event.target.value; },
    };
  }

  function alertBox() {
    return state.error ? h("p", { class: "alert", role: "alert" }, state.error) : null;
  }

  function header(stepText, title) {
    return [h("p", { class: "eyebrow" }, stepText), h("h1", {}, title)];
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

  // Step 1: network
  function selectedNetwork() {
    return state.networks.find((n) => n.ssid === state.ssid);
  }

  function viewWifi() {
    const net = selectedNetwork();
    const needsPassword = !state.manual && net && net.authmode !== 0;
    const ready = state.manual ? state.manualSsid.trim() !== "" : Boolean(net);

    const rows = state.networks.map((n) =>
      h("button", {
        class: "row",
        type: "button",
        "aria-pressed": String(!state.manual && n.ssid === state.ssid),
        onclick: () => { state.ssid = n.ssid; state.manual = false; state.wifiPassword = ""; render(); },
      },
      h("span", { class: "check", html: !state.manual && n.ssid === state.ssid ? ICON_CHECK : "" }),
      h("span", { class: "name" }, n.ssid),
      n.authmode !== 0 ? h("span", { html: ICON_LOCK }) : null,
      h("span", { html: signalIcon(n.rssi) }))
    );
    if (!rows.length) {
      rows.push(h("p", { class: "row waiting" }, state.scanning ? "Looking for networks…" : "No networks found"));
    }
    rows.push(h("button", {
      class: "row row--link",
      type: "button",
      "aria-pressed": String(state.manual),
      onclick: () => { state.manual = true; state.ssid = ""; render(); },
    }, h("span", { class: "check", html: state.manual ? ICON_CHECK : "" }), "Other…"));

    return [
      ...header("Step 1 of 3", "Choose a network"),
      h("div", { class: "top", style: "margin-top:8px" },
        h("h2", { class: "title", style: "color:var(--muted)" }, "Networks"),
        h("button", { class: "text-button", type: "button", onclick: scan, disabled: state.scanning }, state.scanning ? "Scanning…" : "Scan again")),
      h("div", { class: "list" }, rows),
      h("p", { class: "note" }, "Only 2.4 GHz networks are listed."),
      needsPassword ? [
        h("h2", { class: "label" }, `Password for ${net.ssid}`),
        h("div", { class: "list" },
          h("label", { class: "row" },
            h("input", { ...bind("wifiPassword"), type: "password", autocomplete: "current-password", "aria-label": `Password for ${net.ssid}`, maxlength: 64 }))),
      ] : null,
      state.manual ? [
        h("h2", { class: "label" }, "Other network"),
        h("div", { class: "list" },
          h("label", { class: "row" }, h("span", { class: "key" }, "Name"),
            h("input", { ...bind("manualSsid"), placeholder: "Network name", autocomplete: "off", maxlength: 32 })),
          h("label", { class: "row" }, h("span", { class: "key" }, "Password"),
            h("input", { ...bind("wifiPassword"), type: "password", placeholder: "Leave empty if open", maxlength: 64 }))),
      ] : null,
      alertBox(),
      h("div", { class: "actions" },
        h("button", { class: "button", type: "button", disabled: !ready || state.busy, onclick: submitWifi }, state.busy ? "Saving…" : "Continue")),
    ];
  }

  async function scan() {
    state.scanning = true;
    state.error = "";
    render();
    try {
      const data = await api("/api/v1/wifi/networks");
      // One row per SSID, strongest first.
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

  function submitWifi() {
    run(async () => {
      const ssid = state.manual ? state.manualSsid.trim() : state.ssid;
      await api("/api/v1/wifi", { method: "PUT", body: JSON.stringify({ ssid, password: state.wifiPassword }) });
      state.step = 1;
    });
  }

  // Step 2: air conditioner
  function modelField() {
    return state.schema?.steps?.[0]?.fields?.find((f) => f.id === "model");
  }

  function viewProduct() {
    const field = modelField();
    const options = field?.options || [];
    if (!state.model && options[0]) state.model = options[0].value;
    return [
      ...header("Step 2 of 3", "Choose your air conditioner"),
      h("h2", { class: "label" }, field?.label || "Model"),
      h("div", { class: "list" }, options.map((o) =>
        h("button", {
          class: "row",
          type: "button",
          "aria-pressed": String(o.value === state.model),
          onclick: () => { state.model = o.value; render(); },
        }, h("span", { class: "check", html: o.value === state.model ? ICON_CHECK : "" }), h("span", { class: "name" }, o.label)))),
      h("p", { class: "note" }, "Your unit isn't listed? It needs a profile first; see the project docs."),
      h("h2", { class: "label" }, "Name"),
      h("div", { class: "list" },
        h("label", { class: "row" },
          h("input", { ...bind("friendlyName"), maxlength: 32, "aria-label": "Device name", placeholder: "Living room" }))),
      h("p", { class: "note" }, "Shown in the portal, Home Assistant and Matter."),
      alertBox(),
      h("div", { class: "actions" },
        h("button", { class: "button button--quiet", type: "button", onclick: () => { state.step = 0; state.error = ""; render(); } }, "Back"),
        h("button", { class: "button", type: "button", disabled: !state.model || !state.friendlyName.trim() || state.busy, onclick: submitProduct }, state.busy ? "Saving…" : "Continue")),
    ];
  }

  function submitProduct() {
    run(async () => {
      await api("/api/v1/setup/module", {
        method: "PUT",
        body: JSON.stringify({ model: state.model, friendlyName: state.friendlyName.trim() }),
      });
      state.step = 2;
    });
  }

  // Step 3: access and integrations
  function toggleRow(id, title, hint) {
    return h("label", { class: "row row--toggle" },
      h("span", {}, title, hint ? h("small", {}, hint) : null),
      h("input", {
        type: "checkbox",
        id,
        checked: state[id],
        onchange: (event) => { state[id] = event.target.checked; render(); },
      }));
  }

  function viewAccess() {
    const passwordOk = state.adminPassword.length >= 8;
    return [
      ...header("Step 3 of 3", "Secure and connect"),
      h("h2", { class: "label" }, "Portal password"),
      h("div", { class: "list" },
        h("label", { class: "row" },
          h("input", { ...bind("adminPassword"), type: "password", minlength: 8, maxlength: 64, autocomplete: "new-password", "aria-label": "Portal password", placeholder: "At least 8 characters" }))),
      h("p", { class: "note" }, "You sign in with this to control the device. Forgot it? Hold the reset button for 5 seconds."),
      h("h2", { class: "label" }, "Smart home"),
      h("div", { class: "list" },
        toggleRow("mqttEnabled", "Home Assistant", "Over MQTT with discovery"),
        state.mqttEnabled ? [
          h("label", { class: "row" }, h("span", { class: "key" }, "Broker"),
            h("input", { ...bind("mqttHost"), placeholder: "homeassistant.local", maxlength: 127, autocomplete: "off" })),
          h("label", { class: "row" }, h("span", { class: "key" }, "Port"),
            h("input", { id: "mqttPort", type: "number", min: 1, max: 65535, value: state.mqttPort, oninput: (e) => { state.mqttPort = Number(e.target.value); } })),
          h("label", { class: "row" }, h("span", { class: "key" }, "Username"),
            h("input", { ...bind("mqttUsername"), maxlength: 64, autocomplete: "off" })),
          h("label", { class: "row" }, h("span", { class: "key" }, "Password"),
            h("input", { ...bind("mqttPassword"), type: "password", maxlength: 128, autocomplete: "new-password" })),
          toggleRow("mqttTls", "Verify TLS certificate"),
        ] : null,
        state.session?.matterSupported ? toggleRow("matterEnabled", "Matter", "Apple Home, Google Home and others") : null),
      alertBox(),
      h("div", { class: "actions" },
        h("button", { class: "button button--quiet", type: "button", onclick: () => { state.step = 1; state.error = ""; render(); } }, "Back"),
        h("button", { class: "button", type: "button", disabled: !passwordOk || state.busy, onclick: finish }, state.busy ? "Saving…" : "Finish setup")),
    ];
  }

  function finish() {
    run(async () => {
      await api("/api/v1/setup/connectors", {
        method: "PUT",
        body: JSON.stringify({
          mqttEnabled: state.mqttEnabled,
          mqttHost: state.mqttHost,
          mqttPort: state.mqttPort,
          mqttTls: state.mqttTls,
          mqttUsername: state.mqttUsername,
          mqttPassword: state.mqttPassword,
          matterEnabled: state.matterEnabled,
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
      ...header("Done", `${state.friendlyName.trim()} is set up`),
      h("p", { class: "note", style: "font-size:15px" }, "The device restarts and joins your Wi‑Fi. Reconnect your phone to your home network, then open:"),
      h("h2", { class: "label" }, "Device address"),
      h("div", { class: "panel" },
        h("a", { class: "mono", href: `http://${host}/`, style: "font-size:15px;color:var(--link);word-break:break-all" }, `http://${host}`)),
      h("p", { class: "note" }, "Sign in with the portal password you just chose. If the address does not open, find the device's IP address in your router."),
    ];
  }

  async function boot() {
    try {
      [state.session, state.schema] = await Promise.all([api("/api/v1/session"), api("/api/v1/setup/schema")]);
    } catch (error) {
      state.error = error.message;
    }
    render();
    scan();
  }

  boot();
})();
