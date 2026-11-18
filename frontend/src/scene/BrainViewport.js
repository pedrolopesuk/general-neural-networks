import { hierarchy, pack } from 'd3-hierarchy';
import { getTheme, lerpRgb, rgba } from '../theme.js';

export class BrainViewport {
  constructor(container, {
    onSelectNeuron,
    onHoverNeuron,
    onZoomTierChange,
    onAutoThreshold,
    onBreadcrumbChange
  }) {
    this.container = container;
    this.onSelectNeuron = onSelectNeuron;
    this.onHoverNeuron = onHoverNeuron;
    this.onZoomTierChange = onZoomTierChange;
    this.onAutoThreshold = onAutoThreshold;
    this.onBreadcrumbChange = onBreadcrumbChange;

    this.lens = 'activity';
    this.is3D = false;
    this.selectedId = null;
    this.hoveredId = null;
    this.focusedHolonKey = null; // e.g. 'region:Prefrontal' or 'holon:9'
    this.weightThreshold = null;
    this.edgeCutoffRank = null;

    this.nodes = [];
    this.nodeMap = new Map();
    this.synapses = [];
    this.flashState = new Map();

    // Packed 2D geometry caches (recomputed only when topology changes or resize)
    this.topologyKey = '';
    this.packedPos = new Map();      // neuronId -> { x, y }
    this.holonCircles = [];          // { key, label, level, neuronId, parentKey, x, y, r, memberIds }

    // Camera state
    this.cam = { x: 0, y: 0, zoom: 1.0 };
    this.camAnim = null;
    this.rot3D = { yaw: 0.42, pitch: 0.24 };

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
    this.topologyKey = ''; // Trigger clean repack on resize
    if (this.nodes.length > 0) this._computePackedLayout();
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
        this._handleClick(e.clientX, e.clientY);
      }
    });

    this.canvas.addEventListener('dblclick', (e) => {
      const hitNode = this._hitTestNode(e.clientX, e.clientY);
      if (hitNode) {
        const holon = this.holonCircles.find(h => h.neuronId === hitNode.id);
        if (holon) {
          this.zoomToHolon(holon.key);
          return;
        }
        this.selectAndFlyTo(hitNode.id);
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

      const tier = this.getZoomTier();
      const hitNode = (tier !== 'macro' || this.is3D)
        ? this._hitTestNode(e.clientX, e.clientY)
        : this._hitTestRailNode(e.clientX, e.clientY);
      const hitHolon = (!hitNode && !this.is3D) ? this._hitTestHolon(e.clientX, e.clientY) : null;

      this.hoveredId = hitNode ? hitNode.id : null;
      this.canvas.style.cursor = (hitNode || hitHolon) ? 'pointer' : 'default';
      if (this.onHoverNeuron) {
        this.onHoverNeuron(hitNode || null, e.clientX, e.clientY);
      }
    });

    this.canvas.addEventListener('pointerleave', () => {
      this.hoveredId = null;
      if (this.onHoverNeuron) this.onHoverNeuron(null, 0, 0);
    });

    this.canvas.addEventListener('wheel', (e) => {
      e.preventDefault();
      const factor = e.deltaY < 0 ? 1.15 : 1 / 1.15;
      const nextZoom = Math.max(0.55, Math.min(18.0, this.cam.zoom * factor));
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

  _handleClick(clientX, clientY) {
    const tier = this.getZoomTier();

    // In Macro 2D view, clicking a top-level holon zooms smoothly into Cluster view
    if (!this.is3D && tier === 'macro') {
      const railHit = this._hitTestRailNode(clientX, clientY);
      if (railHit) {
        this.selectedId = railHit.id;
        if (this.onSelectNeuron) this.onSelectNeuron(railHit.id, clientX, clientY);
        return;
      }
      const holon = this._hitTestHolon(clientX, clientY, 1);
      if (holon) {
        this.zoomToHolon(holon.key);
        return;
      }
    }

    // Check individual neuron dot hit
    const hitNode = this._hitTestNode(clientX, clientY);
    if (hitNode) {
      this.selectedId = hitNode.id;
      if (this.onSelectNeuron) this.onSelectNeuron(hitNode.id, clientX, clientY);
      return;
    }

    // Check nested holon circle hit when in Cluster/Micro view
    if (!this.is3D) {
      const holon = this._hitTestHolon(clientX, clientY);
      if (holon && holon.key !== this.focusedHolonKey) {
        this.zoomToHolon(holon.key);
        return;
      }
    }

    // Clicked empty space -> clear selection
    this.selectedId = null;
    if (this.onSelectNeuron) this.onSelectNeuron(null, 0, 0);
  }

  getZoomTier() {
    if (this.cam.zoom < 1.55) return 'macro';
    if (this.cam.zoom < 3.8) return 'cluster';
    return 'micro';
  }

  _emitZoomTier() {
    const tier = this.getZoomTier();
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
    this.focusedHolonKey = null;
    this.animateCameraTo(0, 0, 1.0, 300);
    this._emitBreadcrumbs();
  }

  setWeightThreshold(val) {
    this.weightThreshold = val;
    this.edgeCutoffRank = null; // User took manual control of threshold slider
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
    this.focusedHolonKey = null;
    this.animateCameraTo(0, 0, 1.0, 320);
    this._emitBreadcrumbs();
  }

  zoomToClusterDefault() {
    const region = this.holonCircles.find(h => h.level === 1);
    if (region) this.zoomToHolon(region.key);
  }

  zoomToMicroDefault() {
    const subHolon = this.holonCircles.find(h => h.level >= 3) ||
                     this.holonCircles.find(h => h.level === 2);
    if (subHolon) this.zoomToHolon(subHolon.key);
  }

  zoomToHolon(key) {
    const h = this.holonCircles.find(c => c.key === key);
    if (!h) return;
    this.focusedHolonKey = h.key;
    const minDim = Math.min(this.width, this.height) * 0.38;
    const targetZoom = Math.max(1.75, Math.min(14.0, minDim / Math.max(8, h.r)));
    this.animateCameraTo(h.x, h.y, targetZoom, 320);
    this._emitBreadcrumbs();
  }

  stepZoomOut() {
    if (this.focusedHolonKey) {
      const cur = this.holonCircles.find(c => c.key === this.focusedHolonKey);
      if (cur && cur.parentKey && cur.parentKey !== 'root') {
        this.zoomToHolon(cur.parentKey);
        return;
      }
    }
    this.zoomToMacro();
  }

  selectAndFlyTo(id) {
    this.selectedId = id;
    const holon = this.holonCircles.find(h => h.neuronId === id);
    if (holon && !this.is3D) {
      this.zoomToHolon(holon.key);
      return;
    }
    const pos = this.packedPos.get(id);
    const n = this.nodeMap.get(id);
    if (!n) return;
    if (!this.is3D && pos) {
      const parentHolon = this.holonCircles.find(h => h.neuronId === n.parent_id);
      if (parentHolon) this.focusedHolonKey = parentHolon.key;
      const z = n.depth === 0 ? 2.4 : (n.depth === 1 ? 5.2 : 8.5);
      this.animateCameraTo(pos.x, pos.y, z, 320);
      this._emitBreadcrumbs();
    }
  }

  _emitBreadcrumbs() {
    if (!this.onBreadcrumbChange) return;
    const crumbs = [{ label: 'Macro', key: 'macro' }];
    if (this.focusedHolonKey) {
      const chain = [];
      let cur = this.holonCircles.find(c => c.key === this.focusedHolonKey);
      while (cur) {
        chain.unshift({ label: cur.label, key: cur.key });
        cur = cur.parentKey ? this.holonCircles.find(c => c.key === cur.parentKey) : null;
      }
      crumbs.push(...chain);
    }
    this.onBreadcrumbChange(crumbs);
  }

  // ---------------------------------------------------------------------------
  // Deterministic Circle-Packing Layout (d3-hierarchy pack)
  // ---------------------------------------------------------------------------
  _computePackedLayout() {
    this.packedPos.clear();
    this.holonCircles = [];

    const roots = this.nodes.filter(n => n.role === 'RootSensory');
    const terms = this.nodes.filter(n => n.role === 'TerminalReadout');
    const childrenByParent = new Map();
    for (const n of this.nodes) {
      if (n.parent_id >= 0) {
        if (!childrenByParent.has(n.parent_id)) childrenByParent.set(n.parent_id, []);
        childrenByParent.get(n.parent_id).push(n);
      }
    }

    const buildSubtree = (neuron, parentKey, level) => {
      const kids = childrenByParent.get(neuron.id) || [];
      const shortLbl = neuron.label.includes('.')
        ? neuron.label.split('.').slice(1).join('.').replaceAll('_', ' ')
        : neuron.label;
      if (kids.length === 0) {
        return {
          key: `leaf:${neuron.id}`,
          neuronId: neuron.id,
          label: shortLbl,
          level,
          parentKey,
          isHolon: false,
          value: 1.0
        };
      }
      const hKey = `holon:${neuron.id}`;
      return {
        key: hKey,
        neuronId: neuron.id,
        label: shortLbl,
        level,
        parentKey,
        isHolon: true,
        children: [
          // Dedicated soma slot so composite parent dot never overlaps child circles
          {
            key: `soma:${neuron.id}`,
            neuronId: neuron.id,
            isSomaProxy: true,
            value: 0.9
          },
          ...kids.map(ch => buildSubtree(ch, hKey, level + 1))
        ]
      };
    };

    // Group top-level CorticalComposite neurons into Region Holons (Prefrontal, Amygdala, etc.)
    const regionOrder = ['Prefrontal', 'Amygdala', 'Hippocampus', 'Association'];
    const regionMap = new Map();
    for (const n of this.nodes) {
      if (n.depth === 0 && n.role === 'CorticalComposite') {
        const reg = n.label.split('.')[0] || 'Cortex';
        if (!regionMap.has(reg)) regionMap.set(reg, []);
        regionMap.get(reg).push(n);
      }
    }

    const sortedRegions = Array.from(regionMap.keys()).sort((a, b) => {
      const ia = regionOrder.indexOf(a);
      const ib = regionOrder.indexOf(b);
      return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
    });

    const treeData = {
      key: 'root',
      isRoot: true,
      children: sortedRegions.map(regName => {
        const regKey = `region:${regName}`;
        const cols = regionMap.get(regName) || [];
        return {
          key: regKey,
          label: regName,
          level: 1,
          parentKey: 'root',
          isHolon: true,
          neuronId: null,
          children: cols.map(col => buildSubtree(col, regKey, 2))
        };
      })
    };

    const packDiam = Math.max(250, Math.min(this.width - 300, this.height - 108));
    const rootHier = hierarchy(treeData)
      .sum(d => d.value || 0)
      .sort((a, b) => (b.value || 0) - (a.value || 0));

    const packLayout = pack()
      .size([packDiam, packDiam])
      .padding(d => (d.depth === 0 ? 32 : (d.depth === 1 ? 12 : 6)));

    packLayout(rootHier);

    const offsetX = -packDiam * 0.5;
    const offsetY = -packDiam * 0.5;

    const collectMemberIds = (hNode) => {
      const ids = [];
      hNode.each(desc => {
        if (desc.data.neuronId != null && !ids.includes(desc.data.neuronId)) {
          ids.push(desc.data.neuronId);
        }
      });
      return ids;
    };

    rootHier.each(d => {
      const wx = d.x + offsetX;
      const wy = d.y + offsetY;
      const data = d.data;

      if (data.isHolon) {
        this.holonCircles.push({
          key: data.key,
          label: data.label,
          level: data.level,
          neuronId: data.neuronId ?? null,
          parentKey: data.parentKey,
          x: wx,
          y: wy,
          r: d.r,
          memberIds: collectMemberIds(d)
        });
      }

      if (data.neuronId != null && (data.isSomaProxy || !data.isHolon)) {
        this.packedPos.set(data.neuronId, { x: wx, y: wy });
      }
    });

    // Position RootSensory rail cleanly on the left, TerminalReadout rail cleanly on the right
    const railSpanY = Math.min(packDiam * 0.76, 340);
    const leftX = -packDiam * 0.5 - 76;
    roots.forEach((r, i) => {
      const u = roots.length > 1 ? (i / (roots.length - 1) - 0.5) : 0;
      this.packedPos.set(r.id, { x: leftX, y: u * railSpanY });
    });

    const rightX = packDiam * 0.5 + 76;
    const outSpanY = Math.min(packDiam * 0.52, 220);
    terms.forEach((t, i) => {
      const u = terms.length > 1 ? (i / (terms.length - 1) - 0.5) : 0;
      this.packedPos.set(t.id, { x: rightX, y: u * outSpanY });
    });
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
        if ((delta > 0.048 && act > 0.32) || (act > st.emaAct + 0.075 && sinceLast > 380)) {
          st.flashMs = nowMs;
        }
        st.emaAct = st.emaAct * 0.92 + act * 0.08;
        st.prevAct = act;
      }
    }

    const sig = `${this.nodes.length}:${this.nodes.map(n => `${n.id}.${n.parent_id}`).join(',')}`;
    if (sig !== this.topologyKey) {
      this.topologyKey = sig;
      this._computePackedLayout();
    }

    // Default edge filter so roughly 20% of edges are visible across sensory, cortical & output pathways
    if (this.weightThreshold === null && this.synapses.length > 0) {
      const keepTotal = Math.max(1, Math.round(this.synapses.length * 0.20));
      const allowedSet = new Set();

      const rootEdges = [];
      const termEdges = [];
      const cortEdges = [];

      this.synapses.forEach((s, idx) => {
        const src = this.nodeMap.get(s.source);
        const dst = this.nodeMap.get(s.target);
        const item = { idx, score: Math.abs(s.weight) + Math.abs(s.eligibility || 0) * 0.05 };
        if (src?.role === 'RootSensory') rootEdges.push(item);
        else if (dst?.role === 'TerminalReadout') termEdges.push(item);
        else cortEdges.push(item);
      });

      rootEdges.sort((a, b) => b.score - a.score);
      termEdges.sort((a, b) => b.score - a.score);
      cortEdges.sort((a, b) => b.score - a.score);

      const nRoot = Math.min(rootEdges.length, Math.round(keepTotal * 0.22));
      const nTerm = Math.min(termEdges.length, Math.round(keepTotal * 0.14));
      const nCort = Math.max(0, keepTotal - nRoot - nTerm);

      for (let i = 0; i < nRoot; i++) allowedSet.add(rootEdges[i].idx);
      for (let i = 0; i < nTerm; i++) allowedSet.add(termEdges[i].idx);
      for (let i = 0; i < Math.min(cortEdges.length, nCort); i++) allowedSet.add(cortEdges[i].idx);

      this.edgeCutoffRank = allowedSet;
      const allSorted = this.synapses.map(s => Math.abs(s.weight)).sort((a, b) => b - a);
      const kthWeight = Math.min(3.40, allSorted[Math.min(allSorted.length - 1, keepTotal - 1)]);
      this.weightThreshold = kthWeight;
      if (this.onAutoThreshold) {
        this.onAutoThreshold(kthWeight, 3.5);
      }
    }
  }

  _worldToScreen(wx, wy) {
    return {
      sx: this.width * 0.5 + (wx - this.cam.x) * this.cam.zoom,
      sy: this.height * 0.5 + (wy - this.cam.y) * this.cam.zoom
    };
  }

  _projectNode(n) {
    if (this.is3D) {
      const cx = this.width * 0.5;
      const cy = this.height * 0.5;
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
      n._sx = cx + x1 * 1.45 * scale;
      n._sy = cy - y2 * 1.45 * scale;
      return;
    }

    const p = this.packedPos.get(n.id) || { x: 0, y: 0 };
    const sc = this._worldToScreen(p.x, p.y);
    n._sx = sc.sx;
    n._sy = sc.sy;
  }

  _hitTestRailNode(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const mx = clientX - rect.left;
    const my = clientY - rect.top;
    for (const n of this.nodes) {
      if (n.role !== 'RootSensory' && n.role !== 'TerminalReadout') continue;
      if (Math.hypot(mx - n._sx, my - n._sy) < 10) return n;
    }
    return null;
  }

  _hitTestNode(clientX, clientY) {
    const rect = this.canvas.getBoundingClientRect();
    const mx = clientX - rect.left;
    const my = clientY - rect.top;

    let best = null;
    let bestDist = 9.5;
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

  _hitTestHolon(clientX, clientY, maxLevel = 99) {
    const rect = this.canvas.getBoundingClientRect();
    const mx = clientX - rect.left;
    const my = clientY - rect.top;

    // Prefer deepest matching circle within maxLevel
    let best = null;
    for (const h of this.holonCircles) {
      if (h.level > maxLevel) continue;
      const sc = this._worldToScreen(h.x, h.y);
      const sr = h.r * this.cam.zoom;
      if (Math.hypot(mx - sc.sx, my - sc.sy) <= sr) {
        if (!best || h.level > best.level) best = h;
      }
    }
    return best;
  }

  _computeDotRadius(n) {
    const childBoost = Math.min(3.2, (n.child_count || 0) * 0.8);
    const actBoost = (n.activation || 0) * 1.4;
    return Math.max(3.0, Math.min(8.0, 3.0 + childBoost + actBoost));
  }

  renderFrame() {
    const nowMs = performance.now();
    const theme = getTheme();
    const ctx = this.ctx;

    if (this.camAnim) {
      const t = Math.min(1, (nowMs - this.camAnim.startMs) / this.camAnim.durationMs);
      const ease = 1 - Math.pow(1 - t, 3);
      this.cam.x = this.camAnim.fromX + (this.camAnim.toX - this.camAnim.fromX) * ease;
      this.cam.y = this.camAnim.fromY + (this.camAnim.toY - this.camAnim.fromY) * ease;
      this.cam.zoom = this.camAnim.fromZ + (this.camAnim.toZ - this.camAnim.fromZ) * ease;
      if (t >= 1) this.camAnim = null;
      this._emitZoomTier();
    }

    const tier = this.getZoomTier();

    ctx.save();
    ctx.scale(this.dpr, this.dpr);
    ctx.fillStyle = theme.bg;
    ctx.fillRect(0, 0, this.width, this.height);

    for (const n of this.nodes) {
      this._projectNode(n);
    }

    const focusId = this.hoveredId !== null ? this.hoveredId : this.selectedId;
    const focusNeighbors = new Set();
    if (focusId !== null) {
      focusNeighbors.add(focusId);
      for (const s of this.synapses) {
        if (s.source === focusId) focusNeighbors.add(s.target);
        if (s.target === focusId) focusNeighbors.add(s.source);
      }
    }

    // Build lookup of Region Holon screen circles for clean boundary bundling
    const regionScreenMap = new Map();
    if (!this.is3D) {
      for (const h of this.holonCircles) {
        const sc = this._worldToScreen(h.x, h.y);
        h._sx = sc.sx;
        h._sy = sc.sy;
        h._sr = h.r * this.cam.zoom;
        if (h.level === 1) {
          regionScreenMap.set(h.label, h);
        }
      }

      // 1. Draw Nested Holon Containment Circles & Top-Edge 11px Labels
      ctx.textAlign = 'center';
      ctx.textBaseline = 'bottom';
      ctx.font = `400 11px ${theme.fontSans}`;

      for (const h of this.holonCircles) {
        if (h._sr < 6) continue;

        // Compute aggregate firing flash across member neurons for Macro view
        let recentFlashes = 0;
        for (const mid of h.memberIds) {
          const fst = this.flashState.get(mid);
          if (fst && (nowMs - fst.flashMs) < 300) recentFlashes++;
        }
        const flashRatio = h.memberIds.length > 0 ? Math.min(1, recentFlashes / Math.max(2, h.memberIds.length * 0.35)) : 0;

        // In Macro view, show subtle aggregate activity ring on top-level holons when firing
        if (tier === 'macro' && h.level === 1 && flashRatio > 0.05) {
          const strokeRgb = lerpRgb(theme.rgb.line, theme.rgb.signal, flashRatio * 0.85);
          ctx.strokeStyle = rgba(strokeRgb, 0.9);
        } else {
          ctx.strokeStyle = focusId !== null ? rgba(theme.rgb.line, 0.45) : theme.line;
        }

        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.arc(h._sx, h._sy, h._sr, 0, Math.PI * 2);
        ctx.stroke();

        // Holon label at top edge of circle (11px, --text-dim)
        const showLabel =
          (h.level === 1 && h._sr > 24) ||
          (h.level === 2 && tier !== 'macro' && h._sr > 26) ||
          (h.level >= 3 && tier === 'micro' && h._sr > 22);

        if (showLabel) {
          ctx.fillStyle = theme.textDim;
          ctx.fillText(h.label, h._sx, h._sy - h._sr - 4);
        }
      }
    }

    // 2. Draw Filtered & Bundled Synaptic Edges (~20% default visibility)
    const thresh = this.weightThreshold ?? 0.45;
    const drawnRootBundles = new Set();
    ctx.lineWidth = 1;

    for (let idx = 0; idx < this.synapses.length; idx++) {
      const s = this.synapses[idx];
      const absW = Math.abs(s.weight);
      const isFocusedEdge = focusId !== null && (s.source === focusId || s.target === focusId);

      if (!isFocusedEdge) {
        if (this.edgeCutoffRank !== null) {
          if (!this.edgeCutoffRank.has(idx)) continue;
        } else if (absW < thresh) {
          continue;
        }
      }

      const src = this.nodeMap.get(s.source);
      const dst = this.nodeMap.get(s.target);
      if (!src || !dst) continue;

      // In Macro 2D view, hide fine internal micro-edges unless hovered/selected
      if (!this.is3D && tier === 'macro' && !isFocusedEdge && src.depth > 0 && dst.depth > 0) {
        continue;
      }

      const srcFlash = this.flashState.get(src.id);
      const flashAge = srcFlash ? (nowMs - srcFlash.flashMs) : 9999;
      const isSignalEdge = flashAge >= 0 && flashAge < 260 && (src.activation || 0) > 0.3;

      let strokeStyle;
      if (focusId !== null && !isFocusedEdge) {
        strokeStyle = rgba(theme.rgb.line, 0.08);
      } else if (isSignalEdge) {
        const decay = Math.max(0.25, 1 - flashAge / 260);
        strokeStyle = rgba(theme.rgb.signal, 0.85 * decay);
      } else {
        const normW = Math.min(1, absW / 3.5);
        const rgb = lerpRgb(theme.rgb.line, theme.rgb.textDim, normW);
        const alpha = isFocusedEdge ? 0.9 : (0.25 + 0.5 * normW);
        strokeStyle = rgba(rgb, alpha);
      }

      // Bundle edges from RootSensory neurons cleanly to the Region Holon boundary
      if (!this.is3D && src.role === 'RootSensory' && dst.role === 'CorticalComposite' && focusId === null) {
        const regName = dst.label.split('.')[0];
        const bKey = `${src.id}:${regName}`;
        if (drawnRootBundles.has(bKey)) continue;
        drawnRootBundles.add(bKey);

        const regHolon = regionScreenMap.get(regName);
        if (regHolon) {
          // Terminate cleanly on the left perimeter point of the holon circle
          const angle = Math.atan2(src._sy - regHolon._sy, src._sx - regHolon._sx);
          const bx = regHolon._sx + Math.cos(angle) * regHolon._sr;
          const by = regHolon._sy + Math.sin(angle) * regHolon._sr;
          const ctrlX = src._sx + (bx - src._sx) * 0.52;

          ctx.strokeStyle = strokeStyle;
          ctx.beginPath();
          ctx.moveTo(src._sx, src._sy);
          ctx.quadraticCurveTo(ctrlX, src._sy, bx, by);
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

    // 3. Draw Neuron Dots (3-8px solid dots, faded in Macro for internal nodes, crisp in Cluster/Micro)
    for (const n of this.nodes) {
      const isRail = n.role === 'RootSensory' || n.role === 'TerminalReadout';
      let baseAlpha = 1.0;

      if (!this.is3D && tier === 'macro' && !isRail) {
        // Macro fades individual internal neurons so holon containment circles lead visually
        baseAlpha = n.depth === 0 ? 0.38 : 0.18;
      } else if (!this.is3D && tier === 'cluster' && n.depth >= 2) {
        baseAlpha = 0.45;
      }

      if (focusId !== null) {
        baseAlpha = focusNeighbors.has(n.id) ? 1.0 : 0.14;
      }

      const r = this._computeDotRadius(n);
      let rgb;

      if (this.lens === 'age') {
        const mat = Math.max(0, Math.min(1, n.maturity ?? 0));
        rgb = lerpRgb(theme.rgb.young, theme.rgb.old, mat);
      } else {
        const st = this.flashState.get(n.id);
        const elapsed = st ? (nowMs - st.flashMs) : 9999;
        if (elapsed >= 0 && elapsed < 320) {
          const flashT = Math.exp(-elapsed / 120);
          rgb = lerpRgb(theme.rgb.textDim, theme.rgb.signal, flashT);
          baseAlpha = Math.max(baseAlpha, 0.92 * flashT);
        } else {
          rgb = theme.rgb.textDim;
        }
      }

      ctx.fillStyle = rgba(rgb, baseAlpha);
      ctx.beginPath();
      ctx.arc(n._sx, n._sy, r, 0, Math.PI * 2);
      ctx.fill();

      // 1px --signal selection / hover ring
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
