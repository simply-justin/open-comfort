#pragma once

namespace HttpPages {

inline constexpr const char* kSetupPage = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OpenComfort Setup</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 1.5rem; max-width: 32rem; }
    label { display: block; margin-top: 0.75rem; font-weight: 600; }
    input, select { width: 100%; padding: 0.5rem; margin-top: 0.25rem; box-sizing: border-box; }
    fieldset { margin-top: 1rem; border: 1px solid #ccc; padding: 0.75rem; }
    button { margin-top: 1rem; padding: 0.6rem 1rem; }
    .hidden { display: none; }
    #status { margin-top: 1rem; color: #444; }
    .error { color: #b00020; }
    .ok { color: #006400; }
  </style>
</head>
<body>
  <h1>OpenComfort Setup</h1>
  <p>Connect to your Wi-Fi, choose an AC product, and configure the bus pins.</p>
  <form id="setup-form">
    <label>Wi-Fi SSID<input name="wifiSsid" required></label>
    <label>Wi-Fi password<input name="wifiPassword" type="password"></label>
    <label>Device name<input name="friendlyName" value="OpenComfort Device"></label>
    <label>AC name<input name="acFriendlyName" value="Air Conditioner"></label>
    <label>Product<select name="productId" id="product-select" required></select></label>
    <fieldset id="spi-fields">
      <legend>SPI bus pins</legend>
      <label>SCK pin<input name="sckPin" type="number" value="12"></label>
      <label>MOSI pin<input name="mosiPin" type="number" value="11"></label>
      <label>MISO pin<input name="misoPin" type="number" value="13"></label>
      <label>CS pin<input name="csPin" type="number" value="10"></label>
      <label>Frequency (Hz)<input name="frequency" type="number" value="1000000"></label>
    </fieldset>
    <button type="submit">Save and configure</button>
  </form>
  <p id="status"></p>
  <script>
    const productSelect = document.getElementById('product-select');
    const spiFields = document.getElementById('spi-fields');
    const statusEl = document.getElementById('status');
    let products = [];

    function setStatus(text, ok) {
      statusEl.textContent = text;
      statusEl.className = ok ? 'ok' : (text ? 'error' : '');
    }

    function updateBusFields() {
      const product = products.find(p => p.id === productSelect.value);
      spiFields.classList.toggle('hidden', !product || product.bus !== 'spi');
    }

    async function loadProducts() {
      const res = await fetch('/api/setup/products');
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Failed to load products');
      products = data.products || [];
      productSelect.innerHTML = products.map(p =>
        `<option value="${p.id}">${p.name}</option>`).join('');
      updateBusFields();
    }

    productSelect.addEventListener('change', updateBusFields);

    document.getElementById('setup-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      setStatus('Saving configuration…', true);
      const form = new FormData(e.target);
      const product = products.find(p => p.id === form.get('productId'));
      const payload = {
        wifiSsid: form.get('wifiSsid'),
        wifiPassword: form.get('wifiPassword'),
        friendlyName: form.get('friendlyName'),
        acFriendlyName: form.get('acFriendlyName'),
        productId: form.get('productId'),
        bus: { type: product ? product.bus : 'spi' }
      };
      if (payload.bus.type === 'spi') {
        Object.assign(payload.bus, {
          sckPin: Number(form.get('sckPin')),
          mosiPin: Number(form.get('mosiPin')),
          misoPin: Number(form.get('misoPin')),
          csPin: Number(form.get('csPin')),
          frequency: Number(form.get('frequency'))
        });
      }
      const res = await fetch('/api/setup', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      const data = await res.json();
      if (!res.ok) {
        setStatus(data.error || 'Setup failed', false);
        return;
      }
      setStatus('Setup complete. Device is restarting…', true);
    });

    loadProducts().catch(err => setStatus(err.message, false));
  </script>
</body>
</html>)HTML";

inline constexpr const char* kControlPage = R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>OpenComfort</title>
  <style>
    body { font-family: system-ui, sans-serif; margin: 1.5rem; max-width: 28rem; }
    label { display: block; margin-top: 0.75rem; font-weight: 600; }
    input, select, button { width: 100%; padding: 0.5rem; margin-top: 0.25rem; box-sizing: border-box; }
    .row { display: flex; gap: 0.5rem; align-items: center; }
    .row input[type="checkbox"] { width: auto; margin: 0; }
    #state { margin: 1rem 0; padding: 0.75rem; background: #f4f4f4; border-radius: 0.25rem; }
    #status { margin-top: 0.75rem; min-height: 1.25rem; }
    .error { color: #b00020; }
    .ok { color: #006400; }
  </style>
</head>
<body>
  <h1>OpenComfort</h1>
  <div id="state">Loading…</div>
  <form id="control-form">
    <div class="row">
      <input id="powerOn" type="checkbox">
      <label for="powerOn">Power on</label>
    </div>
    <label>Mode
      <select id="mode">
        <option value="off">Off</option>
        <option value="cool">Cool</option>
        <option value="heat">Heat</option>
        <option value="dry">Dry</option>
        <option value="fan">Fan</option>
        <option value="auto">Auto</option>
      </select>
    </label>
    <label>Target temperature (°C)
      <input id="targetTemp" type="number" min="16" max="30" step="0.5">
    </label>
    <label>Fan speed
      <select id="fanSpeed">
        <option value="auto">Auto</option>
        <option value="low">Low</option>
        <option value="medium">Medium</option>
        <option value="high">High</option>
      </select>
    </label>
    <button type="submit">Apply</button>
  </form>
  <p id="status"></p>
  <script>
    const stateEl = document.getElementById('state');
    const statusEl = document.getElementById('status');

    function setStatus(text, ok) {
      statusEl.textContent = text || '';
      statusEl.className = ok ? 'ok' : (text ? 'error' : '');
    }

    async function refreshState() {
      const res = await fetch('/api/modules/ac/state');
      const data = await res.json();
      if (!res.ok) throw new Error(data.error || 'Failed to load state');
      document.getElementById('powerOn').checked = !!data.powerOn;
      document.getElementById('mode').value = data.mode;
      document.getElementById('targetTemp').value = data.targetTemp;
      document.getElementById('fanSpeed').value = data.fanSpeed;
      stateEl.textContent =
        `Power: ${data.powerOn ? 'on' : 'off'} · Mode: ${data.mode} · ` +
        `Current: ${data.currentTemp.toFixed(1)}°C · Target: ${data.targetTemp.toFixed(1)}°C · Fan: ${data.fanSpeed}`;
    }

    document.getElementById('control-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      setStatus('Sending command…', true);
      const payload = {
        powerOn: document.getElementById('powerOn').checked,
        mode: document.getElementById('mode').value,
        targetTemp: Number(document.getElementById('targetTemp').value),
        fanSpeed: document.getElementById('fanSpeed').value
      };
      const res = await fetch('/api/modules/ac/command', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(payload)
      });
      const data = await res.json();
      if (!res.ok) {
        setStatus(data.error || 'Command failed', false);
        return;
      }
      setStatus('Command applied', true);
      refreshState().catch(() => {});
    });

    refreshState().catch(err => {
      stateEl.textContent = err.message;
      setStatus(err.message, false);
    });
    setInterval(() => refreshState().catch(() => {}), 3000);
  </script>
</body>
</html>)HTML";

} // namespace HttpPages
