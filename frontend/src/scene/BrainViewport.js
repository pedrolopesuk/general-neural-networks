import { getTheme, lerpRgb, rgba } from '../theme.js';

export class BrainViewport {
  constructor(container, { onSelectNeuron, onHoverNeuron, onZoomTierChange, onAutoThreshold }) {
    this.container = container;
    this.onSelectNeuron = onSelectNeuron;
    this.onHoverNeuron = onHoverNeuron;
    this.onZoomTierChange = onZoomTierChange;
    this.onAutoThreshold = onAutoThreshold;

    this.lens = 'activity';
    this.is3D = false;
    this.selectedId = null;
    this.hoveredId = null;
    this.weightThreshold = null; // Auto-initialized to 80th percentile (~20% visible)

    this.nodes = [];
    this.nodeMap = new Map();
    this.synapses = [];
    this.flashState = new Map(); // id -> { prevAct, emaAct, flashMs }

    // Camera state (2D pan/zoom + 3D orbit angles)
    this.cam = { x: 0, y: 0, zoom: 1.0 };
    this.camAnim = null;
    this.rot3D = { yaw: 0.35, pitch: 0.25 };

    this._initCanvas();
    this._bindEvents();
  }

  _initCanvas() {
    this.canvas = document.createElement('canvas');
    this.canvas.style.display = 'block';
    this.canvas.style.width = '100%';
    this.canvas.style.height = '100%';
    this.container.innerHTML = '';
    this.container.appendChild(this.canvas);
    this.ctx = this.canvas.getContext('2d');
    this._resize();
  }

  _resize() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    const w = this.container.clientWidth || (window.innerWidth - 260);
    const h = this.container.clientHeight || (window.innerHeight - 32);
    this.width = w;
    this.height = h;
    this.dpr = dpr;
    this.canvas.width = Math.round(w * dpr);
    this.canvas.height = Math.round(h * dpr);
  }

  _bindEvents() {
    window.addEventListener('resize', () => this._resize());

    let dragging = false;
    let moved = false;
    let startX = 0, startY = 0;
    let lastX = 0, lastY = 0;

    this.canvas.addEventListener('pointerdown', (e) => {
      dragging = true;
      moved = false;
      startX = lastX = e.clientX;
      startY = lastY = e.clientY;
    });

    window.addEventListener('pointerup', (e) => {
      if (!dragging) return;
      dragging = false;
      if (e.target !== this.canvas) return;

      if (!moved) {
        const hit = this._hitTestNode(e.clientX, e.clientY);
        if (hit) {
          this.selectedId = hit.id;
          if (this.onSelectNeuron) this.onSelectNeuron(hit.id, e.clientX, e.clientY);
        } else {
          this.selectedId = null;
          if (this.onSelectNeuron) this.onSelectNeuron(null, 0, 0);
        }
      }
    });

    this.canvas.addEventListener('pointermove', (e) => {
      const dx = e.clientX - lastX;
      const dy = e.clientY - lastY;
      lastX = e.clientX;
      lastY = e.clientY;

      if (dragging) {
        if (Math.hypot(e.clientX - startX, e.clientY - startY) > 4) moved = true;
        if (this.is3D) {
          this.rot3D.yaw += dx * 0.006;
          this.rot3D.pitch = Math.max(-1.2, Math.min(1.2, this.rot3D.pitch + dy * 0.006));
        } else {
          this.cam.x -= dx / this.cam.zoom;
          this.cam.y -= dy / this.cam.zoom;
        }
        return;
      }

      const hit = this._hitTestNode(e.clientX, e.clientY);
      this.hoveredId = hit ? hit.id : null;
      this.canvas.style.cursor = hit ? 'pointer' : 'default';
      if (this.onHoverNeuron) {
        this.onHoverNeuron(hit || null, e.clientX, e.clientY);
      }
    });

    this.canvas.addEventListener('pointerleave', () => {
      this.hoveredId = null;
      if (this.onHoverNeuron) this.onHoverNeuron(null, 0, 0);
    });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.14 : 1 / 1.14;
      const nextZoom = Math.max(0.45, Math.min(14.0, this.cam.zoom * factor));
      const rect = this.canvas.getBoundingClientRect();
      const sx = e.clientX - rect.left - this.width * 0.5;
      const sy = e.clientY - rect.top - this.height * 0.5;

      if (!this.is3D) {
        const wx = this.cam.x + sx / this.cam.zoom;
        const wy = this.cam.y + sy / this.cam.zoom;
        this.cam.zoom = nextZoom;
        this.cam.x = wx - sx / nextZoom;
        this.cam.y = wy - sy / nextZoom;
      } else {
        this.cam.zoom = nextZoom;
      }
      this._emitZoomTier();
    }, { passive: false });
  }

  _emitZoomTier() {
    const tier = this.cam.zoom < 1.55 ? 'macro' : (this.cam.zoom < 3.6 ? 'cluster' : 'micro');
    if (tier !== this._lastTier) {
      this._lastTier = tier;
      if (this.onZoomTierChange) this.onZoomTierChange(tier);
    }
  }

  setLens(mode) {
    this.lens = mode;
  }

  set3DMode(flag) {
    this.is3D = Boolean(flag);
    this.animateCameraTo(0, 0, 1.0, 280);
  }

  setWeightThreshold(val) {
    this.weightThreshold = val;
  }

  clearSelection() {
    this.selectedId = null;
  }

  animateCameraTo(x, y, zoom, durationMs = 320) {
    this.camAnim = {
      fromX: this.cam.x,
      fromY: this.cam.y,
      fromZ: this.cam.zoom,
      toX: x,
      toY: y,
      toZ: zoom,
      startMs: performance.now(),
      durationMs
    };
  }

  zoomToMacro() {
    this.animateCameraTo(0, 0, 1.0, 320);
  }

  zoomToClusterDefault() {
    const n = this.nodes.find(d => d.is_composite && d.depth === 0);
    if (n) this.selectAndFlyTo(n.id);
  }

  zoomToMicroDefault() {
    const n = this.nodes.find(d => d.is_composite && d.depth >= 1) ||
              this.nodes.find(d => d.depth >= 2);
    if (n) this.selectAndFlyTo(n.id);
  }

  stepZoomOut() {
    if (this.cam.zoom > 3.2) {
      this.animateCameraTo(this.cam.x * 0.5, this.cam.y * 0.5, 2.0, 280);
    } else {
      this.zoomToMacro();
    }
  }

  selectAndFlyTo(id) {
    this.selectedId = id;
    const n = this.nodeMap.get(id);
    if (!n || n._wx === undefined) return;
    const targetZoom = n.depth === 0 ? 2.4 : (n.depth === 1 ? 4.8 : 7.5);
    this.animateCameraTo(n._wx, n._wy, targetZoom, 320);
  }

  ingestState(state) {
    if (!state || !state.flat_nodes) return;
    const nowMs = performance.now();

    this.nodes = state.flat_nodes;
    this.synapses = state.synapses || [];
    this.nodeMap.clear();

    for (const n of this.nodes) {
      this.nodeMap.set(n.id, n);
      const act = n.activation || 0;
      let st = this.flashState.get(n.id);
      if (!st) {
        st = { prevAct: act, emaAct: act, flashMs: -9999 };
        this.flashState.set(n.id, st);
      } else {
        const delta = act - st.prevAct;
        const sinceLast = nowMs - st.flashMs;
        // Detect genuine spike / rising burst above running baseline
        if ((delta > 0.055 && act > 0.35) || (act > st.emaAct + 0.08 && sinceLast > 420)) {
          st.flashMs = nowMs;
        }
        st.emaAct = st.emaAct * 0.92 + act * 0.08;
        st.prevAct = act;
      }
    }

    // Auto-compute 80th-percentile |weight| threshold on first ingest (~20% visible edges)
    if (this.weightThreshold === null && this.synapses.length > 0) {
      const absWeights = this.synapses.map(s => Math.abs(s.weight)).sort((a, b) => a - b);
      const idx80 = Math.min(absWeights.length - 1, Math.floor(absWeights.length * 0.80));
      const maxW = Math.max(1.0, absWeights[absWeights.length - 1]);
      this.weightThreshold = absWeights[idx80];
      if (this.onAutoThreshold) {
        this.onAutoThreshold(this.weightThreshold, maxW);
      }
    }
  }

  _projectNode(n) {
    const cx = this.width * 0.5;
    const cy = this.height * 0.5;

    if (this.is3D) {
      const cosY = Math.cos(this.rot3D.yaw);
      const sinY = Math.sin(this.rot3D.yaw);
      const cosP = Math.cos(this.rot3D.pitch);
      const sinP = Math.sin(this.rot3D.pitch);

      const x1 = n.x * cosY - n.z * sinY;
      const z1 = n.x * sinY + n.z * cosY;
      const y2 = n.y * cosP - z1 * sinP;
      const z2 = n.y * sinP + z1 * cosP;

      const fov = 520 * this.cam.zoom;
      const scale = fov / Math.max(120, 520 + z2);
      n._wx = x1 * 1.4;
      n._wy = -y2 * 1.4;
      n._sx = cx + x1 * 1.4 * scale;
      n._sy = cy - y2 * 1.4 * scale;
      n._depthSort = z2;
      return;
    }

    // 2D Planar Projection (replaced by full nested Circle Packing in Task 6)
    const wx = n.x * 1.55;
    const wy = n.z * 1.35;
    n._wx = wx;
    n._wy = wy;
    n._sx = cx + (wx - this.cam.x) * this.cam.zoom;
    n._sy = cy + (wy - this.cam.y) * this.cam.zoom;
    n._depthSort = n.depth;
  }

  _hitTestNode(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const mx = clientX - rect.left;
    const my = clientY - rect.top;

    let best = null;
    let bestDist = 11;
    for (const n of this.nodes) {
      if (n._sx === undefined) continue;
      const d = Math.hypot(mx - n._sx, my - n._sy);
      if (d < bestDist) {
        bestDist = d;
        best = n;
      }
    }
    return best;
  }

  _computeDotRadius(n) {
    const childBoost = Math.min(3.5, (n.child_count || 0) * 0.85);
    const actBoost = (n.activation || 0) * 1.5;
    return Math.max(3.0, Math.min(8.0, 3.0 + childBoost + actBoost));
  }

  renderFrame() {
    const nowMs = performance.now();
    const theme = getTheme();
    const ctx = this.ctx;

    // Step smooth ease-out camera transition (250-400ms)
    if (this.camAnim) {
      const t = Math.min(1, (nowMs - this.camAnim.startMs) / this.camAnim.durationMs);
      const ease = 1 - Math.pow(1 - t, 3);
      this.cam.x = this.camAnim.fromX + (this.camAnim.toX - this.camAnim.fromX) * ease;
      this.cam.y = this.camAnim.fromY + (this.camAnim.toY - this.camAnim.fromY) * ease;
      this.cam.zoom = this.camAnim.fromZ + (this.camAnim.toZ - this.camAnim.fromZ) * ease;
      if (t >= 1) this.camAnim = null;
      this._emitZoomTier();
    }

    ctx.save();
    ctx.scale(this.dpr, this.dpr);
    ctx.fillStyle = theme.bg;
    ctx.fillRect(0, 0, this.width, this.height);

    for (const n of this.nodes) {
      this._projectNode(n);
    }

    // Build focus neighbor set for hover/selection isolation
    const focusId = this.hoveredId !== null ? this.hoveredId : this.selectedId;
    const focusNeighbors = new Set();
    if (focusId !== null) {
      focusNeighbors.add(focusId);
      for (const s of this.synapses) {
        if (s.source === focusId) focusNeighbors.add(s.target);
        if (s.target === focusId) focusNeighbors.add(s.source);
      }
    }

    const thresh = this.weightThreshold ?? 0.45;

    // Compute holon / region centroids so root sensory edges bundle cleanly instead of fanning out
    const regionCentroids = new Map();
    for (const n of this.nodes) {
      if (n.role === 'CorticalComposite' && n.depth === 0) {
        const reg = n.label.split('.')[0];
        let acc = regionCentroids.get(reg);
        if (!acc) {
          acc = { sx: 0, sy: 0, count: 0 };
          regionCentroids.set(reg, acc);
        }
        acc.sx += n._sx;
        acc.sy += n._sy;
        acc.count += 1;
      }
    }

    const drawnRootBundles = new Set();

    // 1. Draw Filtered & Bundled Edges
    ctx.lineWidth = 1;
    for (const s of this.synapses) {
      const absW = Math.abs(s.weight);
      const isFocusedEdge = focusId !== null && (s.source === focusId || s.target === focusId);

      if (!isFocusedEdge && absW < thresh) continue;

      const src = this.nodeMap.get(s.source);
      const dst = this.nodeMap.get(s.target);
      if (!src || !dst) continue;

      const srcFlash = this.flashState.get(src.id);
      const flashAge = srcFlash ? (nowMs - srcFlash.flashMs) : 9999;
      const isCarryingSignal = flashAge >= 0 && flashAge < 260 && (src.activation || 0) > 0.3;

      let strokeStyle;
      if (focusId !== null && !isFocusedEdge) {
        strokeStyle = rgba(theme.rgb.line, 0.12);
      } else if (isCarryingSignal) {
        const fAlpha = Math.max(0.25, 1 - flashAge / 260);
        strokeStyle = rgba(theme.rgb.signal, 0.85 * fAlpha);
      } else {
        const normW = Math.min(1, (absW - thresh) / Math.max(0.2, 1.5 - thresh));
        const rgb = lerpRgb(theme.rgb.line, theme.rgb.textDim, normW);
        const alpha = isFocusedEdge ? 0.88 : (0.28 + 0.55 * normW);
        strokeStyle = rgba(rgb, alpha);
      }

      // Bundle root sensory edges to region holon boundary rather than fanning to every node
      if (src.role === 'RootSensory' && dst.role === 'CorticalComposite' && focusId === null) {
        const reg = dst.label.split('.')[0];
        const bundleKey = `${src.id}:${reg}`;
        if (drawnRootBundles.has(bundleKey)) continue;
        drawnRootBundles.add(bundleKey);

        const c = regionCentroids.get(reg);
        if (c && c.count > 0) {
          const tx = c.sx / c.count;
          const ty = c.sy / c.count;
          const midX = src._sx + (tx - src._sx) * 0.55;
          ctx.strokeStyle = strokeStyle;
          ctx.beginPath();
          ctx.moveTo(src._sx, src._sy);
          ctx.quadraticCurveTo(midX, src._sy, tx, ty);
          ctx.stroke();
          continue;
        }
      }

      ctx.strokeStyle = strokeStyle;
      ctx.beginPath();
      ctx.moveTo(src._sx, src._sy);
      ctx.lineTo(dst._sx, dst._sy);
      ctx.stroke();
    }

    // 2. Draw Solid Neuron Dots (3-8px) + 300ms Firing Flash Decay + 1px Selection Ring
    for (const n of this.nodes) {
      const r = this._computeDotRadius(n);
      const dimmed = focusId !== null && !focusNeighbors.has(n.id);

      let rgb;
      if (this.lens === 'age') {
        const mat = Math.max(0, Math.min(1, n.maturity ?? 0));
        rgb = lerpRgb(theme.rgb.young, theme.rgb.old, mat);
      } else {
        const st = this.flashState.get(n.id);
        const elapsed = st ? (nowMs - st.flashMs) : 9999;
        if (elapsed >= 0 && elapsed < 320) {
          // Exponential decay over ~300ms from --signal back to --text-dim
          const flashT = Math.exp(-elapsed / 120);
          rgb = lerpRgb(theme.rgb.textDim, theme.rgb.signal, flashT);
        } else {
          rgb = theme.rgb.textDim;
        }
      }

      ctx.fillStyle = rgba(rgb, dimmed ? 0.2 : 1.0);
      ctx.beginPath();
      ctx.arc(n._sx, n._sy, r, 0, Math.PI * 2);
      ctx.fill();

      if (n.id === this.selectedId || n.id === this.hoveredId) {
        ctx.strokeStyle = theme.signal;
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(n._sx, n._sy, r + 3.5, 0, Math.PI * 2);
        ctx.stroke();
      }
    }

    ctx.restore();
  }
}
