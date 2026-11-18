import { MetaBrainBackendClient } from './api/backendClient.js';
import { BrainViewport } from './scene/BrainViewport.js';
import { Oscilloscope } from './trace/Oscilloscope.js';
import { getTheme, lerpRgb } from './theme.js';

const api = new MetaBrainBackendClient();
const theme = getTheme();

let latestState = null;
let selectedNeuronId = null;
let selectedNeuronVoltage = null;
let lastAnchorPos = { x: 240, y: 160 };
let simRunning = true;
let is3DMode = false;
let autoGrow = false;
let sliderBeingDragged = false;

function formatOutputName(raw) {
  let s = raw.replace(/^Output\./, '').replaceAll('_', ' ');
  s = s.replace(/\bAlpha\b/gi, 'A').replace(/\bBeta\b/gi, 'B');
  if (s.length > 1) {
    s = s.charAt(0).toUpperCase() + s.slice(1).toLowerCase();
    s = s.replace(/\bpattern a\b/i, 'Pattern A').replace(/\bpattern b\b/i, 'Pattern B');
  }
  return s;
}

function formatNodeName(raw) {
  if (!raw) return '';
  return raw.replace(/^Output\./, '').replace(/^Sensory\./, '').replaceAll('_', ' ');
}

let toastTimer = null;
function showToast(msg) {
  const el = document.getElementById('toast-banner');
  el.textContent = msg;
  el.style.display = 'block';
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { el.style.display = 'none'; }, 2800);
}

function drawLegendRamp(mode) {
  const c = document.getElementById('legend-canvas');
  if (!c) return;
  const ctx = c.getContext('2d');
  const w = c.width;
  const h = c.height;
  for (let x = 0; x < w; x++) {
    const t = x / Math.max(1, w - 1);
    const rgb = mode === 'activity'
      ? lerpRgb(theme.rgb.textDim, theme.rgb.signal, t)
      : lerpRgb(theme.rgb.young, theme.rgb.old, t);
    ctx.fillStyle = `rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})`;
    ctx.fillRect(x, 0, 1, h);
  }
}
drawLegendRamp('activity');

const stageEl = document.getElementById('canvas-stage');
const popoverEl = document.getElementById('inspector-popover');

function closeInspector() {
  selectedNeuronId = null;
  selectedNeuronVoltage = null;
  popoverEl.style.display = 'none';
  if (viewport.clearSelection) viewport.clearSelection();
  else viewport.selectedId = null;
}

function positionPopover(clientX, clientY) {
  const rect = stageEl.getBoundingClientRect();
  const relX = clientX - rect.left;
  const relY = clientY - rect.top;
  const popW = 242;
  const popH = popoverEl.offsetHeight || 220;

  let left = relX + 14;
  let top = relY - 16;
  if (left + popW > rect.width - 12) left = Math.max(12, relX - popW - 14);
  if (top + popH > rect.height - 12) top = Math.max(12, rect.height - popH - 12);
  if (top < 12) top = 12;

  popoverEl.style.left = `${Math.round(left)}px`;
  popoverEl.style.top = `${Math.round(top)}px`;
}

const viewport = new BrainViewport(document.getElementById('viewport-root'), {
  onSelectNeuron: async (id, clientX, clientY) => {
    document.getElementById('hover-card').style.display = 'none';
    if (id === null) {
      closeInspector();
      return;
    }
    selectedNeuronId = id;
    if (clientX && clientY) {
      lastAnchorPos = { x: clientX, y: clientY };
    }
    await refreshNeuronInspector(id, true);
    updateBreadcrumbs(id);
  },
  onHoverNeuron: (node, clientX, clientY) => {
    const card = document.getElementById('hover-card');
    if (!node || selectedNeuronId === node.id) {
      card.style.display = 'none';
      return;
    }
    const rect = stageEl.getBoundingClientRect();
    card.style.display = 'block';
    card.style.left = `${Math.min(rect.width - 170, clientX - rect.left + 12)}px`;
    card.style.top = `${Math.max(8, clientY - rect.top + 12)}px`;
    card.innerHTML = `
      <div>${formatNodeName(node.label)}</div>
      <div class="mono" style="color:var(--text-dim);">act ${(node.activation || 0).toFixed(3)} · age ${node.age_ticks}</div>
    `;
  },
  onZoomTierChange: (tier) => {
    document.getElementById('btn-z-macro').classList.toggle('active', tier === 'macro');
    document.getElementById('btn-z-cluster').classList.toggle('active', tier === 'cluster');
    document.getElementById('btn-z-micro').classList.toggle('active', tier === 'micro');
  },
  onAutoThreshold: (val, maxW) => {
    const sl = document.getElementById('sl-edge-thresh');
    if (sl) {
      sl.max = Math.max(1.0, maxW).toFixed(2);
      sl.value = val.toFixed(2);
    }
    const lbl = document.getElementById('lbl-edge-thresh');
    if (lbl) lbl.textContent = val.toFixed(2);
  },
  onBreadcrumbChange: (crumbs) => {
    renderHolonBreadcrumbs(crumbs);
  }
});

window.addEventListener('keydown', (e) => {
  if (e.key === 'Escape') {
    if (popoverEl.style.display !== 'none') {
      closeInspector();
    } else if (viewport.stepZoomOut) {
      viewport.stepZoomOut();
    }
  }
});

// Retina 16x16 Canvas
const retinaCanvas = document.getElementById('retina-canvas');
const rCtx = retinaCanvas.getContext('2d');
const retinaGrid = new Float32Array(16 * 16);

function renderRetinaPad() {
  const img = rCtx.createImageData(16, 16);
  for (let i = 0; i < 256; i++) {
    const v = Math.max(0, Math.min(1, retinaGrid[i]));
    const rgb = lerpRgb(theme.rgb.bg, theme.rgb.text, v);
    const p = i * 4;
    img.data[p]     = rgb[0];
    img.data[p + 1] = rgb[1];
    img.data[p + 2] = rgb[2];
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
};

// Manual Channels
const manualValues = [0.85, 0.2, 0.65, 0.1, 0.9, 0.15, 0.45, 0.3];
const manualContainer = document.getElementById('manual-channels');

function renderManualFaders() {
  manualContainer.innerHTML = manualValues.map((v, i) => `
    <div class="ch-col">
      <div class="ch-bar-wrap" data-ch="${i}">
        <div class="ch-bar-fill" style="height:${Math.round(v * 100)}%"></div>
      </div>
      <span class="ch-lbl">${i + 1}</span>
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
};

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

async function triggerTextPulse(txt) {
  if (!txt) return;
  const state = await api.injectText(txt, 18);
  applyIncomingState(state);
}
document.getElementById('btn-send-text').onclick = () => {
  const val = document.getElementById('inp-text').value.trim() || 'HARMONY_REWARD_EXPLORE_SIGNAL';
  triggerTextPulse(val);
};
document.querySelectorAll('[data-preset]').forEach(btn => {
  btn.onclick = () => triggerTextPulse(btn.dataset.preset);
});

function switchLens(mode) {
  viewport.setLens(mode);
  document.getElementById('btn-lens-activity').classList.toggle('active', mode === 'activity');
  document.getElementById('btn-lens-age').classList.toggle('active', mode === 'age');
  drawLegendRamp(mode);
  document.getElementById('lens-labels').textContent =
    mode === 'activity' ? 'idle → firing' : 'new → established';
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

// Edge threshold slider
const edgeSl = document.getElementById('sl-edge-thresh');
edgeSl.addEventListener('input', () => {
  const v = parseFloat(edgeSl.value);
  document.getElementById('lbl-edge-thresh').textContent = v.toFixed(2);
  if (viewport.setWeightThreshold) viewport.setWeightThreshold(v);
});

document.getElementById('btn-reward').onclick = async () => {
  const state = await api.sendFeedback({ valence: 1.15 });
  applyIncomingState(state);
};

document.getElementById('btn-punish').onclick = async () => {
  const state = await api.sendFeedback({ valence: -1.0 });
  applyIncomingState(state);
};

document.getElementById('btn-add-child').onclick = async () => {
  const targetParent = selectedNeuronId !== null ? selectedNeuronId : -1;
  const state = await api.addChildSubNetwork(targetParent, 3);
  applyIncomingState(state);
  if (selectedNeuronId !== null) await refreshNeuronInspector(selectedNeuronId, false);
};

document.getElementById('btn-auto-grow').onclick = async (e) => {
  autoGrow = !autoGrow;
  await api.setControl({ auto_neurogenesis: autoGrow });
  e.target.classList.toggle('active', autoGrow);
  if (autoGrow) {
    const state = await api.addChildSubNetwork(-1, 3);
    applyIncomingState(state);
  }
};

document.getElementById('btn-synaptogenesis').onclick = async () => {
  const state = await api.triggerSynaptogenesis();
  applyIncomingState(state);
  showToast('Wired co-active neurons');
};

document.getElementById('btn-run-toggle').onclick = async (e) => {
  simRunning = !simRunning;
  await api.setControl({ running: simRunning });
  e.target.textContent = simRunning ? 'Pause · 18 Hz' : 'Run · 18 Hz';
};

document.getElementById('btn-3d-toggle').onclick = (e) => {
  is3DMode = !is3DMode;
  e.target.classList.toggle('active', is3DMode);
  if (viewport.set3DMode) viewport.set3DMode(is3DMode);
};

document.getElementById('btn-save').onclick = async () => {
  const res = await api.saveSnapshots();
  if (res.saved) {
    showToast(`Saved ${res.json_path}`);
  }
};

document.getElementById('btn-bench').onclick = async (e) => {
  const orig = e.target.textContent;
  e.target.textContent = 'Running…';
  try {
    const rep = await api.runBenchmark(10000);
    showToast(
      `${rep.total_neurons} neurons, ${rep.total_synapses} synapses · ${rep.forward_msyn_s.toFixed(0)} MSyn/s (${rep.forward_ms.toFixed(2)} ms/tick)`
    );
  } finally {
    e.target.textContent = orig;
  }
};

document.getElementById('btn-reset').onclick = async () => {
  closeInspector();
  const state = await api.resetBrain({ roots: 8, cortical: 20, terminals: 4 });
  applyIncomingState(state);
  viewport.zoomToMacro();
};

document.getElementById('btn-z-macro').onclick = () => {
  viewport.zoomToMacro();
  updateBreadcrumbs(null);
};
document.getElementById('btn-z-cluster').onclick = () => {
  viewport.zoomToClusterDefault();
};
document.getElementById('btn-z-micro').onclick = () => {
  viewport.zoomToMicroDefault();
};

function renderHolonBreadcrumbs(crumbs) {
  const bc = document.getElementById('breadcrumbs');
  bc.innerHTML = crumbs.map((c, idx) => {
    const isLast = idx === crumbs.length - 1;
    const sep = idx > 0 ? '<span>/</span>' : '';
    return `${sep}<span class="crumb ${isLast ? 'active' : ''}" data-hkey="${c.key}">${c.label}</span>`;
  }).join('');

  bc.querySelectorAll('[data-hkey]').forEach(el => {
    el.onclick = () => {
      const k = el.dataset.hkey;
      if (k === 'macro') {
        closeInspector();
        viewport.zoomToMacro();
      } else {
        viewport.zoomToHolon(k);
      }
    };
  });
}

function updateBreadcrumbs(id) {
  if (id === null) {
    renderHolonBreadcrumbs([{ label: 'Macro', key: 'macro' }]);
    return;
  }
  const n = latestState?.flat_nodes?.find(d => d.id === id);
  if (!n) return;

  const chain = [];
  let cur = n;
  while (cur) {
    chain.unshift(cur);
    cur = cur.parent_id >= 0 ? latestState.flat_nodes.find(d => d.id === cur.parent_id) : null;
  }

  const crumbs = [{ label: 'Macro', key: 'macro' }];
  const topNode = chain[0];
  if (topNode && topNode.role === 'CorticalComposite') {
    const reg = topNode.label.split('.')[0];
    crumbs.push({ label: reg, key: `region:${reg}` });
  }
  for (const item of chain) {
    const shortName = item.label.includes('.')
      ? item.label.split('.').slice(1).join('.').replaceAll('_', ' ')
      : formatNodeName(item.label);
    crumbs.push({ label: shortName, key: `node:${item.id}` });
  }

  const bc = document.getElementById('breadcrumbs');
  bc.innerHTML = crumbs.map((c, idx) => {
    const isLast = idx === crumbs.length - 1;
    const sep = idx > 0 ? '<span>/</span>' : '';
    return `${sep}<span class="crumb ${isLast ? 'active' : ''}" data-ckey="${c.key}">${c.label}</span>`;
  }).join('');

  bc.querySelectorAll('[data-ckey]').forEach(el => {
    el.onclick = async () => {
      const k = el.dataset.ckey;
      if (k === 'macro') {
        closeInspector();
        viewport.zoomToMacro();
      } else if (k.startsWith('region:')) {
        viewport.zoomToHolon(k);
      } else if (k.startsWith('node:')) {
        const nid = parseInt(k.slice(5), 10);
        selectedNeuronId = nid;
        viewport.selectAndFlyTo(nid);
        await refreshNeuronInspector(nid, false);
      }
    };
  });
}

async function refreshNeuronInspector(id, reposition = false) {
  if (id === null) return;
  try {
    const d = await api.getNeuronDetail(id);
    if (d.error) return;

    const inSyns = d.incoming_synapses || [];
    const outSyns = d.outgoing_synapses || [];
    const allConns = [
      ...inSyns.map(s => ({ dir: 'in', label: formatNodeName(s.source_label), w: s.weight })),
      ...outSyns.map(s => ({ dir: 'out', label: formatNodeName(s.target_label), w: s.weight }))
    ];

    const connHtml = allConns.length > 0
      ? `<div class="conn-list">
           ${allConns.map(c => `
             <div class="conn-row">
               <span style="color:var(--text-dim);">${c.dir === 'in' ? '←' : '→'} ${c.label}</span>
               <span>${c.w >= 0 ? '+' : ''}${c.w.toFixed(3)}</span>
             </div>
           `).join('')}
         </div>`
      : `<div class="dim-label" style="margin-top:4px;">No connections</div>`;

    const canGrow = d.role !== 'RootSensory' && d.role !== 'TerminalReadout';

    popoverEl.innerHTML = `
      <div class="pop-title-row">
        <span class="pop-title">${formatNodeName(d.label)}</span>
        <button class="link-btn" id="pop-close" title="Close (Esc)">Esc</button>
      </div>
      <div class="pop-grid">
        <div>role <strong>${d.role}</strong></div>
        <div>depth <strong>${d.depth}</strong></div>
        <div>bias <strong>${d.bias.toFixed(3)}</strong></div>
        <div>voltage <strong>${d.potential.toFixed(3)}</strong></div>
        <div>activation <strong>${d.activation.toFixed(3)}</strong></div>
        <div>age <strong>${d.age_ticks}</strong></div>
        <div>children <strong>${d.children.length}</strong></div>
      </div>
      <div class="pop-links">
        <button class="link-btn" id="pop-zoom">Zoom in</button>
        ${canGrow ? `<button class="link-btn" id="pop-add-children">Add 3 children</button>` : ''}
      </div>
      <div class="dim-label">Connections · ${inSyns.length} in, ${outSyns.length} out</div>
      ${connHtml}
    `;

    selectedNeuronVoltage = d.potential;

    popoverEl.style.display = 'block';
    if (reposition) {
      positionPopover(lastAnchorPos.x, lastAnchorPos.y);
    }

    document.getElementById('pop-close').onclick = closeInspector;
    document.getElementById('pop-zoom').onclick = () => viewport.selectAndFlyTo(d.id);
    const addBtn = document.getElementById('pop-add-children');
    if (addBtn) {
      addBtn.onclick = async () => {
        const state = await api.addChildSubNetwork(d.id, 3);
        applyIncomingState(state);
        await refreshNeuronInspector(d.id, false);
      };
    }
  } catch (_) {}
}

const scope = new Oscilloscope(
  document.getElementById('oscilloscope'),
  document.getElementById('trace-legend'),
  document.getElementById('trace-status')
);

function applyIncomingState(state) {
  if (!state || !state.metadata) return;
  latestState = state;
  viewport.ingestState(state);

  const m = state.metadata;
  const e = state.endocrine;

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

  // Compact Output Table
  const tbody = document.getElementById('terminal-tbody');
  const terms = state.terminals || [];
  let bestId = -1, bestVal = -1;
  for (const t of terms) {
    if (t.activation > bestVal) { bestVal = t.activation; bestId = t.id; }
  }

  tbody.innerHTML = terms.map(t => {
    const pct = Math.round(Math.max(0, Math.min(1, t.activation)) * 100);
    const win = t.id === bestId && t.activation > 0.05;
    return `
      <tr class="${win ? 'winner-row' : ''}">
        <td class="out-name">${formatOutputName(t.label)}</td>
        <td class="out-bar-cell">
          <div class="out-bar-bg">
            <div class="out-bar-fill" style="width:${pct}%;"></div>
          </div>
        </td>
        <td class="out-val mono">${t.activation.toFixed(3)}</td>
        <td class="out-target-cell">
          <button class="out-target-btn" data-train-id="${t.id}" title="Train toward this output">Target</button>
        </td>
      </tr>
    `;
  }).join('');

  tbody.querySelectorAll('[data-train-id]').forEach(btn => {
    btn.onclick = async () => {
      const tid = parseInt(btn.dataset.trainId, 10);
      const next = await api.sendFeedback({ valence: 1.25, targetId: tid });
      applyIncomingState(next);
    };
  });

  scope.ingest(state, selectedNeuronVoltage);
}

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
      await refreshNeuronInspector(selectedNeuronId, false);
    }
  } catch (_) {}
  finally {
    pollInFlight = false;
  }
}, 65);

function renderLoop(nowMs) {
  requestAnimationFrame(renderLoop);
  viewport.renderFrame(nowMs * 0.001);
}
requestAnimationFrame(renderLoop);
