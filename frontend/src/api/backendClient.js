const DEFAULT_BASE = '';

export class MetaBrainBackendClient {
  constructor(baseUrl = DEFAULT_BASE) {
    this.baseUrl = baseUrl;
    this.lastLatencyMs = 0;
    this.online = false;
  }

  async _request(path, options = {}) {
    const t0 = performance.now();
    try {
      const res = await fetch(`${this.baseUrl}${path}`, {
        headers: { 'Content-Type': 'application/json' },
        ...options
      });
      this.lastLatencyMs = Math.max(0.1, performance.now() - t0);
      if (!res.ok) {
        throw new Error(`HTTP ${res.status}`);
      }
      this.online = true;
      return await res.json();
    } catch (err) {
      // Fallback direct port probe if opened outside Vite proxy
      if (this.baseUrl === '') {
        try {
          const res2 = await fetch(`http://127.0.0.1:8085${path}`, {
            headers: { 'Content-Type': 'application/json' },
            ...options
          });
          if (res2.ok) {
            this.baseUrl = 'http://127.0.0.1:8085';
            this.online = true;
            this.lastLatencyMs = Math.max(0.1, performance.now() - t0);
            return await res2.json();
          }
        } catch (_) {}
      }
      this.online = false;
      throw err;
    }
  }

  getHealth() {
    return this._request('/api/health');
  }

  getState() {
    return this._request('/api/state');
  }

  getNeuronDetail(id) {
    return this._request(`/api/neuron?id=${encodeURIComponent(id)}`);
  }

  setControl(payload) {
    return this._request('/api/control', {
      method: 'POST',
      body: JSON.stringify(payload)
    });
  }

  step(ticks = 1, mode = 'oop') {
    return this._request('/api/step', {
      method: 'POST',
      body: JSON.stringify({ ticks, mode })
    });
  }

  setEndocrine(payload) {
    return this._request('/api/endocrine', {
      method: 'POST',
      body: JSON.stringify(payload)
    });
  }

  setConfig(payload) {
    return this._request('/api/config', {
      method: 'POST',
      body: JSON.stringify(payload)
    });
  }

  injectText(text, holdTicks = 16) {
    return this._request('/api/pulse', {
      method: 'POST',
      body: JSON.stringify({ mode: 'text', text, hold_ticks: holdTicks })
    });
  }

  injectImage(pixels, width = 16, height = 16, holdTicks = 18) {
    return this._request('/api/pulse', {
      method: 'POST',
      body: JSON.stringify({ mode: 'image', pixels, width, height, hold_ticks: holdTicks })
    });
  }

  injectManual(pulses, holdTicks = 16) {
    return this._request('/api/pulse', {
      method: 'POST',
      body: JSON.stringify({ mode: 'manual', pulses, hold_ticks: holdTicks })
    });
  }

  sendFeedback({ valence = 1.0, targetId = -1, targets = [] }) {
    return this._request('/api/feedback', {
      method: 'POST',
      body: JSON.stringify({ valence, target_id: targetId, targets })
    });
  }

  addChildSubNetwork(parentId = -1, count = 3) {
    return this._request('/api/add_child', {
      method: 'POST',
      body: JSON.stringify({ parent_id: parentId, count })
    });
  }

  triggerSynaptogenesis() {
    return this._request('/api/synaptogenesis', {
      method: 'POST',
      body: '{}'
    });
  }

  saveSnapshots() {
    return this._request('/api/save', { method: 'POST', body: '{}' });
  }

  loadSnapshot() {
    return this._request('/api/load', { method: 'POST', body: '{}' });
  }

  runBenchmark(macroNeurons = 10000) {
    return this._request('/api/benchmark', {
      method: 'POST',
      body: JSON.stringify({ macro_neurons: macroNeurons })
    });
  }

  resetBrain({ roots = 8, cortical = 20, terminals = 4 } = {}) {
    return this._request('/api/reset', {
      method: 'POST',
      body: JSON.stringify({ roots, cortical, terminals })
    });
  }
}
