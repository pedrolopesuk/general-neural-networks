import * as THREE from 'three';
import { MetaBrainBackendClient } from './api/backendClient.js';
import { BrainViewport } from './scene/BrainViewport.js';

const api = new MetaBrainBackendClient();
let latestState = null;
let selectedNeuronId = null;
let simRunning = true;
let dynamicForces = true;
let sliderBeingDragged = false;

// Toast Helper
let toastTimer = null;
function showToast(msg) {
  const el = document.getElementById('toast-banner');
  el.textContent = msg;
  el.style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.style.display = 'none'; }, 3800);
}

// -----------------------------------------------------------------------------
// Initialize 3D Viewport
// -----------------------------------------------------------------------------
const viewport = new BrainViewport(document.getElementById('viewport-root'), {
  onSelectNeuron: async (id) => {
    selectedNeuronId = id;
    await refreshNeuronInspector(id);
    updateBreadcrumbs(id);
  },
  onHoverNeuron: (node, x, y) => {
    const card = document.getElementById('hover-card');
    if (!node) {
      card.style.display = 'none';
      return;
    }
    card.style.display = 'block';
    card.style.left = `${x + 15}px`;
    card.style.top = `${y + 12}px`;
    card.innerHTML = `
      <div style="font-weight:700;color:#7ce8ff;margin-bottom:3px;">${node.label} (#${node.id})</div>
      <div style="color:#b8cbe8;">Role: <strong>${node.role}</strong> • Depth: <strong>${node.depth}</strong></div>
      <div style="color:#b8cbe8;">Fire: <strong>${(node.activation || 0).toFixed(3)}</strong> • Age: <strong>${node.age_ticks}t</strong></div>
      ${node.is_composite ? `<div style="color:#00ff95;margin-top:3px;">✦ Composite Holon (${node.child_count} children) — Double-click to zoom inside</div>` : ''}
    `;
  },
  onZoomTierChange: (tier) => {
    document.getElementById('btn-z-macro').classList.toggle('active', tier === 'macro');
    document.getElementById('btn-z-cluster').classList.toggle('active', tier === 'cluster');
    document.getElementById('btn-z-micro').classList.toggle('active', tier === 'micro');
  }
});

// -----------------------------------------------------------------------------
// 16x16 Interactive Visual Retina Canvas
// -----------------------------------------------------------------------------
const retinaCanvas = document.getElementById('retina-canvas');
const rCtx = retinaCanvas.getContext('2d');
const retinaGrid = new Float32Array(16 * 16);

function renderRetinaPad() {
  const img = rCtx.createImageData(16, 16);
  for (let i = 0; i < 256; i++) {
    const v = Math.max(0, Math.min(1, retinaGrid[i]));
    const p = i * 4;
    img.data[p]     = Math.round(12 + 243 * v);
    img.data[p + 1] = Math.round(22 + 225 * v);
    img.data[p + 2] = Math.round(48 + 207 * v);
    img.data[p + 3] = 255;
  }
  rCtx.putImageData(img, 0, 0);
}

function paintRetina(e) {
  const rect = retinaCanvas.getBoundingClientRect();
  const gx = Math.floor(((e.clientX - rect.left) / rect.width) * 16);
  const gy = Math.floor(((e.clientY - rect.top) / rect.height) * 16);
  for (let dy = -1; dy <= 1; dy++) {
    for (let dx = -1; dx <= 1; dx++) {
      const x = gx + dx, y = gy + dy;
      if (x >= 0 && x < 16 && y >= 0 && y < 16) {
        const falloff = (dx === 0 && dy === 0) ? 1.0 : 0.45;
        retinaGrid[y * 16 + x] = Math.min(1.0, retinaGrid[y * 16 + x] + falloff);
      }
    }
  }
  renderRetinaPad();
}

let paintingRetina = false;
retinaCanvas.addEventListener('pointerdown', (e) => { paintingRetina = true; paintRetina(e); });
window.addEventListener('pointerup', () => { paintingRetina = false; });
retinaCanvas.addEventListener('pointermove', (e) => { if (paintingRetina) paintRetina(e); });

function drawRetinaPreset(kind) {
  retinaGrid.fill(0);
  for (let y = 0; y < 16; y++) {
    for (let x = 0; x < 16; x++) {
      if (kind === 'circle') {
        const d = Math.hypot(x - 7.5, y - 7.5);
        if (Math.abs(d - 5.2) < 1.35) retinaGrid[y * 16 + x] = 1.0;
      } else if (kind === 'cross') {
        if (Math.abs(x - 7.5) < 1.2 || Math.abs(y - 7.5) < 1.2) retinaGrid[y * 16 + x] = 1.0;
      } else if (kind === 'diag') {
        if (Math.abs((15 - x) - y) < 1.5) retinaGrid[y * 16 + x] = 1.0;
      }
    }
  }
  renderRetinaPad();
}
drawRetinaPreset('circle');

document.getElementById('ret-circle').onclick = () => drawRetinaPreset('circle');
document.getElementById('ret-cross').onclick  = () => drawRetinaPreset('cross');
document.getElementById('ret-diag').onclick   = () => drawRetinaPreset('diag');
document.getElementById('ret-clear').onclick  = () => { retinaGrid.fill(0); renderRetinaPad(); };
document.getElementById('btn-send-retina').onclick = async () => {
  const state = await api.injectImage(Array.from(retinaGrid), 16, 16, 20);
  applyIncomingState(state);
  showToast('Injected 16×16 Retina Visual Grid → C++ SensoryPipeline');
};

// -----------------------------------------------------------------------------
// Manual Float [0..1] Root Channel Faders
// -----------------------------------------------------------------------------
const manualValues = [0.85, 0.2, 0.65, 0.1, 0.9, 0.15, 0.45, 0.3];
const manualContainer = document.getElementById('manual-channels');

function renderManualFaders() {
  manualContainer.innerHTML = manualValues.map((v, i) => `
    <div class="ch-col">
      <div class="ch-bar-wrap" data-ch="${i}">
        <div class="ch-bar-fill" style="height:${Math.round(v * 100)}%"></div>
      </div>
      <span class="ch-lbl">R${i}</span>
    </div>
  `).join('');

  manualContainer.querySelectorAll('.ch-bar-wrap').forEach(bar => {
    const setFromEvent = (ev) => {
      const rect = bar.getBoundingClientRect();
      const ratio = 1.0 - (ev.clientY - rect.top) / rect.height;
      const idx = parseInt(bar.dataset.ch, 10);
      manualValues[idx] = Math.max(0, Math.min(1, ratio));
      bar.querySelector('.ch-bar-fill').style.height = `${Math.round(manualValues[idx] * 100)}%`;
    };
    let dragging = false;
    bar.addEventListener('pointerdown', (ev) => { dragging = true; setFromEvent(ev); });
    window.addEventListener('pointerup', () => { dragging = false; });
    bar.addEventListener('pointermove', (ev) => { if (dragging) setFromEvent(ev); });
  });
}
renderManualFaders();

document.getElementById('btn-send-manual').onclick = async () => {
  const state = await api.injectManual(manualValues, 18);
  applyIncomingState(state);
  showToast('Injected Manual [0..1] Float Vector → Root Sensory Neurons');
};

// Sensory Tab Switching
const tabs = [
  ['tab-btn-text', 'pane-text'],
  ['tab-btn-retina', 'pane-retina'],
  ['tab-btn-manual', 'pane-manual']
];
tabs.forEach(([btnId, paneId]) => {
  document.getElementById(btnId).onclick = () => {
    tabs.forEach(([b, p]) => {
      document.getElementById(b).classList.toggle('active', b === btnId);
      document.getElementById(p).classList.toggle('active', p === paneId);
    });
  };
});

// Text Sensory Injection
async function triggerTextPulse(txt) {
  document.getElementById('inp-text').value = txt;
  const state = await api.injectText(txt, 18);
  applyIncomingState(state);
}
document.getElementById('btn-send-text').onclick = () =>
  triggerTextPulse(document.getElementById('inp-text').value);
document.querySelectorAll('[data-preset]').forEach(btn => {
  btn.onclick = () => triggerTextPulse(btn.dataset.preset);
});

// -----------------------------------------------------------------------------
// Lenses, Endocrine Sliders & Transport Controls
// -----------------------------------------------------------------------------
function switchLens(mode) {
  viewport.setLens(mode);
  document.getElementById('btn-lens-activity').classList.toggle('active', mode === 'activity');
  document.getElementById('btn-lens-age').classList.toggle('active', mode === 'age');
  const ramp = document.getElementById('lens-ramp');
  const labels = document.getElementById('lens-labels');
  if (mode === 'activity') {
    ramp.style.background = 'linear-gradient(90deg, #091533, #00e5ff, #ffffff)';
    labels.innerHTML = '<span>Resting (0.0)</span><span>Cyan Active</span><span>White Firing (1.0)</span>';
  } else {
    ramp.style.background = 'linear-gradient(90deg, #00ff95, #38bdf8, #a855f7)';
    labels.innerHTML = '<span>Emerald (Nascent)</span><span>Consolidating</span><span>Purple (Mature)</span>';
  }
}
document.getElementById('btn-lens-activity').onclick = () => switchLens('activity');
document.getElementById('btn-lens-age').onclick      = () => switchLens('age');

const slIds = ['sl-fear', 'sl-dopamine', 'sl-fatigue', 'sl-curiosity'];
slIds.forEach(id => {
  const el = document.getElementById(id);
  el.addEventListener('pointerdown', () => { sliderBeingDragged = true; });
  window.addEventListener('pointerup', () => { sliderBeingDragged = false; });
  el.addEventListener('input', async () => {
    const payload = {
      fear: parseFloat(document.getElementById('sl-fear').value),
      dopamine: parseFloat(document.getElementById('sl-dopamine').value),
      fatigue: parseFloat(document.getElementById('sl-fatigue').value),
      curiosity: parseFloat(document.getElementById('sl-curiosity').value)
    };
    document.getElementById('lbl-fear').textContent = payload.fear.toFixed(2);
    document.getElementById('lbl-dopamine').textContent = payload.dopamine.toFixed(2);
    document.getElementById('lbl-fatigue').textContent = payload.fatigue.toFixed(2);
    document.getElementById('lbl-curiosity').textContent = payload.curiosity.toFixed(2);
    const state = await api.setEndocrine(payload);
    applyIncomingState(state);
  });
});

document.getElementById('btn-reward').onclick = async () => {
  const state = await api.sendFeedback({ valence: 1.15 });
  applyIncomingState(state);
  showToast('▲ Global Reward (+Dopamine Surge & Recursive Backpropagation)');
};

document.getElementById('btn-punish').onclick = async () => {
  const state = await api.sendFeedback({ valence: -1.0 });
  applyIncomingState(state);
  showToast('▼ Global Punish (+Fear Alert & Synaptic Error Correction)');
};

document.getElementById('btn-mitosis-global').onclick = async () => {
  const state = await api.addChildSubNetwork(-1, 3);
  applyIncomingState(state);
  switchLens('age');
  showToast('✦ Curiosity Mitosis: Spawned New Emerald Micro-Circuit via AddChild()');
};

document.getElementById('btn-synaptogenesis').onclick = async () => {
  const state = await api.triggerSynaptogenesis();
  applyIncomingState(state);
  showToast('🔗 Synaptogenesis: Wired Co-Active Neurons & Pruned Weak Synapses');
};

document.getElementById('btn-run-toggle').onclick = async (e) => {
  simRunning = !simRunning;
  await api.setControl({ running: simRunning });
  e.target.textContent = simRunning ? '⏸ Running (18Hz)' : '▶ Paused';
  e.target.classList.toggle('active', simRunning);
};

document.getElementById('btn-physics-toggle').onclick = async (e) => {
  dynamicForces = !dynamicForces;
  await api.setControl({ dynamic_layout: dynamicForces });
  e.target.classList.toggle('active', dynamicForces);
};

document.getElementById('btn-save').onclick = async () => {
  const res = await api.saveSnapshots();
  if (res.saved) {
    showToast(`💾 Saved ${res.json_path} & ${res.graphml_path}`);
  }
};

document.getElementById('btn-bench').onclick = async (e) => {
  const orig = e.target.textContent;
  e.target.textContent = '⏳ Benchmarking...';
  try {
    const rep = await api.runBenchmark(10000);
    showToast(
      `⚡ C++20 AVX2 CSR [${rep.total_neurons.toLocaleString()} Neurons / ${rep.total_synapses.toLocaleString()} Synapses]: ` +
      `${rep.forward_msyn_s.toFixed(0)}M Syn/s (${rep.forward_ms.toFixed(2)}ms/tick)`
    );
  } finally {
    e.target.textContent = orig;
  }
};

document.getElementById('btn-reset').onclick = async () => {
  selectedNeuronId = null;
  const state = await api.resetBrain({ roots: 8, cortical: 20, terminals: 4 });
  applyIncomingState(state);
  viewport.flyToTarget(new THREE.Vector3(0, 0, 0), 395);
  showToast('↺ Rebuilt Fresh Recursive Emotional Brain');
};

// Infinite Zoom Footer Buttons
document.getElementById('btn-z-macro').onclick = () => {
  viewport.flyToTarget(new THREE.Vector3(0, 0, 0), 395);
};
document.getElementById('btn-z-cluster').onclick = () => {
  const col = latestState?.flat_nodes?.find(n => n.is_composite && n.depth === 0);
  if (col) viewport.selectAndFlyTo(col.id);
};
document.getElementById('btn-z-micro').onclick = () => {
  const sub = latestState?.flat_nodes?.find(n => n.is_composite && n.depth >= 1) ||
              latestState?.flat_nodes?.find(n => n.depth >= 2);
  if (sub) viewport.selectAndFlyTo(sub.id);
};

// -----------------------------------------------------------------------------
// Deep Neuron Inspector & Hierarchy Breadcrumbs
// -----------------------------------------------------------------------------
function updateBreadcrumbs(id) {
  const bc = document.getElementById('breadcrumbs');
  const chain = [];
  let cur = latestState?.flat_nodes?.find(n => n.id === id);
  while (cur) {
    chain.unshift(cur);
    cur = cur.parent_id >= 0 ? latestState.flat_nodes.find(n => n.id === cur.parent_id) : null;
  }

  let html = `<span class="crumb" data-fly="macro">🧠 Macro Brain</span>`;
  for (const node of chain) {
    html += `<span>›</span><span class="crumb" data-nid="${node.id}">${node.label}</span>`;
  }
  bc.innerHTML = html;

  bc.querySelectorAll('[data-fly="macro"]').forEach(el => {
    el.onclick = () => viewport.flyToTarget(new THREE.Vector3(0, 0, 0), 395);
  });
  bc.querySelectorAll('[data-nid]').forEach(el => {
    el.onclick = async () => {
      const nid = parseInt(el.dataset.nid, 10);
      selectedNeuronId = nid;
      viewport.selectAndFlyTo(nid);
      await refreshNeuronInspector(nid);
      updateBreadcrumbs(nid);
    };
  });
}

async function refreshNeuronInspector(id) {
  if (id === null) return;
  try {
    const d = await api.getNeuronDetail(id);
    if (d.error) return;
    const box = document.getElementById('inspector-box');

    const childrenHtml = d.children && d.children.length > 0
      ? `<div class="sub-pill-list">
           ${d.children.map(c => `<span class="sub-pill" data-cid="${c.id}">${c.label.split('.').pop()} (${c.activation.toFixed(2)})</span>`).join('')}
         </div>`
      : `<div style="font-size:10.5px;color:var(--txt-muted);">Leaf Neuron (0 nested Child_Neurons)</div>`;

    const weightsHtml = d.incoming_synapses && d.incoming_synapses.length > 0
      ? `<div class="weight-table">
           ${d.incoming_synapses.slice(0, 12).map(s => `
             <div class="w-row">
               <span style="color:#9eb6db;">← ${s.source_label}</span>
               <strong style="color:${s.weight >= 0 ? '#00ff95' : '#ff6b8b'};">
                 w=${s.weight >= 0 ? '+' : ''}${s.weight.toFixed(3)} (e=${s.eligibility.toFixed(2)})
               </strong>
             </div>
           `).join('')}
         </div>`
      : `<div style="font-size:10.5px;color:var(--txt-muted);">Direct Sensory Receptor (No presynaptic weights)</div>`;

    box.innerHTML = `
      <div style="display:flex;justify-content:space-between;align-items:center;">
        <strong style="color:#7ce8ff;font-size:12.5px;">${d.label} (#${d.id})</strong>
        <button id="btn-dive-sel" class="btn-sm">🔍 Zoom In</button>
      </div>
      <div class="insp-meta">
        <div>Role: <strong>${d.role}</strong></div>
        <div>Depth: <strong>${d.depth}</strong></div>
        <div>Bias: <strong>${d.bias.toFixed(3)}</strong></div>
        <div>Membrane V: <strong>${d.potential.toFixed(3)}</strong></div>
        <div>Activation: <strong>${d.activation.toFixed(3)}</strong></div>
        <div>Age: <strong>${d.age_ticks} ticks</strong></div>
      </div>
      <div>
        <div style="font-size:10px;color:#82a2d1;margin-bottom:4px;font-weight:700;">
          COMPOSITE CHILD_NEURONS (${d.children.length})
        </div>
        ${childrenHtml}
      </div>
      <div>
        <div style="font-size:10px;color:#82a2d1;margin-bottom:4px;font-weight:700;">
          CONNECTION_WEIGHTS (${d.incoming_synapses.length} IN / ${d.outgoing_synapses.length} OUT)
        </div>
        ${weightsHtml}
      </div>
      ${d.role !== 'RootSensory' && d.role !== 'TerminalReadout' ? `
        <button id="btn-spawn-here" class="btn-purple btn-sm" style="width:100%;">
          ✦ AddChild() — Instantiate 3 Sub-Neurons Inside #${d.id}
        </button>
      ` : ''}
    `;

    document.getElementById('btn-dive-sel').onclick = () => viewport.selectAndFlyTo(d.id);

    box.querySelectorAll('[data-cid]').forEach(pill => {
      pill.onclick = async () => {
        const cid = parseInt(pill.dataset.cid, 10);
        selectedNeuronId = cid;
        viewport.selectAndFlyTo(cid);
        await refreshNeuronInspector(cid);
        updateBreadcrumbs(cid);
      };
    });

    const spawnBtn = document.getElementById('btn-spawn-here');
    if (spawnBtn) {
      spawnBtn.onclick = async () => {
        const state = await api.addChildSubNetwork(d.id, 3);
        applyIncomingState(state);
        viewport.selectAndFlyTo(d.id);
        await refreshNeuronInspector(d.id);
        showToast(`✦ Instantiated 3 Child Neurons inside ${d.label}!`);
      };
    }
  } catch (_) {}
}

// -----------------------------------------------------------------------------
// Oscilloscope Sparkline Renderer
// -----------------------------------------------------------------------------
const oscCanvas = document.getElementById('oscilloscope');
const oscCtx = oscCanvas.getContext('2d');

function drawOscilloscope(telemetry) {
  const w = oscCanvas.width;
  const h = oscCanvas.height;
  oscCtx.clearRect(0, 0, w, h);

  if (!telemetry || telemetry.length < 2) return;

  // Subtle horizontal gridline
  oscCtx.strokeStyle = 'rgba(130, 180, 255, 0.1)';
  oscCtx.lineWidth = 1;
  oscCtx.beginPath();
  oscCtx.moveTo(0, h * 0.5);
  oscCtx.lineTo(w, h * 0.5);
  oscCtx.stroke();

  const plotTrace = (getter, color, lineWidth = 1.8) => {
    oscCtx.strokeStyle = color;
    oscCtx.lineWidth = lineWidth;
    oscCtx.beginPath();
    for (let i = 0; i < telemetry.length; i++) {
      const x = (i / (telemetry.length - 1)) * w;
      const val = Math.max(0, Math.min(1, getter(telemetry[i])));
      const y = h - 5 - val * (h - 10);
      if (i === 0) oscCtx.moveTo(x, y);
      else oscCtx.lineTo(x, y);
    }
    oscCtx.stroke();
  };

  plotTrace(s => s.f, 'rgba(255, 100, 140, 0.78)', 1.4);          // Fear
  plotTrace(s => (s.d + 0.5) / 2.5, 'rgba(0, 255, 149, 0.8)', 1.5); // Dopamine normalized
  plotTrace(s => s.a, '#00e5ff', 2.1);                            // Mean firing activity
}

// -----------------------------------------------------------------------------
// Apply Incoming C++ Backend Telemetry State
// -----------------------------------------------------------------------------
function applyIncomingState(state) {
  if (!state || !state.metadata) return;
  latestState = state;
  viewport.ingestState(state);

  const m = state.metadata;
  const e = state.endocrine;

  document.getElementById('backend-status').textContent =
    `C++20 AVX2 Live (${api.lastLatencyMs.toFixed(1)}ms)`;
  document.getElementById('backend-dot').style.background = '#00ff95';

  document.getElementById('t-tick').textContent = m.tick;
  document.getElementById('t-neurons').textContent = m.total_neurons;
  document.getElementById('t-holons').textContent = m.composite_neurons;
  document.getElementById('t-synapses').textContent = m.total_synapses;
  document.getElementById('t-depth').textContent = m.max_depth;
  document.getElementById('t-step-us').textContent = Math.round(m.last_step_us || 0);

  document.getElementById('kpi-theta').textContent = (e.effective_threshold || 0).toFixed(3);
  document.getElementById('kpi-lr').textContent = (e.effective_learning_rate || 0).toFixed(3);

  if (!sliderBeingDragged) {
    document.getElementById('sl-fear').value = e.fear;
    document.getElementById('sl-dopamine').value = e.dopamine;
    document.getElementById('sl-fatigue').value = e.fatigue;
    document.getElementById('sl-curiosity').value = e.curiosity;
    document.getElementById('lbl-fear').textContent = e.fear.toFixed(2);
    document.getElementById('lbl-dopamine').textContent = e.dopamine.toFixed(2);
    document.getElementById('lbl-fatigue').textContent = e.fatigue.toFixed(2);
    document.getElementById('lbl-curiosity').textContent = e.curiosity.toFixed(2);
  }

  // Render Terminal Output Brightness Bars + Targeted Conditioning Buttons
  const termContainer = document.getElementById('terminal-container');
  const terms = state.terminals || [];
  let bestId = -1, bestVal = -1;
  for (const t of terms) {
    if (t.activation > bestVal) { bestVal = t.activation; bestId = t.id; }
  }

  termContainer.innerHTML = terms.map(t => {
    const pct = Math.round(Math.max(0, Math.min(1, t.activation)) * 100);
    const win = t.id === bestId && t.activation > 0.05;
    return `
      <div class="term-row">
        <div class="term-hdr">
          <span style="color:${win ? '#ffffff' : '#9bb3d6'};font-weight:${win ? '700' : '500'};">
            ${win ? '★ ' : ''}${t.label.replace('Output.', '')}
          </span>
          <div style="display:flex;align-items:center;gap:6px;">
            <strong style="color:${win ? '#00ff95' : '#8ea5cc'};font-variant-numeric:tabular-nums;">
              ${t.activation.toFixed(3)}
            </strong>
            <button class="btn-sm" data-train-id="${t.id}" title="Reward & Train Brain toward this Terminal">🎯</button>
          </div>
        </div>
        <div class="term-track">
          <div class="term-fill" style="width:${pct}%;opacity:${0.35 + 0.65 * (pct / 100)};"></div>
        </div>
      </div>
    `;
  }).join('');

  termContainer.querySelectorAll('[data-train-id]').forEach(btn => {
    btn.onclick = async () => {
      const tid = parseInt(btn.dataset.trainId, 10);
      const next = await api.sendFeedback({ valence: 1.25, targetId: tid });
      applyIncomingState(next);
      showToast(`🎯 Conditioned Pathway → Terminal #${tid} (+Dopamine Backprop)`);
    };
  });

  drawOscilloscope(state.telemetry);
}

// -----------------------------------------------------------------------------
// High-Frequency Polling & 60-120 FPS Render Loop
// -----------------------------------------------------------------------------
let pollInFlight = false;
let pollCount = 0;

setInterval(async () => {
  if (pollInFlight) return;
  pollInFlight = true;
  try {
    const state = await api.getState();
    applyIncomingState(state);
    pollCount++;
    if (selectedNeuronId !== null && pollCount % 8 === 0) {
      await refreshNeuronInspector(selectedNeuronId);
    }
  } catch (_) {
    document.getElementById('backend-status').textContent = 'Backend Reconnecting (:8085)...';
    document.getElementById('backend-dot').style.background = '#ff3b6b';
  } finally {
    pollInFlight = false;
  }
}, 65);

function renderLoop(nowMs) {
  requestAnimationFrame(renderLoop);
  viewport.renderFrame(nowMs * 0.001);
}
requestAnimationFrame(renderLoop);
