(() => {
  const state = {
    step: 0,
    session: null,
    schema: null,
    networks: [],
    selectedSsid: "",
    password: "",
    model: "",
    friendlyName: "OpenComfort",
  };

  const title = document.getElementById("title");
  const subtitle = document.getElementById("subtitle");
  const panel = document.getElementById("panel");
  const alertBox = document.getElementById("alert");
  const stepper = document.getElementById("steps");

  class OcStepper extends HTMLElement {
    setSteps(labels, active) {
      this.innerHTML = labels
        .map((label, index) => {
          const cls = [
            "step-pill",
            index === active ? "is-active" : "",
            index < active ? "is-done" : "",
          ]
            .filter(Boolean)
            .join(" ");
          return `<div class="${cls}">${label}</div>`;
        })
        .join("");
    }
  }
  customElements.define("oc-stepper", OcStepper);

  class OcAlert extends HTMLElement {
    show(message) {
      this.hidden = !message;
      this.textContent = message || "";
    }
  }
  customElements.define("oc-alert", OcAlert);

  async function api(path, options = {}) {
    const response = await fetch(path, {
      headers: { "Content-Type": "application/json", ...(options.headers || {}) },
      ...options,
    });
    const data = await response.json().catch(() => ({}));
    if (!response.ok) {
      throw new Error(data.error || "request_failed");
    }
    return data;
  }

  function showError(message) {
    alertBox.show(message);
  }

  function clearError() {
    alertBox.show("");
  }

  function renderStepper() {
    stepper.setSteps(["Wi‑Fi", "Product", "Finish"], state.step);
  }

  function renderWifi() {
    title.textContent = "Connect to Wi‑Fi";
    subtitle.textContent = "Choose your network, then continue to product setup.";
    panel.innerHTML = `
      <div class="actions" style="justify-content:space-between;margin-top:0;">
        <p class="muted">Nearby networks</p>
        <button class="button-secondary" id="scanBtn" type="button">Scan</button>
      </div>
      <div class="network-list" id="networks"></div>
      <div class="field">
        <label for="password">Password</label>
        <input id="password" type="password" autocomplete="current-password" />
      </div>
      <div class="actions">
        <button class="button-primary" id="wifiNext" type="button">Continue</button>
      </div>
    `;

    const list = document.getElementById("networks");
    list.innerHTML = state.networks
      .map(
        (network) => `
      <button class="network ${state.selectedSsid === network.ssid ? "is-selected" : ""}" data-ssid="${network.ssid}" type="button">
        <span>${network.ssid}</span>
        <span class="muted">${network.rssi} dBm</span>
      </button>`
      )
      .join("");

    list.querySelectorAll(".network").forEach((button) => {
      button.addEventListener("click", () => {
        state.selectedSsid = button.dataset.ssid;
        renderWifi();
      });
    });

    document.getElementById("password").value = state.password;
    document.getElementById("password").addEventListener("input", (event) => {
      state.password = event.target.value;
    });
    document.getElementById("scanBtn").addEventListener("click", loadNetworks);
    document.getElementById("wifiNext").addEventListener("click", submitWifi);
  }

  function fieldOptions(field) {
    return (field.options || [])
      .map((option) => `<option value="${option.value}">${option.label}</option>`)
      .join("");
  }

  function renderProduct() {
    const step = state.schema.steps[0];
    const modelField = step.fields.find((field) => field.id === "model");
    title.textContent = step.title;
    subtitle.textContent = step.description;
    panel.innerHTML = `
      <div class="field">
        <label for="model">${modelField.label}</label>
        <select id="model">${fieldOptions(modelField)}</select>
      </div>
      <div class="field">
        <label for="friendlyName">Device name</label>
        <input id="friendlyName" type="text" maxlength="32" />
      </div>
      <div class="actions">
        <button class="button-secondary" id="backBtn" type="button">Back</button>
        <button class="button-primary" id="productNext" type="button">Continue</button>
      </div>
    `;
    document.getElementById("model").value = state.model || modelField.options[0]?.value || "";
    document.getElementById("friendlyName").value = state.friendlyName;
    document.getElementById("model").addEventListener("change", (event) => {
      state.model = event.target.value;
    });
    document.getElementById("friendlyName").addEventListener("input", (event) => {
      state.friendlyName = event.target.value;
    });
    document.getElementById("backBtn").addEventListener("click", () => {
      state.step = 0;
      render();
    });
    document.getElementById("productNext").addEventListener("click", submitProduct);
  }

  function renderFinish() {
    title.textContent = "Ready to finish";
    subtitle.textContent = "Save the configuration and reboot into normal operation.";
    panel.innerHTML = `
      <p class="muted">Wi‑Fi and product settings will be stored on the device.</p>
      <div class="actions">
        <button class="button-secondary" id="backBtn" type="button">Back</button>
        <button class="button-primary" id="commitBtn" type="button">Save and reboot</button>
      </div>
    `;
    document.getElementById("backBtn").addEventListener("click", () => {
      state.step = 1;
      render();
    });
    document.getElementById("commitBtn").addEventListener("click", commitSetup);
  }

  function render() {
    clearError();
    renderStepper();
    if (state.step === 0) renderWifi();
    if (state.step === 1) renderProduct();
    if (state.step === 2) renderFinish();
  }

  async function loadNetworks() {
    clearError();
    try {
      const data = await api("/api/v1/wifi/networks");
      state.networks = data.networks || [];
      if (!state.selectedSsid && state.networks[0]) {
        state.selectedSsid = state.networks[0].ssid;
      }
      renderWifi();
    } catch (error) {
      showError(error.message);
    }
  }

  async function submitWifi() {
    clearError();
    if (!state.selectedSsid) {
      showError("Select a Wi‑Fi network.");
      return;
    }
    try {
      await api("/api/v1/wifi", {
        method: "PUT",
        body: JSON.stringify({ ssid: state.selectedSsid, password: state.password }),
      });
      state.step = 1;
      render();
    } catch (error) {
      showError(error.message);
    }
  }

  async function submitProduct() {
    clearError();
    try {
      await api("/api/v1/setup/module", {
        method: "PUT",
        body: JSON.stringify({
          model: state.model,
          friendlyName: state.friendlyName,
        }),
      });
      state.step = 2;
      render();
    } catch (error) {
      showError(error.message);
    }
  }

  async function commitSetup() {
    clearError();
    try {
      await api("/api/v1/setup/commit", { method: "POST", body: "{}" });
      title.textContent = "Setup complete";
      subtitle.textContent = "The device is rebooting into normal mode.";
      panel.innerHTML = `<p class="muted">You can close this page.</p>`;
    } catch (error) {
      showError(error.message);
    }
  }

  async function boot() {
    try {
      state.session = await api("/api/v1/session");
      state.schema = await api("/api/v1/setup/schema");
      await loadNetworks();
      render();
    } catch (error) {
      showError(error.message);
    }
  }

  boot();
})();
