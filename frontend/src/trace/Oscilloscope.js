import { getTheme } from '../theme.js';

const MAX_SAMPLES = 600;

export class Oscilloscope {
  constructor(canvas, legendEl, statusEl) {
    this.canvas = canvas;
    this.legendEl = legendEl;
    this.statusEl = statusEl;
    this.ctx = canvas.getContext('2d');

    this.buffer = [];
    this.lastTick = -1;
    this.paused = false;
    this.hasSelectedNeuron = false;

    this._setupCanvas();
    this.canvas.addEventListener('click', () => {
      this.paused = !this.paused;
      if (this.statusEl) {
        this.statusEl.textContent = this.paused ? 'paused' : '';
      }
      this.render();
    });
  }

  _setupCanvas() {
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    this.cssW = 260;
    this.cssH = 120;
    this.dpr = dpr;
    this.canvas.width = Math.round(this.cssW * dpr);
    this.canvas.height = Math.round(this.cssH * dpr);
  }

  ingest(state, selectedNeuronVoltage = null) {
    if (this.paused || !state || !state.metadata) return;

    this.hasSelectedNeuron = selectedNeuronVoltage !== null && Number.isFinite(selectedNeuronVoltage);

    // Seed initial history from backend ring buffer on first frame
    if (this.buffer.length === 0 && Array.isArray(state.telemetry) && state.telemetry.length > 0) {
      for (const s of state.telemetry) {
        if (s.t > this.lastTick) {
          this.buffer.push({
            tick: s.t,
            act: s.a ?? 0,
            da: s.d ?? 0,
            fear: s.f ?? 0,
            vm: this.hasSelectedNeuron ? selectedNeuronVoltage : null
          });
          this.lastTick = s.t;
        }
      }
    } else if (Array.isArray(state.telemetry)) {
      // Append any unseen intermediate telemetry ticks
      for (const s of state.telemetry) {
        if (s.t > this.lastTick) {
          this.buffer.push({
            tick: s.t,
            act: s.a ?? 0,
            da: s.d ?? 0,
            fear: s.f ?? 0,
            vm: this.hasSelectedNeuron ? selectedNeuronVoltage : null
          });
          this.lastTick = s.t;
        }
      }
    }

    const curTick = state.metadata.tick;
    if (curTick > this.lastTick) {
      this.buffer.push({
        tick: curTick,
        act: state.metadata.mean_activation ?? 0,
        da: state.endocrine?.dopamine ?? 0,
        fear: state.endocrine?.fear ?? 0,
        vm: this.hasSelectedNeuron ? selectedNeuronVoltage : null
      });
      this.lastTick = curTick;
    } else if (this.buffer.length > 0 && this.hasSelectedNeuron) {
      this.buffer[this.buffer.length - 1].vm = selectedNeuronVoltage;
    }

    while (this.buffer.length > MAX_SAMPLES) {
      this.buffer.shift();
    }

    this.render();
  }

  _computeAutoscaleSeries(key, bandTop, bandBottom) {
    const vals = [];
    for (const item of this.buffer) {
      const v = item[key];
      if (v !== null && v !== undefined && Number.isFinite(v)) vals.push(v);
    }
    if (vals.length === 0) return null;

    let minV = Infinity;
    let maxV = -Infinity;
    for (const v of vals) {
      if (v < minV) minV = v;
      if (v > maxV) maxV = v;
    }

    const mid = (minV + maxV) * 0.5;
    // Minimum span avoids amplifying floating-point rounding jitter while keeping small real waves crisp
    const span = Math.max(0.012, (maxV - minV) * 1.18);
    const lo = mid - span * 0.5;
    const hi = mid + span * 0.5;

    return {
      lo,
      hi,
      latest: vals[vals.length - 1],
      mapY: (v) => {
        const u = (v - lo) / (hi - lo);
        return bandBottom - Math.max(0, Math.min(1, u)) * (bandBottom - bandTop);
      }
    };
  }

  render() {
    const theme = getTheme();
    const ctx = this.ctx;
    const w = this.cssW;
    const h = this.cssH;
    const readoutGutter = 38;
    const plotW = w - readoutGutter;

    ctx.save();
    ctx.scale(this.dpr, this.dpr);

    // Background
    ctx.fillStyle = theme.bg;
    ctx.fillRect(0, 0, w, h);

    // Graticule: 4 horizontal divisions and 10 vertical divisions in --line
    ctx.strokeStyle = theme.line;
    ctx.lineWidth = 1;
    ctx.beginPath();
    for (let r = 1; r < 4; r++) {
      const y = Math.round((h * r) / 4) + 0.5;
      ctx.moveTo(0, y);
      ctx.lineTo(plotW, y);
    }
    for (let c = 1; c <= 10; c++) {
      const x = Math.round((plotW * c) / 10) + 0.5;
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
    }
    ctx.stroke();

    if (this.buffer.length < 2) {
      ctx.restore();
      return;
    }

    const seriesDefs = [
      { key: 'fear', color: theme.danger, top: 10, bottom: h - 10 },
      { key: 'da',   color: theme.young,  top: 10, bottom: h - 10 },
      { key: 'act',  color: theme.signal, top: 8,  bottom: h - 8 }
    ];
    if (this.hasSelectedNeuron) {
      seriesDefs.push({ key: 'vm', color: theme.text, top: 10, bottom: h - 10 });
    }

    // Window smoothly scrolls across up to MAX_SAMPLES (600 ticks); before 600 ticks, uses visible window >= 120 slots
    const windowSlots = Math.min(MAX_SAMPLES, Math.max(120, this.buffer.length));
    const n = this.buffer.length;
    const endLabels = [];

    for (const def of seriesDefs) {
      const scale = this._computeAutoscaleSeries(def.key, def.top, def.bottom);
      if (!scale) continue;

      ctx.strokeStyle = def.color;
      ctx.lineWidth = 1;
      ctx.beginPath();
      let started = false;
      let lastY = h * 0.5;

      for (let i = 0; i < n; i++) {
        const val = this.buffer[i][def.key];
        if (val === null || val === undefined) continue;
        // Right-aligned strip-chart x so new ticks continuously enter at plotW and scroll left!
        const slotFromRight = (n - 1) - i;
        const x = plotW - (slotFromRight / (windowSlots - 1)) * plotW;
        if (x < 0) continue;
        const y = scale.mapY(val);
        lastY = y;
        if (!started) {
          ctx.moveTo(x, y);
          started = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();

      endLabels.push({
        key: def.key,
        color: def.color,
        y: lastY,
        text: scale.latest.toFixed(2)
      });
    }

    // Anti-collide right-edge mono live readouts
    endLabels.sort((a, b) => a.y - b.y);
    const minGap = 11;
    for (let i = 1; i < endLabels.length; i++) {
      if (endLabels[i].y - endLabels[i - 1].y < minGap) {
        endLabels[i].y = endLabels[i - 1].y + minGap;
      }
    }
    for (let i = endLabels.length - 1; i >= 0; i--) {
      if (endLabels[i].y > h - 6) endLabels[i].y = h - 6;
      if (i > 0 && endLabels[i].y - endLabels[i - 1].y < minGap) {
        endLabels[i - 1].y = Math.max(9, endLabels[i].y - minGap);
      }
    }

    ctx.font = `400 10px ${theme.fontMono}`;
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (const lbl of endLabels) {
      ctx.fillStyle = lbl.color;
      ctx.fillText(lbl.text, w - 4, lbl.y);
    }

    ctx.restore();

    // Update inline mono legend below trace
    if (this.legendEl && this.buffer.length > 0) {
      const latest = this.buffer[this.buffer.length - 1];
      let text = `act ${latest.act.toFixed(2)}  da ${latest.da.toFixed(2)}  fear ${latest.fear.toFixed(2)}`;
      if (this.hasSelectedNeuron && latest.vm !== null) {
        text += `  v ${latest.vm.toFixed(2)}`;
      }
      this.legendEl.textContent = text;
    }
  }
}
