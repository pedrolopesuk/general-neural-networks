# MetaBrain UI Redesign — Audit & Implementation Notes

## 1. Current Frontend Stack & Architecture
- **Build & Dev Server**: Vite `^5.4.0` (`frontend/vite.config.js`), proxying `/api` requests to the C++20 simulation daemon at `http://127.0.0.1:8085` without modifying backend simulation logic.
- **Graph Rendering Library**: Currently `three` (`^0.165.0`) inside `frontend/src/scene/BrainViewport.js` using `EffectComposer`, `UnrealBloomPass`, additive `THREE.Sprite` glow halos, `ShaderMaterial` Fresnel spheres, and `Points` starfield dust.
  - **Redesign Plan**: Replace the bloom-heavy 3D hairball renderer in `BrainViewport.js` with a precision HiDPI 2D Canvas instrument renderer (using `d3-hierarchy` `pack` for deterministic nested holon circle packing as the default 2D view, plus an optional clean 3D force projection toggle using the C++ 3D coordinates without glow or bloom).
- **Where Styles Live**:
  - `frontend/src/style.css`: Contains all layout, typography, panel, slider, button, oscilloscope, retina pad, and inspector rules (currently uses glassmorphism `backdrop-filter`, rounded `16px` translucent cards, gradients, and neon cyan/purple/emerald/crimson variables).
  - Inline style attributes inside `frontend/index.html` and dynamic template strings inside `frontend/src/main.js` (currently contain hard-coded hex colors like `#ff85a1`, `#00ff95`, `#7ce8ff`, `#d8b4fe`, `#00e5ff`, `#a855f7`).
  - Legacy standalone file `visualizer/index.html`: Will also be aligned or stripped of legacy neon hex values so repository-wide palette checks stay clean.

## 2. Components & Files to Touch
1. **`frontend/src/tokens.css` & `frontend/src/theme.js` (New — Single Source of Truth)**:
   - Defines exact CSS custom properties (`--bg`, `--surface`, `--line`, `--text-dim`, `--text`, `--signal`, `--young`, `--old`, `--danger`), font stacks (`--font-sans`, `--font-mono`), font sizes, and maximum `2px` border radius.
   - `theme.js` reads computed CSS custom properties from `:root` for Canvas 2D drawing so zero hex literals exist outside `tokens.css`.
2. **`frontend/index.html`**:
   - Load `Inter` and `JetBrains Mono` (with clean system fallbacks) and import `tokens.css` before `style.css`.
   - Restructure DOM into:
     - `32px` single-line `#header-bar` (`MetaBrain`, mono stats row including step time, right-side text controls: `Pause · 18 Hz`, `3D`, `Save`, `Benchmark`, `Reset`).
     - Fixed `260px` `#left-rail` with hairline `--line` dividers: `State`, `Trace` (`260 × 120` canvas + inline legend), `Input` (`Text` / `Retina 16×16` / `Manual` tabs, stimulus presets `Stimulus A–D`), and `Structure` (`Grow automatically`, `Wire co-active`, and `Edge filter` weight threshold slider).
     - Canvas overlays: top-left `Lens` segmented control (`Activity` / `Age` + legend ramp), bottom-right `Output` compact terminal readout table + `Reward` / `Punish` buttons, bottom-center `/` breadcrumb + `Macro / Cluster / Micro` segmented switch, and floating anchored `#inspector-popover` (hidden until neuron selection; closes on `Esc` or empty canvas click).
   - Remove all emoji, uppercase headers, slider subtitles (moved to `title` attributes), and implementation-leaking copy.
3. **`frontend/src/style.css`**:
   - Strip all `backdrop-filter`, `box-shadow` glows, gradients, and pill radii.
   - Apply flat `--surface` (`#131416`), `1px solid var(--line)` (`#26282B`), max `2px` radius, sentence-case `11px` `--text-dim` section labels, `12–13px` sans labels, and `tabular-nums` monospace numbers everywhere.
4. **`frontend/src/scene/BrainViewport.js`**:
   - Implement HiDPI Canvas renderer with deterministic nested holon circle packing (`Region -> Cortical Holon -> Composite Sub-Holons -> Leaf Neurons`), `11px` top-edge holon labels (`Prefrontal`, `Amygdala`, `Hippocampus`, `Association`), `3–8px` solid `--text-dim` neuron dots, `300ms` `--signal` (`#FFB000`) firing flash decay, `1px --signal` selection ring, top-~20% `|weight|` edge threshold filtering, bundled root-to-holon curves, hover/selection neighbor isolation, `250–400ms` ease-out zoom transitions across `Macro / Cluster / Micro`, and optional `3D` projection toggle.
5. **`frontend/src/trace/Oscilloscope.js` (Extracted from `main.js`)**:
   - `600-tick` client-side ring buffer sampling incoming C++ state (`act`, `da`, `fear`, plus selected neuron `vm`), `260 × 120px` canvas with `4 × 10` `--line` divisions, per-series autoscaling, right-edge mono live readouts, inline mono legend, and click-to-pause/resume.
6. **`frontend/src/main.js`**:
   - Wire all existing backend actions (`api/backendClient.js` untouched in behavior) to the new layout, copy table, anchored inspector popover, `Esc` key handler, edge weight threshold slider, and auto-grow toggle.

## 3. Invariants Preserved
- Zero changes to C++ simulation math, learning rules, endocrine equations, data structures, JSON/GraphML serialization formats, or benchmark execution (`include/`, `src/`, `tests/` untouched).
- Every existing interactive capability (`Pause/Resume`, `3D`, `Save`, `Benchmark`, `Reset`, `Activity/Age` lens, 4 `State` sliders, `Text` / `Retina 16×16` / `Manual` input pulse, terminal target training, `Reward` / `Punish`, `Add 3 children`, `Grow automatically`, `Wire co-active`) retains full functionality.
