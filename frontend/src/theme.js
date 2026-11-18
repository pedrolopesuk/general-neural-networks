// Reads design tokens directly from CSS custom properties in tokens.css.
// No hard-coded hex values live in UI or rendering components.

function parseHexToRgb(hex) {
  const clean = hex.trim().replace('#', '');
  if (clean.length !== 6) return [128, 128, 128];
  return [
    parseInt(clean.slice(0, 2), 16),
    parseInt(clean.slice(2, 4), 16),
    parseInt(clean.slice(4, 6), 16)
  ];
}

let cachedTheme = null;

export function getTheme() {
  if (cachedTheme) return cachedTheme;
  const cs = getComputedStyle(document.documentElement);
  const read = (name) => cs.getPropertyValue(name).trim();

  const bg = read('--bg');
  const surface = read('--surface');
  const line = read('--line');
  const textDim = read('--text-dim');
  const text = read('--text');
  const signal = read('--signal');
  const young = read('--young');
  const old = read('--old');
  const danger = read('--danger');

  cachedTheme = {
    bg,
    surface,
    line,
    textDim,
    text,
    signal,
    young,
    old,
    danger,
    fontSans: read('--font-sans'),
    fontMono: read('--font-mono'),
    rgb: {
      bg: parseHexToRgb(bg),
      surface: parseHexToRgb(surface),
      line: parseHexToRgb(line),
      textDim: parseHexToRgb(textDim),
      text: parseHexToRgb(text),
      signal: parseHexToRgb(signal),
      young: parseHexToRgb(young),
      old: parseHexToRgb(old),
      danger: parseHexToRgb(danger)
    }
  };
  return cachedTheme;
}

export function rgba(rgbTuple, alpha = 1) {
  const a = Math.max(0, Math.min(1, alpha));
  return `rgba(${rgbTuple[0]}, ${rgbTuple[1]}, ${rgbTuple[2]}, ${a.toFixed(3)})`;
}

export function lerpRgb(a, b, t) {
  const u = Math.max(0, Math.min(1, t));
  return [
    Math.round(a[0] + (b[0] - a[0]) * u),
    Math.round(a[1] + (b[1] - a[1]) * u),
    Math.round(a[2] + (b[2] - a[2]) * u)
  ];
}
