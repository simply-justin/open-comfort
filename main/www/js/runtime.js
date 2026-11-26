(() => {
  // Builds DOM nodes; text always goes through textContent.
  function h(tag, props = {}, ...children) {
    const el = document.createElement(tag);
    for (const [key, value] of Object.entries(props)) {
      if (value === undefined || value === null || value === false) continue;
      if (key.startsWith("on")) el.addEventListener(key.slice(2), value);
      else if (key === "html") el.innerHTML = value; // static markup or numbers only
      else el.setAttribute(key, value === true ? "" : value);
    }
    for (const child of children.flat(Infinity)) {
      if (child === null || child === undefined || child === false) continue;
      el.append(child instanceof Node ? child : document.createTextNode(String(child)));
    }
    return el;
  }

  const svg = (d, size = 20, width = 1.9) =>
    `<svg width="${size}" height="${size}" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="${d}"/></svg>`;
  const P = {
    power: "M12 3v8M6.3 6.8a8 8 0 1 0 11.4 0",
    climate: "M10 14.5V4a2 2 0 1 1 4 0v10.5a4 4 0 1 1-4 0z",
    schedule: "M12 7v5l3 2M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0z",
    status: "M3 12h4l3-7 4 14 3-7h4",
    settings: "M4 8h9M17 8h3M4 16h3M11 16h9M15 6v4M9 14v4",
    back: "M15 5l-7 7 7 7",
    chevron: "M9 5l7 7-7 7",
    minus: "M5 12h14",
    plus: "M5 12h14M12 5v14",
    close: "M6 6l12 12M18 6L6 18",
    copy: "M9 9h11v11H9zM5 15V6a2 2 0 0 1 2-2h8",
    check: "M4.5 12.5l4.5 4.5L19.5 7",
    alert: "M12 8v5M12 16.5h.01M10.3 4l-8 14a2 2 0 0 0 1.7 3h16a2 2 0 0 0 1.7-3l-8-14a2 2 0 0 0-3.4 0z",
    home: "M3 11l9-7 9 7v9a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1zM9 21v-6h6v6",
    lock: "M5 11h14v10H5zM8 11V8a4 4 0 0 1 8 0v3",
    wifi: "M2 8.5a15 15 0 0 1 20 0M5.5 12.2a10 10 0 0 1 13 0M9 15.8a5 5 0 0 1 6 0M12 19h.01",
    globe: "M12 3a9 9 0 1 0 0 18 9 9 0 0 0 0-18zM3 12h18M12 3c2.5 3 2.5 15 0 18M12 3c-2.5 3-2.5 15 0 18",
    matter: "M12 3l3 5h-6zM5 19l3-5 3 5zM13 19l3-5 3 5z",
    qr: "M4 4h6v6H4zM14 4h6v6h-6zM4 14h6v6H4zM14 14h2M18 14h2v2M14 18v2h2M18 20h2",
    bus: "M4 7h16M4 12h16M4 17h10M18 15v4",
    chip: "M7 7h10v10H7zM10 3v4M14 3v4M10 17v4M14 17v4M3 10h4M3 14h4M17 10h4M17 14h4",
    download: "M12 4v11M7 10l5 5 5-5M5 20h14",
    ac: "M2.5 5h19v9h-19zM6 11h12M8 17.5c0 1.2-1 1.5-1 2.5M12 17.5c0 1.2-1 1.5-1 2.5M16 17.5c0 1.2-1 1.5-1 2.5",
  };
  const MODE_ICON = {
    auto: "M12 3l1.8 4.7L18.5 9.5l-4.7 1.8L12 16l-1.8-4.7L5.5 9.5l4.7-1.8zM19 15l.8 2.2 2.2.8-2.2.8L19 21l-.8-2.2-2.2-.8 2.2-.8z",
    cool: "M12 2v20M4.2 6.5l15.6 11M19.8 6.5L4.2 17.5M9 4l3 2 3-2M9 20l3-2 3 2",
    heat: "M12 3v2M12 19v2M3 12h2M19 12h2M5.6 5.6l1.4 1.4M17 17l1.4 1.4M5.6 18.4L7 17M17 7l1.4-1.4M12 8a4 4 0 1 0 0 8 4 4 0 0 0 0-8z",
    dry: "M12 3.5s6 6.4 6 10.5a6 6 0 0 1-12 0c0-4.1 6-10.5 6-10.5z",
    fan: "M12 12c0-4 1-8 4-8s3 4-4 8zM12 12c4 0 8 1 8 4s-4 3-8-4zM12 12c0 4-1 8-4 8s-3-4 4-8zM12 12c-4 0-8-1-8-4s4-3 8 4z",
  };
  const MODES = [["auto", "Auto"], ["cool", "Cool"], ["heat", "Heat"], ["dry", "Dry"], ["fan", "Fan"]];
  const FANS = [["auto", "Auto"], ["low", "Low"], ["medium", "Medium"], ["high", "High"]];
  const STATUS = { cool: "Cooling", heat: "Heating", dry: "Drying", fan: "Fan only", auto: "Auto" };
  // What the unit reports it is doing; only when the profile reads the compressor.
  const ACTION = { cooling: "Cooling", heating: "Heating", drying: "Drying", fan: "Fan only", idle: "Idle, at temperature", off: "Off" };
  const VANE_LABEL = (name) => (/^\d+$/.test(name) ? `Position ${name}` : name.charAt(0).toUpperCase() + name.slice(1));
  const DAYS = ["Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"];
  const TIMEZONES = [
    ["CET-1CEST,M3.5.0,M10.5.0/3", "Central Europe (Amsterdam, Berlin, Paris)"],
    ["GMT0BST,M3.5.0/1,M10.5.0", "United Kingdom, Ireland"],
    ["WET0WEST,M3.5.0/1,M10.5.0", "Portugal, Canary Islands"],
    ["EET-2EEST,M3.5.0/3,M10.5.0/4", "Eastern Europe (Athens, Helsinki)"],
    ["EST5EDT,M3.2.0,M11.1.0", "US Eastern"],
    ["CST6CDT,M3.2.0,M11.1.0", "US Central"],
    ["MST7MDT,M3.2.0,M11.1.0", "US Mountain"],
    ["PST8PDT,M3.2.0,M11.1.0", "US Pacific"],
    ["AEST-10AEDT,M10.1.0,M4.1.0/3", "Australia East (Sydney)"],
    ["UTC0", "UTC"],
  ];

  const MESSAGES = {
    invalid_credentials: "That password is not right.",
    wrong_current_password: "The current password is not right.",
    too_many_attempts: "Too many wrong passwords. Wait 30 seconds and try again.",
    host_not_allowed: "Open this page through the device name (name.local) or its IP address.",
    invalid_admin_password: "Use 8 to 64 characters for the portal password.",
    invalid_connector_config: "Check the MQTT broker and port.",
    invalid_hostname: "Use 1–32 letters, numbers or hyphens, without a hyphen at either end. Leave empty for the automatic name.",
    hostname_unavailable: "The device could not determine its local address.",
    invalid_state: "The device did not accept that setting.",
    invalid_schedule: "That rule is not valid.",
    ssid_required: "Enter a network name.",
    commissioning_unavailable: "Matter is not running yet. Try again in a moment.",
    matter_not_enabled: "Turn on Matter first.",
    choose_one_integration: "Choose Home Assistant or Matter, not both.",
    firmware_not_opencomfort: "That file is not OpenComfort firmware.",
    firmware_invalid: "The firmware file is damaged or for a different chip.",
    firmware_size_invalid: "That file is too large for this device.",
    firmware_upload_interrupted: "The upload stopped. Try again.",
    network_error: "Could not reach GitHub. Check the internet connection.",
    no_release_found: "No release found yet.",
    release_data_too_large: "GitHub sent more release data than this device can read. Install the new version from a file.",
    no_build_for_this_device: "The newest release has no build for this device.",
    bad_release_data: "GitHub sent something unexpected. Try again later.",
    download_failed: "The download did not start.",
    download_interrupted: "The download stopped. Try again.",
    not_opencomfort_firmware: "The release file is not OpenComfort firmware.",
    updates_disabled: "Online updates are turned off in this build.",
    update_busy: "An update is already running.",
    no_update_available: "There is no update to install.",
    request_failed: "The device did not answer.",
  };

  const CSRF_KEY = "oc.csrf";
  const store = {
    get(key) { try { return sessionStorage.getItem(key) || ""; } catch { return ""; } },
    set(key, value) { try { value ? sessionStorage.setItem(key, value) : sessionStorage.removeItem(key); } catch { /* private mode */ } },
  };

  const app = document.getElementById("app");
  const ui = {
    view: "login",
    csrf: store.get(CSRF_KEY),
    error: "",
    notice: "",
    pollError: "",
    busy: false,
    device: null,
    connectors: null,
    state: null,
    desired: null,
    editedAt: 0,
    history: null,
    range: "24h",
    schedule: null,
    editingRule: null,
    status: null,
    pairing: null,
    pairingTick: null,
    settingsOpen: null,
    form: {},
    upload: null,
    update: null,
    timers: {},
  };

  async function api(path, options = {}) {
    const headers = { "Content-Type": "application/json" };
    if (ui.csrf && options.method && options.method !== "GET") headers["X-OC-CSRF"] = ui.csrf;
    const response = await fetch(path, { credentials: "same-origin", headers, ...options });
    const data = await response.json().catch(() => ({}));
    if ((response.status === 401 || response.status === 403) && path !== "/api/v1/auth/login") signedOut();
    if (!response.ok) throw new Error(MESSAGES[data.error] || data.error || MESSAGES.request_failed);
    return data;
  }

  // Rendering
  function render() {
    const active = document.activeElement;
    const focused = active?.id;
    const caret = active && "selectionStart" in active ? active.selectionStart : null;
    const views = { login: viewLogin, rebooting: viewRebooting, control: viewControl, climate: viewClimate, schedule: viewSchedule, status: viewStatus, settings: viewSettings, matter: viewMatter, logs: viewLogs };
    const listScroll = app.querySelector(".log-list")?.scrollTop || 0;  // keep the reading position
    const content = [views[ui.view]()].flat(Infinity).filter(Boolean);
    if (ui.view === "login" || ui.view === "rebooting") {
      app.className = "shell";
      app.replaceChildren(...content);
    } else {
      app.className = "app";
      app.replaceChildren(sideNav(), h("main", { class: "app-main" }, content), tabBar());
    }
    const again = focused && document.getElementById(focused);
    if (again) {
      again.focus();
      if (caret !== null) try { again.setSelectionRange(caret, caret); } catch { /* not text */ }
    }
    const list = app.querySelector(".log-list");
    if (list && listScroll) list.scrollTop = listScroll;
  }

  function messages() {
    return [
      ui.error ? h("p", { class: "alert", role: "alert" }, ui.error) : null,
      ui.pollError ? h("p", { class: "alert", role: "alert" }, ui.pollError) : null,
      ui.notice ? h("p", { class: "alert ok", role: "status" }, ui.notice) : null,
    ];
  }

  const SECTIONS = [["control", "Control", P.power], ["climate", "Climate", P.climate], ["schedule", "Schedule", P.schedule], ["status", "Status", P.status]];

  function go(view) {
    ui.view = view;
    ui.error = "";
    ui.notice = "";
    ui.editingRule = null;
    ui.settingsOpen = null;
    if (view === "climate") loadHistory();
    if (view === "schedule") loadSchedule();
    if (view === "status") loadStatus();
    if (view === "settings") { openSettings(); loadUpdate(); }
    if (view === "matter") loadPairing();
    if (view === "logs") openLogs();
    startPolling();
    render();
    window.scrollTo(0, 0);
  }

  function tabBar() {
    return h("nav", { class: "tabs", "aria-label": "Sections" },
      SECTIONS.map(([id, label, icon]) => h("button", { class: "tab", type: "button", "aria-current": ui.view === id ? "page" : null, onclick: () => go(id) },
        h("span", { class: "tab-pill", html: svg(icon, 20, 1.8) }), label)));
  }

  function sideNav() {
    return h("aside", { class: "side", "aria-label": ui.device?.friendlyName || "Device" },
      h("div", { class: "side-head" },
        h("span", { class: "side-logo", html: svg(P.ac, 22, 2) }),
        h("span", { class: "grow", style: "display:flex;flex-direction:column;min-width:0" },
          h("strong", { style: "font-size:16px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap" }, ui.device?.friendlyName || ""),
          h("span", { class: "sub" }, ui.device?.model || "Air conditioner"))),
      [...SECTIONS, ["settings", "Settings", P.settings]].map(([id, label, icon]) =>
        h("button", { class: "nav", type: "button", "aria-current": ui.view === id || (id === "settings" && (ui.view === "matter" || ui.view === "logs")) ? "page" : null, onclick: () => go(id), html: `${svg(icon, 20, 1.8)}<span></span>` }, label)));
  }

  function pageHeader(title, sub, extra) {
    return h("header", { class: "top" },
      h("div", { class: "grow" }, h("h1", {}, title), sub ? h("p", { class: "sub" }, sub) : null),
      extra || null);
  }

  function settingsButton() {
    return h("button", { class: "icon-btn hide-wide", type: "button", "aria-label": "Settings", onclick: () => go("settings"), html: svg(P.settings, 20, 1.8) });
  }

  // Polling: only what the open view shows, and nothing while the tab is hidden
  function startPolling() {
    clearInterval(ui.timers.poll);
    if (document.hidden || !ui.device) return;
    const every = { control: [refreshState, 2000], climate: [refreshState, 5000], status: [loadStatus, 5000], logs: [pollLogs, 2000] }[ui.view];
    if (every) ui.timers.poll = setInterval(every[0], every[1]);
  }
  document.addEventListener("visibilitychange", () => {
    if (!ui.device) return;
    if (!document.hidden && (ui.view === "control" || ui.view === "climate")) refreshState();
    startPolling();
  });

  // Sign in
  function viewLogin() {
    return [
      h("span", { class: "badge", html: svg(P.lock, 24, 2.2), style: "margin-top:40px" }),
      h("p", { class: "eyebrow" }, "OPENCOMFORT"),
      h("h1", { style: "font-size:28px" }, "Sign in"),
      h("p", { class: "lede" }, "Enter the portal password you chose during setup."),
      h("form", { style: "display:contents", onsubmit: (e) => { e.preventDefault(); login(); } },
        h("div", { class: "field", style: "margin-top:24px" },
          h("span", { class: "grow" }, h("label", { for: "password" }, "Portal password"),
            h("input", { id: "password", type: "password", autocomplete: "current-password", required: true }))),
        h("label", { class: "row", style: "padding:0;min-height:44px;margin-top:8px" },
          h("span", { class: "grow", style: "font-size:15px" }, "Keep me signed in"),
          h("input", { id: "remember", class: "switch", type: "checkbox", checked: true })),
        h("p", { class: "note" }, "Forgot it? Switch the power off and on 5 times in a row, each time within 10 seconds. That resets the device and starts setup again."),
        ui.error ? h("p", { class: "alert", role: "alert", style: "margin-top:12px" }, ui.error) : null,
        h("div", { class: "spacer" }),
        h("button", { class: "btn block", type: "submit", style: "margin-top:16px", disabled: ui.busy }, ui.busy ? "Signing in…" : "Sign in")),
    ];
  }

  async function login() {
    ui.busy = true;
    ui.error = "";
    const password = document.getElementById("password").value;
    const remember = document.getElementById("remember").checked;
    render();
    try {
      const result = await api("/api/v1/auth/login", { method: "POST", body: JSON.stringify({ password, remember }) });
      ui.csrf = result.csrfToken;
      store.set(CSRF_KEY, ui.csrf);
      await openDashboard();
    } catch (error) {
      ui.error = error.message;
    }
    ui.busy = false;
    render();
  }

  function signedOut() {
    ui.csrf = "";
    store.set(CSRF_KEY, "");
    // A restart (update, settings) may have brought a new UI with the new firmware.
    if (ui.view !== "login") { location.reload(); return; }
    ui.device = null;
    Object.values(ui.timers).forEach((t) => { clearInterval(t); clearTimeout(t); });
    ui.view = "login";
    render();
  }

  async function openDashboard() {
    const [device, connectors] = await Promise.all([api("/api/v1/device"), api("/api/v1/connectors")]);
    ui.device = device;
    ui.connectors = connectors;
    document.title = device.friendlyName;
    await refreshState();
    loadSchedule();
    ui.view = "control";
    startPolling();
  }

  async function refreshState() {
    try {
      ui.state = await api("/api/v1/state");
      ui.pollError = "";
      // Don't let a poll overwrite what the user is changing right now.
      if (Date.now() - ui.editedAt > 2500) ui.desired = { ...ui.state.desired };
      if (ui.view === "control" || ui.view === "climate") render();
    } catch {
      if (!ui.device) return;
      ui.pollError = "Lost contact with the device. Retrying…";
      if (ui.view === "control") render();
    }
  }

  // Control
  function change(patch, delay = 0) {
    ui.desired = { ...ui.desired, ...patch };
    ui.editedAt = Date.now();
    render();
    clearTimeout(ui.timers.send);
    ui.timers.send = setTimeout(async () => {
      try {
        await api("/api/v1/state", { method: "PUT", body: JSON.stringify(ui.desired) });
        ui.error = "";
      } catch (error) {
        ui.error = error.message;
        render();
      }
    }, delay);
  }

  function fmt(temp) {
    return `${Number(temp).toFixed(1).replace(/\.0$/, "")}°`;
  }

  function viewControl() {
    const s = ui.state, d = ui.desired;
    if (!s || !d) return [pageHeader("Loading…")];
    const min = s.capabilities.minTempC, max = s.capabilities.maxTempC;
    const on = d.powerOn && d.mode !== "off";
    const set = Math.round(d.targetTempC);
    const room = s.reported.returnAirC !== undefined ? fmt(s.reported.returnAirC) : "—";
    const reported = s.reported.setTempC !== undefined ? fmt(s.reported.setTempC) : "—";
    const heroMode = on ? d.mode : "off";
    let status = on ? STATUS[d.mode] || "On" : "Off";
    if (on && ACTION[s.action] && s.action !== "off") status = ACTION[s.action];
    if (!s.available) status = "Not responding";

    const hero = h("section", { class: `hero ${heroMode}`, "aria-label": "Temperature" },
      h("div", { class: "hero-top" },
        h("span", { class: "pill", html: `${svg(on ? MODE_ICON[d.mode] || P.power : P.power, 16, 2)}<span></span>` }, status),
        h("button", { class: "power", type: "button", "aria-label": on ? "Turn off" : "Turn on", "aria-pressed": String(on), html: svg(P.power, 20, 2.2),
          onclick: () => change({ powerOn: !on, mode: d.mode === "off" ? "auto" : d.mode }) })),
      on ? h("div", { class: "setpoint" },
        h("button", { class: "step", type: "button", "aria-label": "One degree cooler", disabled: set <= min, html: svg(P.minus, 20, 2.4), onclick: () => change({ targetTempC: set - 1 }, 600) }),
        h("div", {},
          h("p", { class: "big", "aria-live": "polite" }, String(set), h("sup", {}, "°")),
          h("p", { class: "big-label" }, "Target")),
        h("button", { class: "step", type: "button", "aria-label": "One degree warmer", disabled: set >= max, html: svg(P.plus, 20, 2.4), onclick: () => change({ targetTempC: set + 1 }, 600) }))
        : h("div", { class: "offstate" },
          h("p", { class: "big" }, "Off"),
          h("p", { class: "sub", style: "margin:8px 0 16px;font-size:14px" }, `Last set to ${(STATUS[d.mode] || "auto").toLowerCase()} at ${set}°`),
          h("button", { class: "btn dark block", type: "button", onclick: () => change({ powerOn: true, mode: d.mode === "off" ? "auto" : d.mode }) }, "Turn on")),
      h("div", { class: "stats" },
        h("div", {}, h("span", {}, "Room now"), h("strong", {}, room)),
        h("div", {}, h("span", {}, "Unit reports"), h("strong", {}, reported))));

    return [
      pageHeader(ui.device.friendlyName, ui.device.model || "Air conditioner", settingsButton()),
      ...messages(),
      h("div", { class: "wide-grid" },
        hero,
        h("section", { class: on ? null : "dim", "aria-labelledby": "mode-h" },
          h("h2", { id: "mode-h", class: "section-title" }, "Mode"),
          h("div", { class: "tiles" }, MODES.map(([value, label]) => h("button", {
            class: `tile ${value}`, type: "button", "aria-pressed": String(on && d.mode === value),
            onclick: () => change({ mode: value, powerOn: true }), html: `${svg(MODE_ICON[value], 22)}<span></span>`,
          }, label)))),
        h("section", { class: on ? null : "dim", "aria-labelledby": "fan-h" },
          h("h2", { id: "fan-h", class: "section-title" }, "Fan speed"),
          h("div", { class: "segments" }, FANS.map(([value, label]) => h("button", { type: "button", "aria-pressed": String(d.fan === value), onclick: () => change({ fan: value }) }, label)))),
        s.capabilities.vanes?.length ? h("section", { class: on ? null : "dim", "aria-labelledby": "air-h" },
          h("h2", { id: "air-h", class: "section-title" }, "Airflow"),
          h("div", { class: "segments small", style: `grid-auto-columns:minmax(0,1fr)` }, s.capabilities.vanes.map((name) =>
            h("button", { type: "button", "aria-pressed": String(d.vanes === name), "aria-label": VANE_LABEL(name), onclick: () => change({ vanes: name }) }, /^\d+$/.test(name) ? name : VANE_LABEL(name))))) : null,
        nextRow()),
    ];
  }

  // The next thing the schedule will do, if anything.
  function nextAction() {
    const sch = ui.schedule;
    if (!sch) return null;
    if (sch.sleepSecondsLeft > 0) return { title: `Turns off in ${duration(sch.sleepSecondsLeft)}`, sub: "Sleep timer" };
    if (!sch.timeValid || !sch.now) return null;
    const [nh, nm] = sch.now.split(":").map(Number);
    const nowMin = nh * 60 + nm;
    let best = null;
    for (const r of sch.rules) {
      if (!r.enabled) continue;
      const [rh, rm] = r.time.split(":").map(Number);
      for (let ahead = 0; ahead < 8; ahead++) {
        const day = (sch.weekday + ahead) % 7;
        if (!(r.days & (1 << day))) continue;
        const delta = ahead * 1440 + rh * 60 + rm - nowMin;
        if (delta <= 0) continue;
        if (!best || delta < best.delta) best = { delta, rule: r, ahead };
        break;
      }
    }
    if (!best) return null;
    const what = best.rule.action === "off" ? "Turns off" : `${STATUS[best.rule.mode] || "On"} to ${best.rule.targetTempC}°`;
    const when = best.ahead === 0 ? `at ${best.rule.time}` : best.ahead === 1 ? `tomorrow ${best.rule.time}` : `${DAYS[(sch.weekday + best.ahead) % 7]} ${best.rule.time}`;
    return { title: `${what} ${when}`, sub: "Next in your schedule" };
  }

  function nextRow() {
    const next = nextAction();
    return h("button", { class: "next", type: "button", onclick: () => go("schedule") },
      h("span", { class: "ic warn", html: svg(P.schedule, 18, 2) }),
      h("span", { class: "grow", style: "display:flex;flex-direction:column" },
        h("span", { style: "font-size:15px;font-weight:500" }, next ? next.title : "No timers set"),
        h("span", { class: "sub" }, next ? next.sub : "Plan when it turns on or off")),
      h("span", { html: svg(P.chevron, 16, 2.2), style: "color:var(--faint);display:flex" }));
  }

  function duration(seconds) {
    const m = Math.ceil(seconds / 60);
    return m >= 60 ? `${Math.floor(m / 60)} h ${m % 60 ? `${m % 60} min` : ""}`.trim() : `${m} min`;
  }

  // Climate
  async function loadHistory() {
    try {
      ui.history = await api("/api/v1/history");
    } catch (error) {
      ui.error = error.message;
    }
    if (ui.view === "climate") render();
  }

  function viewClimate() {
    const s = ui.state;
    const target = ui.desired && ui.desired.powerOn && ui.desired.mode !== "off" ? Math.round(ui.desired.targetTempC) : null;
    const nowCard = h("section", { class: "card now-card", "aria-label": "Room temperature now" },
      h("span", { class: "ic", html: svg(P.climate, 26) }),
      h("div", { class: "grow", style: "flex:1 1 auto" },
        h("span", { class: "sub", style: "display:block;margin:0" }, "Room now"),
        h("span", { class: "now-value" }, s?.reported.returnAirC !== undefined ? fmt(s.reported.returnAirC) : "—")),
      target !== null ? h("span", { class: "tag", style: "align-self:flex-start" }, `${STATUS[ui.desired.mode]} to ${target}°`) : null);

    return [
      pageHeader("Climate", `${ui.device.friendlyName} · readings every 2 s`),
      ...messages(),
      h("div", { class: "wide-two" },
        h("div", { class: "page" }, nowCard, h("section", { class: "card", style: "padding:4px 16px" },
          h("div", { class: "row", style: "padding:0" }, h("span", { class: "grow" }, "Unit set point"), h("strong", {}, s?.reported.setTempC !== undefined ? fmt(s.reported.setTempC) : "—")),
          s?.action && s.action !== "unknown" ? h("div", { class: "row", style: "padding:0;box-shadow:inset 0 1px 0 var(--hairline)" }, h("span", { class: "grow" }, "Unit is"), h("strong", {}, ACTION[s.action] || s.action)) : null,
          s?.reported.mode ? h("div", { class: "row", style: "padding:0;box-shadow:inset 0 1px 0 var(--hairline)" }, h("span", { class: "grow" }, "Unit mode"), h("strong", {}, STATUS[s.reported.mode] || s.reported.mode)) : null,
          s?.reported.fan ? h("div", { class: "row", style: "padding:0;box-shadow:inset 0 1px 0 var(--hairline)" }, h("span", { class: "grow" }, "Unit fan"), h("strong", {}, s.reported.fan.charAt(0).toUpperCase() + s.reported.fan.slice(1))) : null,
          h("div", { class: "row", style: "padding:0;box-shadow:inset 0 1px 0 var(--hairline)" }, h("span", { class: "grow" }, "Unit answering"), h("span", { class: s?.reported.valid ? "state ok" : "state" }, s?.reported.valid ? "Yes" : "No")))),
        chartCard(target)),
    ];
  }

  function chartCard(target) {
    const all = ui.history?.samples || [];
    const raw = ui.range === "1h" ? all.slice(-60) : all;
    // 24 h: average blocks of 5 minutes to keep the line readable.
    const step = ui.range === "1h" ? 1 : 5;
    const data = [];
    for (let i = 0; i < raw.length; i += step) {
      const block = raw.slice(i, i + step).filter((v) => v !== null);
      data.push(block.length ? block.reduce((a, b) => a + b, 0) / block.length : null);
    }
    const values = data.filter((v) => v !== null);
    const seg = (id, label) => h("button", { type: "button", "aria-pressed": String(ui.range === id), onclick: () => { ui.range = id; render(); } }, label);
    const head = h("div", { class: "top", style: "margin-bottom:12px" },
      h("h2", { class: "grow", style: "font-size:15px;color:var(--ink)" }, "Room temperature"),
      h("div", { class: "segments track small", role: "group", "aria-label": "Range" }, seg("1h", "1 hour"), seg("24h", "24 hours")));
    if (values.length < 2) {
      return h("section", { class: "card chart" }, head, h("p", { class: "empty" }, "Collecting readings. The chart fills in over the next minutes."));
    }
    let lo = Math.min(...values, target ?? Infinity), hi = Math.max(...values, target ?? -Infinity);
    lo = Math.floor(lo / 2) * 2 - 1;
    hi = Math.ceil(hi / 2) * 2 + 1;
    const W = 326, top = 10, bottom = 130, x0 = 30;
    const y = (t) => bottom - ((t - lo) / (hi - lo)) * (bottom - top);
    const x = (i) => x0 + (i / Math.max(1, data.length - 1)) * (W - x0);
    let line = "", started = false;
    data.forEach((v, i) => {
      if (v === null) { started = false; return; }
      line += `${started ? "L" : "M"}${x(i).toFixed(1)} ${y(v).toFixed(1)}`;
      started = true;
    });
    const first = data.findIndex((v) => v !== null);
    const lastIndex = data.length - 1 - [...data].reverse().findIndex((v) => v !== null);
    const area = `${line}L${x(lastIndex).toFixed(1)} ${bottom}L${x(first).toFixed(1)} ${bottom}Z`;
    const mid = Math.round((lo + hi) / 2);
    const minutes = raw.length;
    const label = (m) => (ui.range === "1h" ? `−${m} min` : `−${Math.round(m / 60)} h`);
    const chart = `<svg viewBox="0 0 ${W} 160" role="img" aria-label="Room temperature, last ${ui.range === "1h" ? "hour" : "24 hours"}: from ${fmt(Math.min(...values))} to ${fmt(Math.max(...values))}.">
      <path class="chart-grid" d="M${x0} ${y(hi - 1)}H${W}M${x0} ${y(mid)}H${W}M${x0} ${y(lo + 1)}H${W}"/>
      <text class="chart-axis" x="0" y="${y(hi - 1) + 4}">${hi - 1}°</text>
      <text class="chart-axis" x="0" y="${y(mid) + 4}">${mid}°</text>
      <text class="chart-axis" x="0" y="${y(lo + 1) + 4}">${lo + 1}°</text>
      ${target !== null ? `<path class="chart-target" d="M${x0} ${y(target)}H${W}"/><text class="chart-axis" x="${W}" y="${y(target) - 6}" text-anchor="end">Target ${target}°</text>` : ""}
      <path class="chart-area" d="${area}"/>
      <path class="chart-line" d="${line}"/>
      <circle cx="${x(lastIndex)}" cy="${y(data[lastIndex])}" r="5" fill="var(--cool)" stroke="var(--panel)" stroke-width="2"/>
      <text class="chart-axis" x="${x0}" y="154">${label(minutes)}</text>
      <text class="chart-axis" x="${W}" y="154" text-anchor="end">Now</text>
    </svg>`;
    return h("section", { class: "card chart" }, head, h("div", { html: chart }),
      h("div", { class: "trio" },
        h("div", {}, h("span", {}, "Lowest"), h("strong", {}, fmt(Math.min(...values)))),
        h("div", {}, h("span", {}, "Highest"), h("strong", {}, fmt(Math.max(...values)))),
        h("div", {}, h("span", {}, "Now"), h("strong", {}, fmt(values[values.length - 1])))));
  }

  // Schedule
  async function loadSchedule() {
    try {
      ui.schedule = await api("/api/v1/schedule");
    } catch (error) {
      if (ui.view === "schedule") ui.error = error.message;
    }
    if (ui.view === "schedule" || ui.view === "control") render();
  }

  async function saveSchedule(patch) {
    ui.busy = true;
    render();
    try {
      ui.schedule = await api("/api/v1/schedule", { method: "PUT", body: JSON.stringify({ timezone: ui.schedule.timezone, rules: ui.schedule.rules, ...patch }) });
      ui.editingRule = null;
      ui.error = "";
    } catch (error) {
      ui.error = error.message;
    }
    ui.busy = false;
    render();
  }

  async function setSleep(minutes) {
    try {
      ui.schedule = await api("/api/v1/schedule/sleep", { method: "POST", body: JSON.stringify({ minutes }) });
    } catch (error) {
      ui.error = error.message;
    }
    render();
  }

  function daysText(mask) {
    if (mask === 0x7f) return "Every day";
    if (mask === 0x1f) return "Monday to Friday";
    if (mask === 0x60) return "Weekends";
    return DAYS.filter((_, i) => mask & (1 << i)).join(", ");
  }

  function ruleText(r) {
    return r.action === "off" ? "Turn off" : `${STATUS[r.mode] || "On"} to ${r.targetTempC}°`;
  }

  function timeline(sch) {
    if (!sch.timeValid) return null;
    const today = sch.weekday;
    const toPct = (t) => { const [a, b] = t.split(":").map(Number); return ((a * 60 + b) / 1440) * 100; };
    const events = sch.rules.filter((r) => r.enabled && r.days & (1 << today)).sort((a, b) => a.time.localeCompare(b.time));
    const blocks = [];
    let onAt = null;
    for (const r of events) {
      if (r.action === "on" && onAt === null) onAt = toPct(r.time);
      if (r.action === "off" && onAt !== null) { blocks.push([onAt, toPct(r.time)]); onAt = null; }
    }
    if (onAt !== null) blocks.push([onAt, 100]);
    return h("section", { class: "card", style: "padding:16px" },
      h("h2", { style: "margin-bottom:12px;color:var(--ink)" }, "Today"),
      h("div", { class: "timeline", "aria-hidden": "true" },
        blocks.map(([a, b]) => h("span", { style: `left:${a}%;width:${b - a}%` })),
        h("span", { class: "now", style: `left:${toPct(sch.now)}%` })),
      h("div", { class: "mono", "aria-hidden": "true", style: "display:flex;justify-content:space-between;margin-top:6px;font-size:11px;color:var(--muted)" },
        ["00", "06", "12", "18", "24"].map((t) => h("span", {}, t))),
      h("p", { class: "sub", style: "margin-top:8px" }, nextAction()?.title || "Nothing planned for today"));
  }

  function viewSchedule() {
    const sch = ui.schedule;
    if (!sch) return [pageHeader("Schedule"), ...messages()];
    const sleepOptions = [[0, "Off"], [30, "30 min"], [60, "1 h"], [120, "2 h"], [240, "4 h"]];
    const sleepActive = sch.sleepSecondsLeft > 0;
    const rules = sch.rules.map((r, i) => ui.editingRule?.index === i ? ruleEditor() : h("div", { class: `row rule${r.enabled ? "" : " off"}`, style: "min-height:64px" },
      h("button", { type: "button", style: "all:unset;flex:1 1 auto;display:flex;align-items:center;gap:14px;cursor:pointer", onclick: () => { ui.editingRule = { index: i, rule: { ...r } }; render(); }, "aria-label": `Edit ${r.time} ${ruleText(r)}` },
        h("span", { class: "rule-time" }, r.time),
        h("span", { class: "grow" }, h("span", { style: "font-weight:500" }, ruleText(r)), h("small", {}, daysText(r.days)))),
      h("input", { class: "switch", type: "checkbox", checked: r.enabled, "aria-label": `${r.time} ${ruleText(r)}`, onchange: (e) => {
        const next = sch.rules.map((x, j) => (j === i ? { ...x, enabled: e.target.checked } : x));
        saveSchedule({ rules: next });
      } })));
    if (ui.editingRule?.index === -1) rules.push(ruleEditor());

    return [
      pageHeader("Schedule", "Runs on the device, even when this page is closed"),
      ...messages(),
      !sch.timeValid ? h("p", { class: "alert", role: "status" }, "The device does not know the time yet. Timed rules start once it reaches the internet clock; the sleep timer works now.") : null,
      h("div", { class: "wide-two" },
        h("div", { class: "page" },
          timeline(sch),
          h("section", { class: "card", style: "padding:16px" },
            h("div", { class: "top", style: "margin-bottom:12px" },
              h("h2", { class: "grow", style: "color:var(--ink)" }, "Sleep timer"),
              sleepActive ? h("span", { class: "tag" }, `Off in ${duration(sch.sleepSecondsLeft)}`) : null),
            h("div", { class: "segments track small" }, sleepOptions.map(([m, label]) => h("button", { type: "button", "aria-pressed": String(m === 0 ? !sleepActive : false), onclick: () => setSleep(m) }, label))))),
        h("div", { class: "page" },
          h("section", {},
            h("h2", { class: "section-title" }, "Rules"),
            rules.length ? h("div", { class: "list" }, rules) : h("div", { class: "card empty" }, "No rules yet. Add one to turn the air conditioner on or off at set times.")),
          sch.rules.length < 8 && !ui.editingRule ? h("button", { class: "btn block", type: "button", onclick: () => { ui.editingRule = { index: -1, rule: { enabled: true, time: "07:00", days: 0x1f, action: "on", mode: "cool", targetTempC: 21 } }; render(); } }, "Add rule") : null,
          h("div", { class: "field" }, h("span", { class: "grow" },
            h("label", { for: "timezone" }, "Time zone"),
            h("select", { id: "timezone", onchange: (e) => saveSchedule({ timezone: e.target.value }) },
              TIMEZONES.map(([tz, label]) => h("option", { value: tz, selected: tz === sch.timezone }, label))))))),
    ];
  }

  function ruleEditor() {
    const edit = ui.editingRule;
    const r = edit.rule;
    const set = (patch) => { Object.assign(r, patch); render(); };
    return h("div", { class: "editor", style: "background:var(--panel)" },
      h("div", { class: "field filled" }, h("span", { class: "grow" },
        h("label", { for: "ruleTime" }, "Time"),
        h("input", { id: "ruleTime", type: "time", value: r.time, oninput: (e) => { r.time = e.target.value; } }))),
      h("div", { class: "days", role: "group", "aria-label": "Days" }, DAYS.map((d, i) => h("button", { type: "button", "aria-pressed": String(Boolean(r.days & (1 << i))), onclick: () => set({ days: r.days ^ (1 << i) }) }, d))),
      h("div", { class: "segments track small" },
        h("button", { type: "button", "aria-pressed": String(r.action === "on"), onclick: () => set({ action: "on" }) }, "Turn on"),
        h("button", { type: "button", "aria-pressed": String(r.action === "off"), onclick: () => set({ action: "off" }) }, "Turn off")),
      r.action === "on" ? [
        h("div", { class: "segments track small" }, MODES.map(([value, label]) => h("button", { type: "button", "aria-pressed": String(r.mode === value), onclick: () => set({ mode: value }) }, label))),
        h("div", { class: "top" },
          h("span", { class: "grow" }, "Temperature"),
          h("button", { class: "icon-btn", type: "button", "aria-label": "Cooler", html: svg(P.minus, 18, 2.4), onclick: () => set({ targetTempC: Math.max(16, r.targetTempC - 1) }) }),
          h("strong", { style: "width:48px;text-align:center;font-size:20px" }, `${r.targetTempC}°`),
          h("button", { class: "icon-btn", type: "button", "aria-label": "Warmer", html: svg(P.plus, 18, 2.4), onclick: () => set({ targetTempC: Math.min(30, r.targetTempC + 1) }) })),
      ] : null,
      h("div", { class: "actions", style: "margin-top:4px" },
        edit.index >= 0 ? h("button", { class: "btn quiet", type: "button", style: "color:var(--danger)", onclick: () => saveSchedule({ rules: ui.schedule.rules.filter((_, j) => j !== edit.index) }) }, "Delete") : null,
        h("button", { class: "btn quiet", type: "button", onclick: () => { ui.editingRule = null; render(); } }, "Cancel"),
        h("button", { class: "btn", type: "button", disabled: !r.days || ui.busy, onclick: () => {
          const rules = [...ui.schedule.rules];
          if (edit.index >= 0) rules[edit.index] = r; else rules.push(r);
          rules.sort((a, b) => a.time.localeCompare(b.time));
          saveSchedule({ rules });
        } }, "Save")));
  }

  // Status
  async function loadStatus() {
    try {
      ui.status = await api("/api/v1/status");
    } catch (error) {
      if (ui.view === "status") ui.error = error.message;
    }
    if (ui.view === "status") render();
  }

  function uptime(seconds) {
    const d = Math.floor(seconds / 86400), hrs = Math.floor((seconds % 86400) / 3600), m = Math.floor((seconds % 3600) / 60);
    return d ? `${d} d ${hrs} h` : hrs ? `${hrs} h ${m} min` : `${m} min`;
  }

  function viewStatus() {
    const st = ui.status;
    if (!st) return [pageHeader("Status", null, settingsButton()), ...messages()];
    const conn = (id) => st.connectors.find((c) => c.id === id);
    const stateOf = (c, enabled) => !enabled ? ["Off", ""] : c?.state === "running" ? ["OK", "ok"] : c?.state === "starting" ? ["Starting", ""] : ["Problem", "warn"];
    const mqtt = stateOf(conn("home_assistant_mqtt") || conn("mqtt"), ui.connectors?.mqttEnabled);
    const matter = stateOf(conn("matter"), ui.connectors?.matterEnabled);
    const rssi = st.wifi.rssi;
    const signalWord = rssi === undefined ? "" : rssi >= -60 ? "good" : rssi >= -72 ? "fair" : "weak";
    const issues = [];
    if (!st.ac.answering) {
      const probe = (st.ac.clockProbe || []).filter((p) => p.edges >= 0).map((p) => `GPIO${p.pin}: ${p.edges}`).join(", ");
      if (st.ac.clockBursts === 0) issues.push(`No AC clock bursts detected on GPIO${st.ac.sckPin} since startup.` +
        (probe ? ` Pulses at startup: ${probe}. With 0 on every pin no clock reaches the board: check the cable and that the indoor unit has power.` : ""));
      else if (st.ac.clockBursts > 0) issues.push(`Detected ${st.ac.clockBursts} clock bursts since startup. No current valid AC frame.`);
      else issues.push("The air conditioner is not answering.");
    }
    if (mqtt[1] === "warn") issues.push("Home Assistant is not connected.");
    if (matter[1] === "warn") issues.push("Matter is not running.");
    const row = (icon, tint, label, detail, [word, cls]) => h("div", { class: "row" },
      h("span", { class: `ic ${tint}`, html: svg(icon, 18) }),
      h("span", { class: "grow" }, h("span", {}, label), h("small", { class: "name" }, detail)),
      h("span", { class: `state ${cls}` }, word));
    const group = (title, ...rows) => h("section", {}, h("h2", { class: "section-title" }, title), h("div", { class: "list" }, rows));

    return [
      pageHeader("Status", `${ui.device.friendlyName} · up ${uptime(st.device.uptimeS)}`, settingsButton()),
      ...messages(),
      h("section", { class: issues.length ? "summary warn" : "summary", "aria-live": "polite" },
        h("span", { class: "dot-big", html: svg(issues.length ? P.alert : P.check, 22, 2.6) }),
        h("span", { style: "display:flex;flex-direction:column" },
          h("strong", { style: "font-size:17px" }, issues.length ? "Needs attention" : "Everything works"),
          h("span", { style: "font-size:14px" }, issues.length ? issues.join(" ") : "The air conditioner is answering."))),
      h("div", { class: "wide-two" },
        group("Network",
          row(P.wifi, "", "Wi‑Fi", st.wifi.ssid ? `${st.wifi.ssid} · signal ${signalWord} (${rssi} dBm)` : "Not connected", st.wifi.ssid ? ["OK", "ok"] : ["Problem", "warn"]),
          row(P.globe, "", "Address", `${st.wifi.hostname}${st.wifi.ip ? ` · ${st.wifi.ip}` : ""}`, ["OK", "ok"])),
        group("Smart home",
          ui.connectors?.mqttSupported !== false ? row(P.home, "ok", "Home Assistant", ui.connectors?.mqttEnabled ? ui.connectors.mqttHost : "Turned off in Settings", mqtt) : null,
          ui.connectors?.matterSupported ? row(P.matter, "gray", "Matter", ui.connectors.matterEnabled ? "Apple Home, Google Home and others" : "Turned off in Settings", matter) : null),
        group("Air conditioner",
          row(P.bus, "warn", "Connection", `${String(st.ac.bus).toUpperCase()} · ${st.ac.answering ? "answering" : "no answer"}`, st.ac.answering ? ["OK", "ok"] : ["Problem", "warn"]),
          row(P.bus, "warn", "Last command", !st.ac.lastRequestId ? "No command yet" : st.ac.lastApplyError === "ESP_OK" ? `Sent, from ${st.ac.lastCommandSource.replace("_", " ")}` : `Failed: ${st.ac.lastApplyError}`, !st.ac.lastRequestId ? ["Idle", ""] : st.ac.lastApplyError === "ESP_OK" ? ["OK", "ok"] : ["Problem", "warn"])),
        group("Device",
          row(P.chip, "gray", "Firmware", `Version ${st.device.firmwareVersion}`, ["OK", "ok"]),
          row(P.chip, "gray", "Memory", `${Math.round(st.device.freeHeap / 1024)} KB free`, st.device.freeHeap > 30000 ? ["OK", "ok"] : ["Low", "warn"]))),
    ];
  }

  // Settings
  function openSettings() {
    const c = ui.connectors;
    ui.form = {
      friendlyName: ui.device.friendlyName, adminPassword: "", currentPassword: "", wifiSsid: "", wifiPassword: "",
      hostname: ui.device.hostnameOverride || "",
      mqttEnabled: c.mqttEnabled, mqttHost: c.mqttHost || "homeassistant.local", mqttPort: c.mqttPort, mqttTls: c.mqttTls,
      mqttUsername: "", mqttPassword: "", matterEnabled: c.matterEnabled, integration: null,
    };
  }

  function bindForm(id, extra = {}) {
    return { id, value: ui.form[id], oninput: (e) => { ui.form[id] = e.target.value; }, ...extra };
  }

  function formField(id, label, extra) {
    return h("div", { class: "field filled" }, h("span", { class: "grow" }, h("label", { for: id }, label), h("input", bindForm(id, extra))));
  }

  function settingsRow(key, icon, tint, label, value, body) {
    const open = ui.settingsOpen === key;
    return h("div", {},
      h("button", { class: "row", type: "button", "aria-expanded": String(open), onclick: () => { ui.settingsOpen = open ? null : key; ui.error = ""; render(); } },
        h("span", { class: `ic ${tint}`, html: svg(icon, 18) }),
        h("span", { class: "grow" }, label),
        h("span", { class: "value" }, value)),
      open ? h("div", { class: "net-open" }, body()) : null);
  }

  function viewSettings() {
    const c = ui.connectors;
    const f = ui.form;
    const saveBtn = (label, payload) => h("button", { class: "btn block", type: "button", style: "height:48px;border-radius:12px", disabled: ui.busy, onclick: () => saveConnectors(payload()) }, label);
    return [
      h("header", { class: "top" },
        h("button", { class: "icon-btn hide-wide", type: "button", "aria-label": "Back", onclick: () => go("control"), html: svg(P.back, 18, 2.2) }),
        h("h1", { class: "grow" }, "Settings")),
      ...messages(),
      h("div", { class: "wide-two" },
        h("div", { class: "page" },
          h("section", {}, h("h2", { class: "section-title" }, "This device"), h("div", { class: "list" },
            settingsRow("name", P.home, "", "Name", ui.device.friendlyName, () => [
              formField("friendlyName", "Device name", { maxlength: 32 }),
              saveBtn("Save and restart", () => ({ friendlyName: f.friendlyName.trim() })),
            ]),
            settingsRow("hostname", P.globe, "", "Local hostname", `${ui.device.hostname}.local`, () => [
              formField("hostname", "Hostname", { maxlength: 38, autocomplete: "off", autocapitalize: "none", spellcheck: false, placeholder: "airco-woonkamer", "aria-describedby": "hostname-help" }),
              h("p", { id: "hostname-help", class: "note", style: "margin:0" }, "Open this device at hostname.local. Use a unique name on your network. Leave empty to restore its automatic name. Saving restarts the device."),
              saveBtn("Save and restart", () => ({ hostname: f.hostname.trim() })),
            ]),
            settingsRow("password", P.lock, "", "Portal password", "Change", () => [
              formField("currentPassword", "Current password", { type: "password", maxlength: 64, autocomplete: "current-password" }),
              formField("adminPassword", "New password · at least 8 characters", { type: "password", minlength: 8, maxlength: 64, autocomplete: "new-password" }),
              saveBtn("Save and restart", () => ({ adminPassword: f.adminPassword, currentPassword: f.currentPassword })),
            ]),
            settingsRow("wifi", P.wifi, "", "Wi‑Fi", ui.status?.wifi?.ssid || "Change", () => [
              formField("wifiSsid", "Network name", { maxlength: 32, autocomplete: "off" }),
              formField("wifiPassword", "Password · empty if open", { type: "password", maxlength: 64, autocomplete: "new-password" }),
              h("p", { class: "note", style: "margin:0" }, "The device restarts and joins the new network. If it cannot connect, switch the power off and on 5 times to set it up again."),
              h("button", { class: "btn block", type: "button", style: "height:48px;border-radius:12px", disabled: ui.busy, onclick: saveWifi }, "Switch network"),
            ]))),
          smartHomeSection(),
        ),
        h("div", { class: "page" },
          ui.device.capabilities.ota ? firmwareCard() : null,
          h("section", {}, h("h2", { class: "section-title" }, "Support"), h("div", { class: "list" },
            h("button", { class: "row", type: "button", onclick: () => go("logs") },
              h("span", { class: "ic gray", html: svg("M8 6h12M8 12h12M8 18h8M4 6h.01M4 12h.01M4 18h.01", 18) }),
              h("span", { class: "grow" }, h("span", {}, "Logs"), h("small", {}, "What the device did since the last restart")),
              h("span", { html: svg(P.chevron, 16, 2.2), style: "color:var(--faint);display:flex" })))),
          h("section", {}, h("div", { class: "list" }, h("button", { class: "row", type: "button", style: "justify-content:center;font-weight:600", onclick: logout }, "Sign out"))))),
    ];
  }

  // Online updates from GitHub releases
  async function loadUpdate() {
    try {
      ui.update = await api("/api/v1/update");
    } catch {
      ui.update = null;
    }
    // Poll while the device is checking or downloading.
    clearTimeout(ui.timers.update);
    if (ui.update && ["checking", "downloading", "restarting"].includes(ui.update.state)) {
      if (ui.update.state === "restarting") {
        rebooting("Installing update", `Restarting with ${ui.update.latest}. This page reconnects when it is back.`);
        return;
      }
      ui.timers.update = setTimeout(loadUpdate, ui.update.state === "downloading" ? 1000 : 1500);
    }
    if (ui.view === "settings") render();
  }

  async function updateAction(path, body) {
    try {
      ui.update = await api(path, { method: body ? "PUT" : "POST", body: JSON.stringify(body || {}) });
      ui.error = "";
    } catch (error) {
      ui.error = error.message;
    }
    loadUpdate();
  }

  function ago(seconds) {
    if (seconds < 0) return "never";
    if (seconds < 60) return "just now";
    if (seconds < 3600) return `${Math.round(seconds / 60)} min ago`;
    if (seconds < 86400) return `${Math.round(seconds / 3600)} h ago`;
    return `${Math.round(seconds / 86400)} d ago`;
  }

  function updateBlock() {
    const u = ui.update;
    if (!u || !u.enabled) return null;
    const busy = u.state === "checking" || u.state === "downloading";
    let line;
    if (u.state === "checking") line = h("p", { class: "sub", style: "margin:0" }, "Checking GitHub…");
    else if (u.state === "downloading") line = [
      h("div", { class: "meter", role: "progressbar", "aria-valuemin": 0, "aria-valuemax": 100, "aria-valuenow": u.progress }, h("span", { style: `width:${u.progress}%` })),
      h("p", { class: "sub", style: "margin:0" }, `Downloading ${u.latest}… ${u.progress}%. The device restarts when it is done.`),
    ];
    else if (u.state === "available") line = h("div", { class: "top" },
      h("span", { class: "grow", style: "display:flex;flex-direction:column" },
        h("strong", { style: "font-size:15px" }, `${u.latest} is available`),
        h("span", { class: "sub" }, u.channel === "prerelease" ? "Pre-release" : "Release")),
      h("button", { class: "btn small", type: "button", onclick: () => updateAction("/api/v1/update/install") }, "Install"));
    else if (u.state === "up_to_date") line = h("p", { class: "sub", style: "margin:0" }, `Up to date · checked ${ago(u.checkedAgoS)}`);
    else if (u.state === "failed") line = h("p", { class: "sub", style: "margin:0;color:var(--warn)" }, MESSAGES[u.error] || u.error || "The check failed.");
    else line = h("p", { class: "sub", style: "margin:0" }, `Checked ${ago(u.checkedAgoS)}`);

    return [
      h("div", { class: "segments track small", role: "group", "aria-label": "Update channel" },
        h("button", { type: "button", "aria-pressed": String(u.channel !== "prerelease"), disabled: busy, onclick: () => updateAction("/api/v1/update", { channel: "release" }) }, "Releases"),
        h("button", { type: "button", "aria-pressed": String(u.channel === "prerelease"), disabled: busy, onclick: () => updateAction("/api/v1/update", { channel: "prerelease" }) }, "Pre-releases")),
      u.channel === "prerelease" ? h("p", { class: "sub", style: "margin:0;line-height:1.4" }, "Pre-releases get new features first and may still have bugs.") : null,
      line,
      u.state !== "available" ? h("button", { class: "btn quiet block", type: "button", style: "height:44px;border-radius:12px", disabled: busy, onclick: () => updateAction("/api/v1/update/check") }, busy ? "Checking…" : "Check for updates") : null,
      h("label", { class: "row", style: "padding:0;min-height:44px" },
        h("span", { class: "grow" }, h("span", { style: "font-size:15px" }, "Install automatically"), h("small", {}, "Checks daily; the device restarts to install")),
        h("input", { class: "switch", type: "checkbox", checked: u.autoInstall, onchange: (e) => updateAction("/api/v1/update", { autoInstall: e.target.checked }) })),
    ];
  }

  // Home Assistant or Matter, never both: both would show the unit twice in Home Assistant.
  function smartHomeSection() {
    const c = ui.connectors;
    const f = ui.form;
    const saved = c.mqttEnabled ? "ha" : c.matterEnabled ? "matter" : "none";
    const chosen = f.integration || saved;
    const options = [["none", "None"]];
    if (c.mqttSupported !== false) options.push(["ha", "Home Assistant"]);
    if (c.matterSupported) options.push(["matter", "Matter"]);
    const pick = (value) => {
      f.integration = value;
      ui.error = "";
      // Matter and "none" need no further input; Home Assistant asks for the broker first.
      if (value === saved) { render(); return; }
      if (value === "none") saveConnectors({ mqttEnabled: false, matterEnabled: false });
      else if (value === "matter") saveConnectors({ mqttEnabled: false, matterEnabled: true });
      else render();
    };
    const brokerForm = () => h("div", { class: "net-open", style: "padding-top:12px" },
      formField("mqttHost", "Broker", { maxlength: 127, autocomplete: "off", placeholder: "homeassistant.local" }),
      h("div", { class: "field filled" }, h("span", { class: "grow" }, h("label", { for: "mqttPort" }, "Port"),
        h("input", { id: "mqttPort", type: "number", min: 1, max: 65535, value: f.mqttPort, oninput: (e) => { f.mqttPort = Number(e.target.value); } }))),
      formField("mqttUsername", "Username", { maxlength: 64, autocomplete: "off", placeholder: c.mqttCredentialsConfigured ? "Unchanged" : "" }),
      formField("mqttPassword", "Password", { type: "password", maxlength: 128, autocomplete: "new-password", placeholder: c.mqttCredentialsConfigured ? "Unchanged" : "" }),
      h("label", { class: "row", style: "padding:0" }, h("span", { class: "grow" }, "Verify TLS certificate"),
        h("input", { class: "switch", type: "checkbox", checked: f.mqttTls, onchange: (e) => { f.mqttTls = e.target.checked; } })),
      h("button", { class: "btn block", type: "button", style: "height:48px;border-radius:12px", disabled: ui.busy, onclick: () => {
        const p = { mqttEnabled: true, matterEnabled: false, mqttHost: f.mqttHost.trim(), mqttPort: f.mqttPort, mqttTls: f.mqttTls };
        if (f.mqttUsername) p.mqttUsername = f.mqttUsername;
        if (f.mqttPassword) p.mqttPassword = f.mqttPassword;
        saveConnectors(p);
      } }, "Save and restart"));

    let detail = null;
    if (chosen === "ha" && (saved !== "ha" || ui.settingsOpen === "mqtt")) {
      detail = brokerForm();
    } else if (chosen === "ha") {
      detail = h("button", { class: "row", type: "button", onclick: () => { ui.settingsOpen = "mqtt"; render(); } },
        h("span", { class: "ic ok", html: svg(P.home, 18) }),
        h("span", { class: "grow" }, h("span", {}, "Home Assistant"), h("small", {}, c.mqttHost)),
        h("span", { class: "value" }, "Edit"));
    } else if (chosen === "matter" && c.matterEnabled) {
      detail = h("button", { class: "row link", type: "button", onclick: () => go("matter") },
        h("span", { class: "ic", html: svg(P.qr, 18, 2) }),
        h("span", { class: "grow" }, "Pair with a home app"),
        h("span", { html: svg(P.chevron, 16, 2.2), style: "color:var(--faint);display:flex" }));
    }

    return h("section", {},
      h("h2", { class: "section-title" }, "Smart home"),
      h("div", { class: "segments", role: "radiogroup", "aria-label": "Smart home" },
        options.map(([value, label]) => h("button", { type: "button", role: "radio", "aria-checked": String(chosen === value), "aria-pressed": String(chosen === value), disabled: ui.busy, onclick: () => pick(value) }, label))),
      detail ? h("div", { class: "list", style: "margin-top:12px" }, detail) : null,
      h("p", { class: "note" }, chosen === "matter"
        ? "Works with Apple Home, Google Home and Home Assistant's Matter integration. Changing this restarts the device."
        : chosen === "ha" ? "Over MQTT with automatic discovery. Changing this restarts the device."
        : "Only this portal controls the device. Changing this restarts the device."));
  }

  function firmwareCard() {
    const up = ui.upload;
    return h("section", {}, h("h2", { class: "section-title" }, "Firmware"),
      h("div", { class: "card", style: "padding:16px;display:flex;flex-direction:column;gap:14px" },
        h("div", { class: "top" },
          h("span", { class: "ic warn", html: svg(P.download, 18) }),
          h("span", { class: "grow", style: "display:flex;flex-direction:column" }, h("span", { style: "font-size:16px" }, "OpenComfort"), h("span", { class: "sub mono" }, ui.device.firmwareVersion))),
        updateBlock(),
        h("details", { style: "border-top:1px solid var(--hairline);padding-top:12px", open: Boolean(ui.fileOpen || up), ontoggle: (e) => { ui.fileOpen = e.currentTarget.open; } },
          h("summary", { style: "cursor:pointer;font-size:14px;font-weight:500;color:var(--accent)" }, "Install from a file"),
          h("div", { style: "display:flex;flex-direction:column;gap:12px;margin-top:12px" },
            up && up.pct !== null ? [
              h("div", { class: "meter", role: "progressbar", "aria-valuemin": 0, "aria-valuemax": 100, "aria-valuenow": up.pct }, h("span", { id: "meterFill", style: `width:${up.pct}%` })),
              h("p", { class: "sub", id: "meterText" }, `Uploading ${up.name}… ${up.pct}%`),
            ] : [
              h("label", { class: "drop" },
                h("span", { class: "grow", style: "font-size:15px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap" }, up?.file ? up.file.name : "Choose OpenComfort.bin"),
                h("span", { class: "btn dark small" }, "Browse"),
                h("input", { type: "file", accept: ".bin", "aria-label": "Firmware file", onchange: (e) => { const file = e.target.files[0]; ui.upload = file ? { file, name: file.name, pct: null } : null; render(); } })),
              up?.file ? h("button", { class: "btn block", type: "button", onclick: uploadFirmware }, "Install update") : null,
            ])),
        h("p", { class: "sub", style: "margin:0;line-height:1.4" }, "If a new version does not start, the device goes back to this one.")));
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

  function saveConnectors(payload) {
    if (payload.adminPassword !== undefined && payload.adminPassword.length < 8) {
      ui.error = MESSAGES.invalid_admin_password;
      render();
      return;
    }
    act(async () => {
      const result = await api("/api/v1/connectors", { method: "PUT", body: JSON.stringify(payload) });
      if (payload.hostname !== undefined && result.hostname !== ui.device.hostname) {
        const address = new URL(window.location.href);
        address.hostname = `${result.hostname}.local`;
        address.pathname = "/";
        address.search = "";
        address.hash = "";
        rebooting("Hostname saved", `The device is restarting. Open ${result.hostname}.local once it is back. Sign in again at the new address.`, address.href);
      } else {
        rebooting("Saved", "The device is restarting. This page reconnects when it is back.");
      }
    });
  }

  function saveWifi() {
    act(async () => {
      const ssid = ui.form.wifiSsid.trim();
      await api("/api/v1/wifi", { method: "PUT", body: JSON.stringify({ ssid, password: ui.form.wifiPassword }) });
      rebooting("Switching network", `The device is joining ${ssid}. Connect to that network and open this address again.`);
    });
  }

  // XHR instead of fetch: only XHR reports upload progress.
  function uploadFirmware() {
    const up = ui.upload;
    up.pct = 0;
    ui.error = "";
    render();
    const xhr = new XMLHttpRequest();
    xhr.open("POST", "/api/v1/firmware");
    xhr.setRequestHeader("X-OC-CSRF", ui.csrf);
    xhr.setRequestHeader("Content-Type", "application/octet-stream");
    xhr.upload.onprogress = (e) => {
      if (!e.lengthComputable) return;
      up.pct = Math.round((e.loaded / e.total) * 100);
      const fill = document.getElementById("meterFill");
      const text = document.getElementById("meterText");
      if (fill) fill.style.width = `${up.pct}%`;
      if (text) text.textContent = `Uploading ${up.name}… ${up.pct}%`;
    };
    const fail = (message) => { ui.upload = null; ui.error = message; render(); };
    xhr.onerror = () => fail(MESSAGES.firmware_upload_interrupted);
    xhr.onload = () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        ui.upload = null;
        rebooting("Installing update", "The device restarts with the new firmware. This page reconnects when it is back.");
        return;
      }
      if (xhr.status === 401 || xhr.status === 403) { signedOut(); return; }
      let code = "";
      try { code = JSON.parse(xhr.responseText).error; } catch { /* not JSON */ }
      fail(MESSAGES[code] || code || MESSAGES.request_failed);
    };
    xhr.send(up.file);
  }

  async function logout() {
    await api("/api/v1/auth/logout", { method: "POST", body: "{}" }).catch(() => {});
    signedOut();
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

  // Matter pairing
  async function loadPairing() {
    clearInterval(ui.timers.pairing);
    try {
      ui.pairing = await api("/api/v1/matter/pairing");
      if (!ui.pairing.open) {
        await api("/api/v1/matter/commissioning-window", { method: "POST", body: "{}" });
        ui.pairing = await api("/api/v1/matter/pairing");
      }
      ui.timers.pairing = setInterval(() => {
        if (ui.view !== "matter" || !ui.pairing) { clearInterval(ui.timers.pairing); return; }
        ui.pairing.secondsLeft = Math.max(0, ui.pairing.secondsLeft - 1);
        const el = document.getElementById("pairLeft");
        if (el) el.textContent = pairingLeft();
      }, 1000);
    } catch (error) {
      ui.error = error.message;
    }
    if (ui.view === "matter") render();
  }

  function pairingLeft() {
    const s = ui.pairing?.secondsLeft || 0;
    return s > 0 ? `Open · ${Math.floor(s / 60)}:${String(s % 60).padStart(2, "0")} left` : "Pairing closed";
  }

  function qrSvg(size, modules) {
    let d = "";
    for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) if (modules[y * size + x] === "1") d += `M${x + 4} ${y + 4}h1v1h-1z`;
    const n = size + 8; // quiet zone
    return `<svg viewBox="0 0 ${n} ${n}" role="img" aria-label="Matter pairing QR code" shape-rendering="crispEdges"><path d="${d}" fill="#14161A"/></svg>`;
  }

  function viewMatter() {
    const p = ui.pairing;
    const manual = p?.manualCode ? p.manualCode.replace(/^(\d{4})(\d{3})(\d{4})$/, "$1-$2-$3") : "";
    return [
      h("section", { class: "matter-hero", "aria-labelledby": "pair-h" },
        h("div", { class: "top", style: "justify-content:space-between" },
          h("button", { class: "round-btn", type: "button", "aria-label": "Close", onclick: () => go("settings"), html: svg(P.close, 18, 2.2) }),
          h("span", { class: "pill", style: "background:rgba(255,255,255,0.16)" },
            h("span", { "aria-hidden": "true", style: `width:8px;height:8px;border-radius:4px;background:${p?.secondsLeft ? "#7EE2A8" : "#C9CDC6"}` }),
            h("span", { id: "pairLeft" }, p ? pairingLeft() : "Opening…"))),
        h("h1", { id: "pair-h", style: "margin:12px 0 4px;text-align:center;font-size:26px" }, "Add to your home"),
        h("p", { style: "margin:0 0 20px;text-align:center;font-size:15px;line-height:1.4;color:rgba(255,255,255,0.82)" }, "Scan this with Apple Home, Google Home or another Matter app."),
        p ? h("div", { class: "qr", html: qrSvg(p.qrSize, p.qrModules) }) : h("div", { class: "qr", style: "height:232px" }),
        h("p", { style: "margin:18px 0 4px;text-align:center;font-size:13px;color:rgba(255,255,255,0.78)" }, "Or enter the code"),
        h("div", { class: "top", style: "justify-content:center;gap:8px" },
          h("span", { class: "mono", style: "font-size:26px;font-weight:600;letter-spacing:0.04em" }, manual || "—"),
          manual ? h("button", { class: "round-btn", type: "button", "aria-label": "Copy code", html: svg(P.copy, 18, 2), onclick: async (e) => {
            const button = e.currentTarget;
            if (!(await copyText(p.manualCode))) return;
            button.innerHTML = svg(P.check, 18, 2.4);
            button.setAttribute("aria-label", "Copied");
            setTimeout(() => { button.innerHTML = svg(P.copy, 18, 2); button.setAttribute("aria-label", "Copy code"); }, 2000);
          } }) : null)),
      ...messages(),
      h("section", { "aria-labelledby": "how-h" },
        h("h2", { id: "how-h", class: "section-title" }, "How to add it"),
        h("ol", { class: "steps" },
          h("li", {}, h("span", { class: "num" }, "1"), h("span", {}, "Open the Home app on a phone on the same Wi‑Fi.")),
          h("li", {}, h("span", { class: "num" }, "2"), h("span", {}, "Tap ", h("b", {}, "Add device"), " and scan the code above.")),
          h("li", {}, h("span", { class: "num" }, "3"), h("span", {}, `${ui.device.friendlyName} appears as an air conditioner.`)))),
      p && !p.secondsLeft ? h("button", { class: "btn block", type: "button", onclick: loadPairing }, "Open pairing again") : null,
      h("button", { class: "btn quiet block", type: "button", onclick: () => go("settings") }, "Done"),
    ];
  }

  // Logs: the device keeps the last lines in RAM; the page fetches only new ones.
  const LOG_STYLE = { E: "error", W: "warning", I: "info", D: "debug", V: "verbose" };
  const LOG_FILTER = { all: "EWIDV", warn: "EW", error: "E" };

  async function openLogs() {
    ui.logs = { lines: [], next: 0, level: "normal", filter: ui.logs?.filter || "all", paused: false, epoch: null, nowMs: 0, last: 0 };
    // The first call may need several pages to get everything the device holds.
    for (let page = 0; page < 4; page++) {
      if (!(await fetchLogs())) break;
    }
    render();
  }

  // Returns true when there were new lines (so more may follow).
  async function fetchLogs() {
    const log = ui.logs;
    if (!log) return false;
    try {
      const data = await api(`/api/v1/logs?since=${log.next}`);
      Object.assign(log, { next: data.next, level: data.level, epoch: data.epoch, nowMs: data.nowMs, last: data.last, bootSeq: data.bootSeq });
      log.lines = log.lines.concat(data.lines).slice(-300);
      ui.error = "";
      return data.lines.length > 0 && data.next < data.last;
    } catch (error) {
      ui.error = error.message;
      return false;
    }
  }

  async function pollLogs() {
    if (!ui.logs || ui.logs.paused) return;
    await fetchLogs();
    if (ui.view === "logs") render();
  }

  function logTime(log, line) {
    if (line.seq < (log.bootSeq || 0)) return "earlier";  // before the last restart
    if (log.epoch !== null) {
      const at = new Date(log.epoch * 1000 - (log.nowMs - line.ms));
      return at.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit", hour12: false });
    }
    const s = Math.floor(line.ms / 1000);
    return `+${Math.floor(s / 3600)}:${String(Math.floor((s % 3600) / 60)).padStart(2, "0")}:${String(s % 60).padStart(2, "0")}`;
  }

  function downloadLogs() {
    const log = ui.logs;
    const text = log.lines.map((l) => `${logTime(log, l)} ${l.level} ${l.tag}: ${l.message}`).join("\n");
    const link = document.createElement("a");
    link.href = URL.createObjectURL(new Blob([text + "\n"], { type: "text/plain" }));
    link.download = `opencomfort-log-${new Date().toISOString().slice(0, 19).replace(/[:T]/g, "-")}.txt`;
    document.body.append(link);
    link.click();
    link.remove();
    setTimeout(() => URL.revokeObjectURL(link.href), 1000);
  }

  async function setLogLevel(level) {
    try {
      const data = await api("/api/v1/logs", { method: "PUT", body: JSON.stringify({ level }) });
      ui.logs.level = data.level;
    } catch (error) {
      ui.error = error.message;
    }
    render();
  }

  function viewLogs() {
    const log = ui.logs;
    const seg = (pressed, label, onclick) => h("button", { type: "button", "aria-pressed": String(pressed), onclick }, label);
    const keep = LOG_FILTER[log?.filter || "all"];
    const lines = log ? log.lines.filter((l) => keep.includes(l.level)).reverse() : [];
    return [
      h("header", { class: "top" },
        h("button", { class: "icon-btn", type: "button", "aria-label": "Back to Settings", onclick: () => go("settings"), html: svg(P.back, 18, 2.2) }),
        h("h1", { class: "grow" }, "Logs")),
      ...messages(),
      log ? [
        h("div", { class: "top", style: "gap:8px" },
          h("div", { class: "segments track small grow", role: "group", "aria-label": "Show" },
            seg(log.filter === "all", "All", () => { log.filter = "all"; render(); }),
            seg(log.filter === "warn", "Warnings", () => { log.filter = "warn"; render(); }),
            seg(log.filter === "error", "Errors", () => { log.filter = "error"; render(); })),
          h("button", { class: "ghost-btn", type: "button", "aria-label": log.paused ? "Resume live log" : "Pause live log", "aria-pressed": String(log.paused),
            html: log.paused ? '<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M8 5v14l11-7z"/></svg>' : '<svg width="18" height="18" viewBox="0 0 24 24" fill="currentColor" aria-hidden="true"><path d="M7 5h4v14H7zM13 5h4v14h-4z"/></svg>',
            onclick: () => { log.paused = !log.paused; if (!log.paused) pollLogs(); render(); } }),
          h("button", { class: "ghost-btn", type: "button", "aria-label": "Download log", html: svg(P.download, 18, 2), onclick: downloadLogs })),
        h("section", { class: "card log-card", "aria-label": "Log" },
          h("p", { class: "log-status" },
            h("span", { class: log.paused ? "log-dot" : "log-dot live", "aria-hidden": "true" }),
            `${log.paused ? "Paused" : "Live"} · ${log.lines.length} lines` +
              (log.lines.some((l) => l.seq < (log.bootSeq || 0)) ? " · “earlier” lines are from before the last restart" : " since the last restart")),
          lines.length ? h("ol", { class: "log-list" }, lines.map((l) => h("li", { class: `log-line ${LOG_STYLE[l.level] || "info"}` },
            h("span", { class: "log-time mono" }, logTime(log, l)),
            h("span", { class: "log-level mono", "aria-label": LOG_STYLE[l.level] || "info" }, l.level),
            h("span", { class: "log-text" }, h("span", { class: "mono log-tag" }, l.tag), ` ${l.message}`))))
            : h("p", { class: "empty" }, log.filter === "all" ? "No log lines yet." : "Nothing at this level. Good news.")),
        h("section", { class: "card log-detail", "aria-labelledby": "detail-h", style: "display:flex;align-items:center;gap:12px;padding:8px 8px 8px 16px;min-height:60px" },
          h("span", { class: "grow", style: "display:flex;flex-direction:column" },
            h("span", { id: "detail-h", style: "font-size:15px" }, "Detail"),
            h("span", { class: "sub", style: "margin:0" }, "Debug shows more, until the next restart")),
          h("div", { class: "segments track small", role: "group", "aria-labelledby": "detail-h" },
            seg(log.level !== "debug", "Normal", () => setLogLevel("normal")),
            seg(log.level === "debug", "Debug", () => setLogLevel("debug")))),
      ] : h("p", { class: "sub" }, "Loading…"),
    ];
  }

  // Restart
  let rebootText = ["", ""];
  let rebootAddress = "";
  function rebooting(title, text, address = "") {
    Object.values(ui.timers).forEach((t) => { clearInterval(t); clearTimeout(t); });
    store.set(CSRF_KEY, ""); // sessions live in RAM and do not survive the restart
    ui.csrf = "";
    ui.device = null;
    rebootText = [title, text];
    rebootAddress = address;
    ui.view = "rebooting";
    render();
    if (!address) setTimeout(waitForDevice, 6000);
  }

  async function waitForDevice() {
    try {
      const response = await fetch("/api/v1/session", { cache: "no-store" });
      if (response.ok) { signedOut(); return; }
    } catch { /* still restarting */ }
    setTimeout(waitForDevice, 3000);
  }

  function viewRebooting() {
    return [
      h("span", { class: "badge", style: "margin-top:40px", html: svg(P.power, 24, 2.2) }),
      h("h1", { style: "font-size:28px" }, rebootText[0]),
      h("p", { class: "lede" }, rebootText[1]),
      rebootAddress ? h("a", { class: "btn", href: rebootAddress }, "Open new address") : null,
    ];
  }

  async function boot() {
    try {
      const session = await api("/api/v1/session");
      if (session.authenticated && ui.csrf) await openDashboard();
    } catch (error) {
      ui.error = error.message;
    }
    render();
  }

  boot();
})();
