/* VP37 measurement viewer: renders the static pages from embedded JSON and
 * VP37.put() data files. No network access, no external libraries. */
(function () {
  'use strict';

  const VP37 = (window.VP37 = window.VP37 || {});
  VP37.base = VP37.base || '';
  const parts = {};
  const waiting = {};
  const loading = {};

  // Data files call VP37.put(slug, part, value).
  VP37.put = function (slug, part, value) {
    parts[slug + '/' + part] = value;
    const key = slug + '/' + part;
    (waiting[key] || []).forEach((resolve) => resolve(value));
    delete waiting[key];
  };

  // Loads one data file with a <script> element (file:// has no fetch).
  function need(slug, part) {
    const key = slug + '/' + part;
    if (key in parts) return Promise.resolve(parts[key]);
    return new Promise((resolve, reject) => {
      (waiting[key] = waiting[key] || []).push(resolve);
      if (loading[key]) return;
      loading[key] = true;
      const script = document.createElement('script');
      script.src = VP37.base + (slug ? 'data/' + slug + '/' + part + '.js' : 'data/' + part + '.js');
      script.onerror = () => reject(new Error('brak pliku danych ' + script.src));
      document.head.appendChild(script);
    });
  }

  // ---------------------------------------------------------------- helpers
  function el(tag, attrs, ...children) {
    const node = document.createElement(tag);
    for (const [key, value] of Object.entries(attrs || {})) {
      if (value === undefined || value === null || value === false) continue;
      if (key === 'class') node.className = value;
      else if (key === 'html') node.innerHTML = value;
      else if (key.startsWith('on')) node.addEventListener(key.slice(2), value);
      else node.setAttribute(key, value === true ? '' : value);
    }
    for (const child of children.flat()) {
      if (child === undefined || child === null || child === false) continue;
      node.appendChild(typeof child === 'string' || typeof child === 'number' ? document.createTextNode(String(child)) : child);
    }
    return node;
  }
  // Element.append() would turn null into the text "null".
  function add(node, ...children) {
    for (const child of children.flat()) {
      if (child === undefined || child === null || child === false) continue;
      node.appendChild(typeof child === 'string' || typeof child === 'number' ? document.createTextNode(String(child)) : child);
    }
    return node;
  }
  const app = () => document.getElementById('app');
  function fmt(value, digits) {
    if (value === null || value === undefined || !isFinite(value)) return '-';
    const d = digits === undefined ? (Math.abs(value) >= 100 ? 0 : Math.abs(value) >= 10 ? 1 : 2) : digits;
    return value.toLocaleString('pl-PL', { maximumFractionDigits: d, minimumFractionDigits: 0 });
  }
  function pct(value) {
    if (value === null || value === undefined || !isFinite(value)) return '-';
    return (value > 0 ? '+' : '') + fmt(value, Math.abs(value) < 10 ? 1 : 0) + ' %';
  }
  function css(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim() || '#888';
  }
  function median(values) {
    const v = values.filter((x) => x !== null && x !== undefined && isFinite(x)).sort((a, b) => a - b);
    if (!v.length) return null;
    const m = Math.floor(v.length / 2);
    return v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2;
  }
  function seconds(t) {
    if (!isFinite(t)) return '-';
    const m = Math.floor(t / 60);
    return m ? m + ' min ' + fmt(t - m * 60, 0) + ' s' : fmt(t, 1) + ' s';
  }
  function runHref(slug) { return VP37.base + 'run/' + slug + '.html'; }
  function iterationHref(slug) { return VP37.base + 'it/' + slug + '.html'; }
  function compareHref(a, b, extra) {
    const q = new URLSearchParams(Object.assign({ a: a || '', b: b || '' }, extra || {}));
    return VP37.base + 'compare.html#' + q.toString();
  }
  function lastPart(path) { return path.split('/').pop(); }
  function short(sha) { return sha ? sha.slice(0, 12) : '-'; }
  const tip = el('div', { class: 'tip' });
  document.addEventListener('DOMContentLoaded', () => document.body.appendChild(tip));
  function showTip(event, text) {
    if (!tip.parentNode) document.body.appendChild(tip);
    tip.textContent = text;
    tip.style.display = 'block';
    const x = Math.min(event.clientX + 14, window.innerWidth - tip.offsetWidth - 8);
    tip.style.left = Math.max(4, x) + 'px';
    tip.style.top = event.clientY + 14 + 'px';
  }
  function hideTip() { tip.style.display = 'none'; }

  // Metrics are labelled like the gate comparisons; families for filters.
  function family(label) {
    if (label.startsWith('cyclic')) return 'cyclic';
    if (label.startsWith('topzero')) return 'topzero';
    if (label.startsWith('top ')) return 'top';
    if (label.startsWith('random')) return 'random';
    if (/^S\d+ /.test(label)) return 'manual';
    if (label.startsWith('control_period_us')) return 'okres pętli';
    if (label.startsWith('feedback_age_us')) return 'wiek feedbacku';
    return 'inne';
  }
  const CORE = [
    'cyclic 2ms p95_abs_err_hz', 'cyclic 4ms p95_abs_err_hz', 'cyclic 6ms p95_abs_err_hz', 'cyclic 12ms p95_abs_err_hz',
    'random p95_abs_err_hz', 'top 95% early_ringing_error_rms_hz', 'top 95% late_ringing_error_rms_hz',
    'topzero 95% early_ringing_error_rms_hz', 'topzero 95% late_ringing_error_rms_hz',
    'S20 p95_abs_error_hz', 'S50 p95_abs_error_hz', 'S75 p95_abs_error_hz', 'S90 p95_abs_error_hz',
    'control_period_us p95', 'control_period_us max', 'feedback_age_us p95'];
  const SMALL_CHANGE = 2; // percent; only a lighter shade, never a different verdict

  // ------------------------------------------------------------------ plot
  function niceTicks(lo, hi, count) {
    if (!(hi > lo)) return [lo];
    const raw = (hi - lo) / count;
    const mag = Math.pow(10, Math.floor(Math.log10(raw)));
    const step = [1, 2, 5, 10].map((s) => s * mag).find((s) => s >= raw) || raw;
    const out = [];
    for (let v = Math.ceil(lo / step) * step; v <= hi + step * 1e-9; v += step) out.push(+v.toPrecision(12));
    return out;
  }

  // Canvas plot with line, band (min/max), point and bar layers, drag zoom
  // and a value tooltip.
  class Plot {
    constructor(parent, opts) {
      this.opts = Object.assign({ height: 200, xFormat: (v) => fmt(v), yLabel: '' }, opts);
      this.layers = [];
      this.marks = [];
      this.root = el('div', { class: 'chart' });
      this.legend = el('div', { class: 'legend' });
      this.canvas = el('canvas');
      add(this.root, this.legend, this.canvas);
      parent.appendChild(this.root);
      this.margin = { l: 58, r: 12, t: 8, b: this.opts.xTicks === false ? 6 : 24 };
      this.drag = null;
      this.canvas.addEventListener('mousedown', (e) => {
        if (!this.opts.onZoom) return;
        this.drag = { from: e.offsetX, to: e.offsetX };
      });
      this.canvas.addEventListener('mousemove', (e) => {
        if (this.drag) {
          this.drag.to = e.offsetX;
          this.draw();
        }
        this.tooltip(e);
      });
      this.canvas.addEventListener('mouseleave', hideTip);
      window.addEventListener('mouseup', () => {
        if (!this.drag) return;
        const { from, to } = this.drag;
        this.drag = null;
        if (Math.abs(to - from) > 6) this.opts.onZoom(this.xOf(Math.min(from, to)), this.xOf(Math.max(from, to)));
        else this.draw();
      });
      this.canvas.addEventListener('dblclick', () => this.opts.onReset && this.opts.onReset());
      window.addEventListener('resize', () => this.draw());
    }
    set(layers, x0, x1) {
      this.layers = layers;
      this.x0 = x0;
      this.x1 = x1;
      this.legend.replaceChildren(...layers.filter((l) => l.name && !l.noLegend).map((l) =>
        el('span', {}, el('i', { style: 'background:' + l.color }), l.name)));
      this.draw();
    }
    px(x) { return this.margin.l + ((x - this.x0) / (this.x1 - this.x0 || 1)) * this.w; }
    py(y) { return this.margin.t + (1 - (y - this.y0) / (this.y1 - this.y0 || 1)) * this.h; }
    xOf(px) { return this.x0 + ((px - this.margin.l) / this.w) * (this.x1 - this.x0); }
    yRange() {
      if (this.opts.y) return this.opts.y;
      let lo = Infinity, hi = -Infinity;
      const take = (x, y) => {
        if (y === null || y === undefined || !isFinite(y) || x < this.x0 || x > this.x1) return;
        if (y < lo) lo = y;
        if (y > hi) hi = y;
      };
      for (const l of this.layers) {
        if (l.kind === 'band') l.x.forEach((x, i) => { take(x, l.lo[i]); take(x, l.hi[i]); });
        else if (l.kind === 'bars') l.y.forEach((y, i) => take(l.x[i], y));
        else l.x.forEach((x, i) => take(x, l.y[i]));
      }
      if (this.opts.yZero) lo = Math.min(lo, 0);
      if (!isFinite(lo)) return [0, 1];
      if (lo === hi) return [lo - 1, hi + 1];
      const pad = (hi - lo) * 0.06;
      return [lo - (this.opts.yZero && lo === 0 ? 0 : pad), hi + pad];
    }
    draw() {
      const width = this.canvas.clientWidth || this.root.clientWidth || 600;
      const height = this.opts.height;
      const dpr = window.devicePixelRatio || 1;
      this.canvas.width = Math.round(width * dpr);
      this.canvas.height = Math.round(height * dpr);
      this.canvas.style.height = height + 'px';
      const g = this.canvas.getContext('2d');
      g.setTransform(dpr, 0, 0, dpr, 0, 0);
      g.clearRect(0, 0, width, height);
      this.w = width - this.margin.l - this.margin.r;
      this.h = height - this.margin.t - this.margin.b;
      [this.y0, this.y1] = this.yRange();
      g.font = '11px system-ui, sans-serif';
      g.fillStyle = css('--muted');
      g.strokeStyle = css('--grid');
      g.lineWidth = 1;
      for (const v of niceTicks(this.y0, this.y1, Math.max(2, Math.floor(this.h / 40)))) {
        const y = Math.round(this.py(v)) + 0.5;
        g.beginPath(); g.moveTo(this.margin.l, y); g.lineTo(width - this.margin.r, y); g.stroke();
        g.textAlign = 'right'; g.textBaseline = 'middle';
        g.fillText((this.opts.yFormat || fmt)(v), this.margin.l - 6, y);
      }
      if (this.opts.xTicks !== false) {
        const ticks = this.opts.xTickValues ? this.opts.xTickValues(this.x0, this.x1) : niceTicks(this.x0, this.x1, Math.max(2, Math.floor(this.w / 90)));
        g.textAlign = 'center'; g.textBaseline = 'top';
        for (const v of ticks) {
          const x = Math.round(this.px(v)) + 0.5;
          if (x < this.margin.l - 1 || x > width - this.margin.r + 1) continue;
          g.beginPath(); g.moveTo(x, this.margin.t); g.lineTo(x, this.margin.t + this.h); g.stroke();
          g.fillText(this.opts.xFormat(v), x, this.margin.t + this.h + 5);
        }
      }
      g.save();
      g.beginPath(); g.rect(this.margin.l, this.margin.t, this.w, this.h); g.clip();
      for (const m of this.marks) {
        g.fillStyle = m.color;
        g.globalAlpha = m.alpha || 0.12;
        const a = this.px(m.from), b = this.px(m.to === undefined ? m.from : m.to);
        g.fillRect(a, this.margin.t, Math.max(1, b - a), this.h);
        g.globalAlpha = 1;
      }
      for (const l of this.layers) this.drawLayer(g, l);
      if (this.drag) {
        g.fillStyle = css('--accent'); g.globalAlpha = 0.15;
        const a = Math.min(this.drag.from, this.drag.to), b = Math.max(this.drag.from, this.drag.to);
        g.fillRect(a, this.margin.t, b - a, this.h); g.globalAlpha = 1;
      }
      g.restore();
      if (this.opts.yLabel) {
        g.save(); g.fillStyle = css('--muted'); g.translate(11, this.margin.t + this.h / 2); g.rotate(-Math.PI / 2);
        g.textAlign = 'center'; g.textBaseline = 'middle'; g.fillText(this.opts.yLabel, 0, 0); g.restore();
      }
    }
    drawLayer(g, l) {
      g.strokeStyle = l.color; g.fillStyle = l.color; g.lineWidth = l.width || 1.3;
      if (l.kind === 'band') {
        g.globalAlpha = l.alpha || 0.45;
        for (let i = 0; i < l.x.length; i++) {
          if (l.lo[i] === null || l.hi[i] === null) continue;
          const x = this.px(l.x[i]);
          const y0 = this.py(l.hi[i]), y1 = this.py(l.lo[i]);
          g.fillRect(x - l.halfWidthPx, y0, Math.max(1, 2 * l.halfWidthPx), Math.max(1, y1 - y0));
        }
        g.globalAlpha = 1;
      } else if (l.kind === 'bars') {
        const half = l.halfWidth;
        g.globalAlpha = 0.8;
        l.x.forEach((x, i) => {
          const a = this.px(x - half), b = this.px(x + half), y = this.py(l.y[i]);
          g.fillRect(a + 0.5, y, Math.max(1, b - a - 1), this.py(Math.max(this.y0, 0)) - y);
        });
        g.globalAlpha = 1;
      } else if (l.kind === 'points') {
        l.x.forEach((x, i) => {
          const y = l.y[i];
          if (y === null || !isFinite(y)) return;
          g.beginPath(); g.arc(this.px(x), this.py(y), l.r || 3.5, 0, 2 * Math.PI);
          g.strokeStyle = (l.colors && l.colors[i]) || l.color; g.fillStyle = g.strokeStyle;
          if (l.hollow && l.hollow[i]) { g.lineWidth = 1.5; g.stroke(); } else g.fill();
        });
      } else {
        g.globalAlpha = l.alpha || 1;
        g.beginPath();
        let pen = false;
        for (let i = 0; i < l.x.length; i++) {
          const y = l.y[i];
          if (y === null || y === undefined || !isFinite(y)) { pen = false; continue; }
          const px = this.px(l.x[i]), py = this.py(y);
          if (pen) g.lineTo(px, py); else g.moveTo(px, py);
          pen = true;
        }
        g.stroke();
        g.globalAlpha = 1;
      }
    }
    tooltip(e) {
      if (!this.layers.length || e.offsetX < this.margin.l) return hideTip();
      const x = this.xOf(e.offsetX);
      const lines = [this.opts.tipX ? this.opts.tipX(x) : this.opts.xFormat(x)];
      for (const l of this.layers) {
        if (!l.name || l.noTip || !l.x.length) continue;
        let lo = 0, hi = l.x.length - 1;
        while (lo < hi) { const mid = (lo + hi) >> 1; if (l.x[mid] < x) lo = mid + 1; else hi = mid; }
        if (lo > 0 && Math.abs(l.x[lo - 1] - x) < Math.abs(l.x[lo] - x)) lo--;
        if (l.tip) lines.push(l.tip(lo));
        else if (l.kind === 'band') lines.push(l.name + ': ' + fmt(l.lo[lo]) + ' ... ' + fmt(l.hi[lo]));
        else lines.push(l.name + ': ' + fmt(l.y[lo]));
      }
      showTip(e, lines.join('\n'));
    }
  }

  // ------------------------------------------------------------- markdown
  function inline(text) {
    return text
      .replace(/`([^`]+)`/g, '<code>$1</code>')
      .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
      .replace(/(^|[\s(])\*([^*\s][^*]*)\*/g, '$1<em>$2</em>')
      .replace(/\[([^\]]+)\]\((https?:[^)\s]+)\)/g, '<a href="$2">$1</a>')
      .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, '<span title="$2">$1</span>');
  }
  function escapeHtml(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }
  // Small Markdown subset for report.md: headings, lists, tables, code.
  function markdown(source) {
    const lines = escapeHtml(source || '').split('\n');
    let html = '', i = 0;
    while (i < lines.length) {
      const line = lines[i];
      if (line.startsWith('```')) {
        let code = '';
        for (i++; i < lines.length && !lines[i].startsWith('```'); i++) code += lines[i] + '\n';
        html += '<pre><code>' + code + '</code></pre>';
        i++;
        continue;
      }
      const h = /^(#{1,4}) (.*)$/.exec(line);
      if (h) { html += '<h' + (h[1].length + 1) + '>' + inline(h[2]) + '</h' + (h[1].length + 1) + '>'; i++; continue; }
      if (line.startsWith('|') && i + 1 < lines.length && /^\|[\s:|-]+\|?\s*$/.test(lines[i + 1])) {
        const cells = (row) => row.replace(/^\||\|\s*$/g, '').split('|').map((c) => inline(c.trim()));
        html += '<div class="scroll"><table><tr>' + cells(line).map((c) => '<th>' + c + '</th>').join('') + '</tr>';
        for (i += 2; i < lines.length && lines[i].startsWith('|'); i++) {
          html += '<tr>' + cells(lines[i]).map((c) => '<td>' + c + '</td>').join('') + '</tr>';
        }
        html += '</table></div>';
        continue;
      }
      if (/^\s*([-*]|\d+\.) /.test(line)) {
        const ordered = /^\s*\d+\./.test(line);
        html += ordered ? '<ol>' : '<ul>';
        while (i < lines.length && /^\s*([-*]|\d+\.) /.test(lines[i])) {
          let item = lines[i].replace(/^\s*([-*]|\d+\.) /, '');
          for (i++; i < lines.length && /^\s{2,}\S/.test(lines[i]) && !/^\s*([-*]|\d+\.) /.test(lines[i]); i++) item += ' ' + lines[i].trim();
          html += '<li>' + inline(item) + '</li>';
        }
        html += ordered ? '</ol>' : '</ul>';
        continue;
      }
      if (!line.trim()) { i++; continue; }
      let para = line;
      for (i++; i < lines.length && lines[i].trim() && !/^(#|```|\||\s*([-*]|\d+\.) )/.test(lines[i]); i++) para += ' ' + lines[i];
      html += '<p>' + inline(para) + '</p>';
    }
    return html;
  }

  // ------------------------------------------------------------ run page
  const TEST_COLORS = {
    cyclic: '#2563b0', random: '#7b3fb5', top: '#d9731a', topzero: '#c62f2f', manual: '#8a8f98',
    pot: '#2b8a4b', kv: '#b58a00', dtc: '#5d6b7a', sequence: 'transparent'
  };
  function testColor(name) {
    if (name in TEST_COLORS) return TEST_COLORS[name];
    let h = 0;
    for (const c of name) h = (h * 31 + c.charCodeAt(0)) % 360;
    return 'hsl(' + h + ',45%,50%)';
  }
  const EXTRA = [
    ['thr', 'zadanie thr', '%'], ['pwm', 'pwm', ''], ['ff', 'ff', ''], ['P', 'P', ''], ['I', 'I', ''], ['D', 'D', ''],
    ['current', 'prąd Ion/I95', 'A'], ['pdt', 'okres pętli pdt', 'µs'], ['age', 'wiek feedbacku age', 'µs'],
    ['hz', 'hz / hzraw', 'Hz'], ['V', 'napięcie V', 'V'], ['ft', 'temperatura ft', '°C']];
  const FULL_SPAN = 120; // seconds; narrower views load the full-rate fragments

  function metricsTable(values, compareTo) {
    const labels = Object.keys(values || {});
    if (!labels.length) return el('p', { class: 'muted' }, 'Plik nie ma metryk w znanych sekcjach.');
    const groups = {};
    labels.forEach((l) => (groups[family(l)] = groups[family(l)] || []).push(l));
    return el('div', { class: 'grid2' }, Object.entries(groups).map(([name, list]) =>
      el('div', {}, el('h3', {}, name), el('table', {}, list.map((l) =>
        el('tr', {}, el('td', { class: 'mono' }, l), el('td', { class: 'num' }, fmt(values[l]))))))));
  }

  function histogramBlock(parent, h, stats) {
    const box = el('div', {});
    parent.appendChild(box);
    const title = h.key === 'pdt' ? 'Okres pętli pdt' : 'Wiek feedbacku age';
    box.appendChild(el('h3', {}, title + ' [' + (h.unit === 'us' ? 'µs' : h.unit) + ']'));
    if (!h.n) {
      box.appendChild(el('p', { class: 'muted' }, 'Brak próbek w oknie.'));
      return;
    }
    const width = (h.hi - h.lo) / h.bins.length;
    const plot = new Plot(box, { height: 170, yZero: true, xFormat: (v) => fmt(v, 0) });
    plot.set([{ kind: 'bars', name: 'próbki', color: css('--accent'), x: h.bins.map((_, i) => h.lo + (i + 0.5) * width),
      y: h.bins, halfWidth: width / 2, tip: (i) => 'próbek: ' + h.bins[i] + '\n' + fmt(h.lo + i * width, 0) + '...' + fmt(h.lo + (i + 1) * width, 0) }], h.lo, h.hi);
    const outside = h.below + h.above;
    box.appendChild(el('p', { class: 'small' },
      'Próbek w oknie: ' + h.n + '. Oś ' + fmt(h.lo, 0) + '...' + fmt(h.hi, 0) + '; poza osią: ',
      el('strong', {}, String(outside)), ' (poniżej ' + h.below + ', powyżej ' + h.above + '). Minimum ' + fmt(h.min, 0) +
      ', maksimum ', el('strong', {}, fmt(h.max, 0)), '.' + (h.missing ? ' Wierszy bez wartości: ' + h.missing + '.' : '')));
    if (stats) {
      box.appendChild(el('p', { class: 'small muted' }, 'Z pliku metryk (' + stats.file + '): mediana ' + fmt(stats.v.median) +
        ', p95 ' + fmt(stats.v.p95) + ', max ' + fmt(stats.v.max) + '. Te liczby policzyła bramka; strona ich nie liczy.'));
    }
  }

  async function runPage(p) {
    const root = app();
    root.replaceChildren();
    const iterationLink = el('a', { href: iterationHref(p.series_slug) }, p.series);
    add(root, el('h1', {}, p.name), el('p', {}, 'Iteracja/seria: ', iterationLink,
      p.raw_of ? el('span', {}, ' · log pozycji surowej przebiegu ', el('a', { href: runHref(p.raw_of_slug) }, lastPart(p.raw_of))) : null,
      p.raw_children.length ? el('span', {}, ' · pozycja surowa: ', p.raw_children.map((s) => el('a', { href: runHref(s) }, lastPart(s.split('~').join('/'))))) : null));

    const role = p.role;
    add(root, el('p', {}, role
      ? [el('span', { class: 'badge ' + role.kind }, role.kind || 'rola nieopisana'), role.repeat !== null ? el('span', { class: 'badge' }, 'powtórzenie ' + role.repeat) : null,
        role.variant ? el('span', { class: 'badge' }, 'wariant ' + role.variant) : null, el('span', { class: 'small muted' }, ' opis: ' + role.origin)]
      : el('span', { class: 'badge' }, 'rola nieopisana (brak runs.json iteracji)')));

    const warnings = [];
    if (p.time_reversals) warnings.push('Czas w logu cofa się ' + p.time_reversals + ' raz(y); dalsze linie przesunięto, by czas rósł.');
    const undescribed = Object.entries(p.undescribed);
    if (undescribed.length) warnings.push('Pola nieopisane w katalogu parsera: ' + undescribed.map(([k, n]) => k + ' (' + n + ')').join(', ') + '.');
    const kinds = Object.entries(p.unknown_kinds);
    if (kinds.length) warnings.push('Nieznane rodzaje linii VP37: ' + kinds.map(([k, n]) => k + ' (' + n + ')').join(', ') + '.');
    warnings.forEach((w) => add(root, el('div', { class: 'warn' }, w)));

    const image = p.image || {};
    const meta = [
      ['Rewizja (pierwsza linia CFG)', p.revision >= 0 ? 'rev' + p.revision + (p.revisions.length > 1 ? ' (zmiany: ' + p.revisions.join(' -> ') + ')' : '') : 'brak linii CFG'],
      ['Log', el('span', { class: 'mono' }, p.log.rel)],
      ['SHA-256 logu (tekst po rozpakowaniu)', el('span', { class: 'mono' }, p.log.sha256)],
      ['Kopie tego samego logu', p.log.duplicates.length ? el('span', { class: 'mono' }, p.log.duplicates.join(', ')) : 'brak'],
      ['Długość nagrania', seconds(p.last_t - p.first_t) + ' (' + fmt(p.first_t, 3) + ' ... ' + fmt(p.last_t, 3) + ' s)'],
      ['Linie VP37 C / IPULSE', p.control_rows + ' / ' + p.pulse_rows],
      ['Start (image.json)', image.started || 'brak image.json'],
      ['Obraz ECU (SHA-256)', el('span', { class: 'mono' }, (image.ecu && image.ecu.sha256) || '-')],
      ['Obraz Adjustometera (SHA-256)', el('span', { class: 'mono' }, (image.adjustometer && image.adjustometer.sha256) || '-')],
      ['Commit Fiesty / HAL', (image.base_commit || '-') + ' / ' + (image.hal_commit || '-')],
      ['Definicje i tryb', [image.JH_EXTRA_DEFINES, image.mode, image.pre_commands && 'pre: ' + image.pre_commands.join(' ')].filter(Boolean).join(' · ') || '-']];
    add(root, el('section', { class: 'panel' }, el('table', {}, meta.map(([k, v]) => el('tr', {}, el('th', {}, k), el('td', {}, v))))));
    if (p.base_commit) add(root, el('details', {}, el('summary', {}, 'base-commit.txt iteracji'), el('pre', { class: 'mono' }, p.base_commit)));

    // Metrics: only bound files, never computed here.
    const metricsBox = el('section', { class: 'panel' }, el('h2', {}, 'Metryki z pliku metryk'));
    add(root, metricsBox);
    if (!p.metrics.length) {
      add(metricsBox, el('p', { class: 'muted' }, 'Brak przypisanych plików metryk. Przeglądarka nie liczy metryk; liczy je skrypt bramki (analyze_gate.py).'));
    }
    for (const m of p.metrics) {
      const w = m.window;
      add(metricsBox, el('h3', {}, el('span', { class: 'mono' }, m.rel), ' ', el('span', { class: 'badge' }, m.basis), el('span', { class: 'badge ' + (m.status === 'bound' ? 'good' : 'bad') }, m.status_text)),
        el('p', { class: 'small' }, w ? 'Okno analizy: ' + w.kind + ' ' + fmt(w.start_s, 3) + ' ... ' + fmt(w.end_s, 3) + ' s' + (w.start_marker ? ' (' + w.start_marker + ' -> ' + w.end_marker + ')' : '') : 'Plik nie zapisał okna analizy.'),
        m.warnings && m.warnings.length ? el('div', { class: 'warn' }, 'Ostrzeżenia analizatora: ' + JSON.stringify(m.warnings)) : null,
        metricsTable(m.values));
    }
    if (p.near_metrics.length) {
      add(metricsBox, el('h3', {}, 'Pliki metryk obok, które nie należą do tego przebiegu'),
        el('table', {}, p.near_metrics.map((m) => el('tr', {}, el('td', { class: 'mono' }, m.rel), el('td', {}, m.status_text),
          el('td', { class: 'mono small' }, m.run ? 'należy do ' + m.run : m.reason || m.source)))));
    }

    // Timeline of tests.
    const t0 = p.first_t, t1 = p.last_t;
    const timelineBox = el('section', { class: 'panel' }, el('h2', {}, 'Oś testów'));
    add(root, timelineBox);
    const timeline = el('div', { class: 'timeline', title: 'kliknij test, aby powiększyć' });
    const legend = {};
    const pos = (t) => ((t - t0) / (t1 - t0 || 1)) * 100 + '%';
    for (const t of p.tests) {
      if (t.test === 'sequence' || t.test === 'none') continue;
      legend[t.test] = testColor(t.test);
      const seg = el('div', { class: 'seg', style: 'left:' + pos(t.start) + ';width:calc(' + pos(t.end) + ' - ' + pos(t.start) + ' + 1px);background:' + testColor(t.test),
        title: t.test + ' ' + fmt(t.start, 1) + '...' + fmt(t.end, 1) + ' s' + (t.result ? ' (' + t.result + ')' : '') + (t.closed ? '' : ' (bez końca w logu)') });
      seg.addEventListener('click', (e) => { e.stopPropagation(); zoom(t.start - 1, t.end + 1); });
      add(timeline, seg);
    }
    p.steps.forEach((s) => add(timeline, el('div', { class: 'step', style: 'left:' + pos(s.t), title: s.test + ' ' + s.percent + ' % (' + s.index + '/' + s.count + ')' })));
    add(timeline, el('div', { class: 'win', style: 'left:' + pos(p.window.start) + ';width:calc(' + pos(p.window.end) + ' - ' + pos(p.window.start) + ')', title: p.window.text }));
    add(timelineBox, timeline, el('div', { class: 'chart' }, el('div', { class: 'legend' },
      Object.entries(legend).map(([k, c]) => el('span', {}, el('i', { style: 'background:' + c + ';height:8px' }), k)),
      el('span', {}, el('i', { style: 'background:var(--accent);height:8px' }), 'okno: ' + p.window.text))));

    // Charts.
    const chartsBox = el('section', { class: 'panel' }, el('h2', {}, 'Przebiegi'));
    add(root, chartsBox);
    const status = el('span', { class: 'small muted' });
    const extras = new Set();
    const controls = el('div', { class: 'controls' },
      el('button', { onclick: () => zoom(t0, t1) }, 'Cały log'),
      el('button', { onclick: () => zoom(p.window.start, p.window.end) }, 'Okno analizy'),
      el('button', { onclick: () => pan(-0.5) }, '←'), el('button', { onclick: () => pan(0.5) }, '->'),
      el('button', { onclick: () => scale(2) }, 'oddal ×2'), status);
    const extraControls = el('div', { class: 'controls' }, el('span', { class: 'muted small' }, 'Dodatkowe serie:'),
      EXTRA.map(([key, label]) => el('label', {}, el('input', { type: 'checkbox', onchange: (e) => { if (e.target.checked) extras.add(key); else extras.delete(key); rebuildCharts(); } }), label)));
    add(chartsBox, controls, extraControls, el('p', { class: 'small muted' }, 'Przeciągnij po wykresie, aby powiększyć; dwuklik wraca do całości. Widok węższy niż ' + FULL_SPAN + ' s pokazuje wszystkie próbki, szerszy minimum i maksimum każdego przedziału.'));
    const chartArea = el('div', {});
    add(chartsBox, chartArea);

    const overview = await need(p.slug, 'overview');
    let view = { x0: t0, x1: t1 };
    let charts = [];
    function zoom(a, b) {
      view = { x0: Math.max(t0, a), x1: Math.min(t1, b) };
      if (view.x1 - view.x0 < 0.05) view.x1 = view.x0 + 0.05;
      update();
    }
    function pan(f) { const s = view.x1 - view.x0; zoom(view.x0 + f * s, view.x1 + f * s); }
    function scale(f) { const c = (view.x0 + view.x1) / 2, s = (view.x1 - view.x0) * f / 2; zoom(c - s, c + s); }
    const opts = (yLabel, height) => ({ height: height || 170, yLabel, onZoom: zoom, onReset: () => zoom(t0, t1), xFormat: (v) => fmt(v, 1), tipX: (v) => 't = ' + fmt(v, 3) + ' s' });
    function rebuildCharts() {
      chartArea.replaceChildren();
      charts = [{ keys: [['tar', '--tar'], ['des', '--des'], ['adj', '--adj']], plot: new Plot(chartArea, opts('Hz', 260)) },
        { keys: [['err', '--err']], plot: new Plot(chartArea, opts('err Hz', 140)) }];
      for (const [key, label, unit] of EXTRA) {
        if (!extras.has(key)) continue;
        const keys = key === 'current' ? [['Ion', '--des', 'IPULSE'], ['I95', '--adj', 'IPULSE']] : key === 'hz' ? [['hz', '--des'], ['hzraw', '--adj']] : [[key, '--err']];
        charts.push({ keys, plot: new Plot(chartArea, opts(label + (unit ? ' [' + unit + ']' : ''), 130)) });
      }
      update();
    }
    let generation = 0;
    async function update() {
      const my = ++generation;
      const span = view.x1 - view.x0;
      let source = null;
      if (span <= FULL_SPAN && overview.fragments) {
        const first = Math.max(0, Math.floor((view.x0 - overview.fragment_t0) / overview.fragment_s));
        const last = Math.min(overview.fragments - 1, Math.floor((view.x1 - overview.fragment_t0) / overview.fragment_s));
        status.textContent = 'wczytywanie pełnych próbek...';
        const list = [];
        for (let k = first; k <= last; k++) list.push(need(p.slug, 'frag-' + k));
        source = await Promise.all(list);
        if (my !== generation) return;
        status.textContent = 'pełne próbki (' + (last - first + 1) + ' fragm.)';
      } else {
        status.textContent = 'przerzedzenie: min/max w ' + overview.bucket.toFixed(2).replace('.', ',') + ' s';
      }
      for (const c of charts) {
        const layers = [];
        for (const [key, color, line] of c.keys) {
          const group = line || 'C';
          if (source) {
            const x = [], y = [];
            for (const frag of source) {
              const g = frag[group];
              if (!g || !g[key]) continue;
              for (let i = 0; i < g.t.length; i++) { x.push(frag.t0 + g.t[i] / 1000); y.push(g[key][i]); }
            }
            layers.push({ kind: 'line', name: key, color: css(color), x, y });
          } else {
            const band = overview[group] && overview[group][key];
            if (!band) continue;
            const x = band.lo.map((_, i) => overview.t0 + (i + 0.5) * overview.bucket);
            const half = Math.max(0.6, (c.plot.root.clientWidth - 70) / band.lo.length / 2);
            layers.push({ kind: 'band', name: key, color: css(color), x, lo: band.lo, hi: band.hi, halfWidthPx: half, alpha: 0.55 });
          }
        }
        c.plot.marks = p.steps.filter((s) => s.t >= view.x0 && s.t <= view.x1).map((s) => ({ from: s.t, color: css('--text'), alpha: 0.25 }));
        c.plot.set(layers, view.x0, view.x1);
      }
    }
    // #t=<from>-<to> opens the page zoomed to that time range (seconds).
    const range = /(?:^#|&)t=([\d.]+)-([\d.]+)/.exec(location.hash);
    if (range) view = { x0: +range[1], x1: +range[2] };
    rebuildCharts();

    const histBox = el('section', { class: 'panel' }, el('h2', {}, 'Histogramy w oknie analizy'), el('p', { class: 'small muted' }, 'Okno: ' + p.window.text + ' (' + fmt(p.window.start, 3) + ' ... ' + fmt(p.window.end, 3) + ' s). Wszystkie próbki wchodzą do liczb; oś może być węższa.'));
    add(root, histBox);
    const grid = el('div', { class: 'grid2' });
    add(histBox, grid);
    const bound = p.metrics.find((m) => m.status === 'bound' && m.basis === 'filtered') || p.metrics.find((m) => m.status === 'bound');
    p.histograms.forEach((h) => histogramBlock(grid, h, bound && bound[h.key === 'pdt' ? 'control_period_us' : 'feedback_age_us'] ? { file: bound.rel, v: bound[h.key === 'pdt' ? 'control_period_us' : 'feedback_age_us'] } : null));

    add(root, el('details', {}, el('summary', {}, 'Pola telemetrii w tym logu (' + p.fields.length + ')'),
      el('table', {}, p.fields.map((f) => el('tr', {}, el('td', {}, f.line), el('td', { class: 'mono' }, f.key), el('td', {}, f.unit),
        el('td', {}, f.described ? f.description : el('span', { class: 'badge bad' }, 'nieopisane')))))));
    if (p.config.length) {
      add(root, el('details', {}, el('summary', {}, 'Pierwsza linia CFG'), el('p', { class: 'mono' }, p.config.map(([k, v]) => k + ':' + v).join(' '))));
    }
    if (p.report.markdown) {
      add(root, el('h2', {}, 'report.md iteracji'));
      if (p.report.verdict) add(root, el('div', { class: 'verdict', html: markdown(p.report.verdict) }), el('p', { class: 'small muted' }, 'Sekcja: ' + p.report.section));
      add(root, el('details', {}, el('summary', {}, 'Cały report.md'), el('div', { class: 'md', html: markdown(p.report.markdown) })));
    }
  }

  // ------------------------------------------------------- iteration page
  function cellClass(status, change) {
    if (status === 'worse') return 'cell worse' + (change !== null && Math.abs(change) < SMALL_CHANGE ? ' soft' : '');
    if (status === 'no_increase') return 'cell better' + (change !== null && Math.abs(change) < SMALL_CHANGE ? ' soft' : '');
    return 'cell missing';
  }
  function comparisonLabel(c) {
    return c.candidates.map((x) => x.name).join(', ') + ' wobec ' + c.references.map((r) => r.name + (r.position ? ' (' + (r.position === 'before' ? 'przed' : 'po') + ')' : '')).join(', ');
  }
  function worseCount(c) { return c.rows.filter((r) => r.s === 'worse').length; }
  function worstChange(c) {
    const v = c.rows.filter((r) => r.s === 'worse' && r.p !== null).map((r) => r.p);
    return v.length ? Math.max(...v) : null;
  }

  function comparisonTable(c) {
    const box = el('section', { class: 'panel' });
    add(box, el('h3', {}, comparisonLabel(c)), el('p', { class: 'small' },
      el('span', { class: 'badge' }, c.basis === 'raw' ? 'pozycja surowa' : 'po filtrze'), el('span', { class: 'badge' }, 'źródło: ' + c.described_from),
      c.result_file ? el('span', { class: 'mono' }, c.result_file) : null));
    if (c.problem) add(box, el('div', { class: 'warn' }, c.problem));
    if (!c.rows.length) return box;
    add(box, el('p', {}, 'Metryk gorszych od referencji (próg 0): ', el('strong', { class: 'status-worse' }, String(worseCount(c))), ' z ' + c.rows.length + '; najgorsza zmiana ' + pct(worstChange(c)) + '.'));
    const a = c.references[0] && c.references[0].id, b = c.candidates[0] && c.candidates[0].id;
    if (a && b) add(box, el('p', {}, el('a', { href: compareHref(a, b, { basis: c.basis }) }, 'Porównanie A/B: ' + c.references[0].name + ' -> ' + c.candidates[0].name)));
    add(box, el('details', {}, el('summary', {}, 'Wszystkie metryki porównania'), el('div', { class: 'scroll' }, el('table', {},
      el('tr', {}, el('th', {}, 'metryka'), c.references.map((r) => el('th', { class: 'num' }, r.name)), el('th', { class: 'num' }, 'kandydat'), el('th', { class: 'num' }, 'zmiana'), el('th', {}, 'ocena bramki')),
      c.rows.map((r) => el('tr', {}, el('td', { class: 'mono' }, r.m), r.r.map((v) => el('td', { class: 'num' }, fmt(v))), el('td', { class: 'num' }, fmt(r.c)),
        el('td', { class: 'num' }, pct(r.p)), el('td', { class: 'status-' + r.s }, r.s === 'worse' ? 'gorzej' : r.s === 'no_increase' ? 'bez wzrostu' : 'brak',
          r.p !== null && Math.abs(r.p) < SMALL_CHANGE && r.s !== 'missing' ? el('span', { class: 'soft-note' }, ' (mała zmiana)') : null)))))));
    return box;
  }

  function iterationPage(p) {
    const it = p.iteration;
    const runs = p.runs;
    const root = app();
    root.replaceChildren();
    add(root, el('h1', {}, lastPart(it.series)), el('p', { class: 'muted' }, it.title || it.series));
    if (it.verdict) add(root, el('div', { class: 'verdict', html: markdown(it.verdict) }), el('p', { class: 'small muted' }, 'report.md, sekcja: ' + it.verdict_section));
    if (it.described) {
      add(root, el('p', { class: 'small' }, 'Opis przebiegów i porównań: ', el('span', { class: 'mono' }, it.origin)));
      it.notes.forEach((n) => add(root, el('p', { class: 'small muted' }, n)));
    } else {
      add(root, el('div', { class: 'warn' }, 'Iteracja nie ma opisu przebiegów i porównań (runs.json). Role przebiegów nie są zgadywane.' + (it.description_error ? ' Błąd opisu: ' + it.description_error : '')));
    }
    const ids = it.runs.filter((id) => !runs[id].raw_of);
    const key = ['cyclic 2ms p95_abs_err_hz', 'topzero 95% early_ringing_error_rms_hz', 'random p95_abs_err_hz', 'control_period_us p95'];
    add(root, el('h2', {}, 'Przebiegi (' + ids.length + ')'), el('div', { class: 'scroll' }, el('table', {},
      el('tr', {}, ['przebieg', 'rodzaj', 'powt.', 'wariant', 'rev', 'długość', 'testy', 'metryki'].map((h) => el('th', {}, h)), key.map((k) => el('th', { class: 'num mono' }, k))),
      ids.map((id) => {
        const r = runs[id];
        const m = r.metrics.filtered || r.metrics.raw;
        return el('tr', {}, el('td', {}, el('a', { href: runHref(r.slug) }, r.name)), el('td', {}, r.kind ? el('span', { class: 'badge ' + r.kind }, r.kind) : el('span', { class: 'muted' }, '-')),
          el('td', {}, r.repeat === null ? '-' : String(r.repeat)), el('td', { class: 'mono small' }, r.variant || '-'), el('td', {}, r.revision >= 0 ? String(r.revision) : '-'),
          el('td', {}, seconds(r.duration)), el('td', { class: 'small' }, r.tests.filter((t) => t !== 'sequence' && t !== 'none').join(' ')),
          el('td', { class: 'small' }, Object.keys(r.metrics).join(' + ') || el('span', { class: 'muted' }, 'brak')),
          key.map((k) => el('td', { class: 'num' }, fmt(m && m.values[k]))));
      }))));

    // All runs relative to the median of the references, per metric.
    const refs = ids.filter((id) => runs[id].kind === 'reference');
    add(root, el('h2', {}, 'Przebiegi względem mediany referencji'));
    if (!refs.length) {
      add(root, el('p', { class: 'muted' }, 'Brak opisanych referencji: wykres wymaga ról z runs.json.'));
    } else {
      const famSel = el('select', {}, ['podstawowe', 'cyclic', 'top', 'topzero', 'random', 'manual', 'okres pętli', 'wiek feedbacku'].map((f) => el('option', { value: f }, f)));
      const box = el('div', {});
      add(root, el('div', { class: 'controls' }, el('label', {}, 'Metryki: ', famSel),
        el('span', { class: 'small muted' }, 'pełny znacznik: po filtrze, pusty: pozycja surowa wobec surowych referencji; szary: referencja, kolor: wariant kandydata')), box);
      const variants = [...new Set(ids.map((id) => runs[id].variant))];
      const palette = ['#d9731a', '#7b3fb5', '#c62f2f', '#2b8a4b', '#b58a00', '#0f8a8a'];
      const draw = () => {
        box.replaceChildren();
        const labels = new Set();
        ids.forEach((id) => ['filtered', 'raw'].forEach((b) => runs[id].metrics[b] && Object.keys(runs[id].metrics[b].values).forEach((l) => labels.add(l))));
        let list = [...labels];
        list = famSel.value === 'podstawowe' ? CORE.filter((l) => labels.has(l)) : list.filter((l) => family(l) === famSel.value);
        const x = [], y = [], colors = [], hollow = [], tips = [];
        list.forEach((label, i) => {
          for (const basis of ['filtered', 'raw']) {
            const base = median(refs.map((id) => runs[id].metrics[basis] && runs[id].metrics[basis].values[label]));
            if (!base) continue;
            ids.forEach((id, k) => {
              const m = runs[id].metrics[basis];
              const v = m && m.values[label];
              if (v === undefined || v === null) return;
              x.push(i + (k - ids.length / 2) * (0.5 / Math.max(ids.length, 1)));
              y.push((v / base) * 100);
              colors.push(runs[id].kind === 'reference' ? css('--tar') : palette[Math.max(0, variants.indexOf(runs[id].variant)) % palette.length]);
              hollow.push(basis === 'raw');
              tips.push(runs[id].name + ' (' + basis + '): ' + fmt(v) + ' = ' + fmt((v / base) * 100, 1) + ' % mediany ref.');
            });
          }
        });
        if (!list.length) { add(box, el('p', { class: 'muted' }, 'Brak metryk tej rodziny.')); return; }
        const order = x.map((_, i) => i).sort((a, b) => x[a] - x[b]);
        const plot = new Plot(box, { height: 300, yLabel: '% mediany referencji', xFormat: (v) => '', xTicks: true,
          xTickValues: () => [], yFormat: (v) => fmt(v, 0) });
        plot.set([{ kind: 'line', name: '', color: css('--muted'), x: [-0.5, list.length - 0.5], y: [100, 100], noTip: true, alpha: 0.6 },
          { kind: 'points', name: 'przebiegi', color: css('--accent'), x: order.map((i) => x[i]), y: order.map((i) => y[i]), colors: order.map((i) => colors[i]), hollow: order.map((i) => hollow[i]), tip: (i) => tips[order[i]] }], -0.5, list.length - 0.5);
        add(box, el('ol', { class: 'small mono', start: '0' }, list.map((l) => el('li', {}, l))));
      };
      famSel.addEventListener('change', draw);
      draw();
    }

    add(root, el('h2', {}, 'Porównania (' + it.comparisons.length + ')'));
    if (!it.comparisons.length) add(root, el('p', { class: 'muted' }, 'Brak opisanych porównań.'));
    it.comparisons.forEach((c) => add(root, comparisonTable(c)));

    add(root, el('h2', {}, 'Pliki metryk w iteracji'), el('div', { class: 'scroll' }, el('table', {},
      it.metrics_files.map((m) => el('tr', {}, el('td', { class: 'mono' }, m.rel), el('td', {}, m.basis || '-'),
        el('td', { class: m.status === 'bound' ? 'status-no_increase' : 'status-worse' }, m.status_text),
        el('td', { class: 'mono small' }, m.run ? lastPart(m.run) : m.reason || m.source))))));
    if (it.base_commit) add(root, el('details', {}, el('summary', {}, 'base-commit.txt'), el('pre', { class: 'mono' }, it.base_commit)));
    if (it.report) add(root, el('h2', {}, 'report.md'), el('div', { class: 'md', html: markdown(it.report) }));
  }

  // -------------------------------------------------------- overview page
  async function overviewPage() {
    const cat = await need('', 'catalog');
    const root = app();
    root.replaceChildren();
    const its = cat.iterations.slice().sort((a, b) => (b.date + b.series).localeCompare(a.date + a.series));
    const gate = its.filter((i) => i.area === 'vp37_implementation_tests');
    const runCount = Object.values(cat.runs).filter((r) => !r.raw_of).length;
    add(root, el('h1', {}, 'Pomiary VP37'), el('div', { class: 'kpis' },
      [['iteracje bramki', gate.length], ['serie pomiarów', its.length - gate.length], ['przebiegi', runCount],
        ['iteracje bez opisu porównań', gate.filter((i) => !i.described).length], ['obowiązująca baza', cat.baseline || '-'],
        ['logi bez telemetrii VP37', cat.skipped]].map(([l, v]) => el('div', { class: 'kpi' }, el('div', { class: 'v' }, String(v)), el('div', { class: 'l' }, l)))));

    const areaSel = el('select', {}, [['vp37_implementation_tests', 'iteracje bramki'], ['all', 'wszystkie serie']].map(([v, l]) => el('option', { value: v }, l)));
    const famSel = el('select', {}, ['podstawowe', 'cyclic', 'top', 'topzero', 'random', 'manual', 'okres pętli', 'wiek feedbacku', 'wszystkie'].map((f) => el('option', { value: f }, f)));
    const revisions = [...new Set(Object.values(cat.runs).map((r) => r.revision).filter((r) => r >= 0))].sort((a, b) => a - b);
    const revSel = el('select', {}, el('option', { value: '' }, 'każda'), revisions.map((r) => el('option', { value: r }, 'rev' + r)));
    const from = el('input', { type: 'date' }), to = el('input', { type: 'date' });
    const text = el('input', { type: 'search', placeholder: 'seria...' });
    add(root, el('h2', {}, 'Mapa porównań'), el('p', { class: 'small muted' }, 'Wiersz to jedno porównanie kandydata z jego referencjami. Kolor: ocena bramki przy progu 0 (czerwony: metryka wzrosła wobec którejkolwiek referencji). Blady odcień: zmiana poniżej ' + SMALL_CHANGE + ' %, tylko wizualnie. Kliknięcie komórki otwiera porównanie A/B.'),
      el('div', { class: 'controls' }, el('label', {}, 'Zakres ', areaSel), el('label', {}, 'Metryki ', famSel), el('label', {}, 'Rewizja ', revSel), el('label', {}, 'Od ', from), el('label', {}, 'Do ', to), text));
    const mapBox = el('div', { class: 'scroll' });
    const lineBox = el('div', {});
    const metricSel = el('select', {});
    add(root, mapBox, el('h2', {}, 'Linia referencji'), el('p', { class: 'small muted' }, 'Jeden punkt na iterację: mediana jej referencji (po filtrze) dla wybranej metryki.'), el('div', { class: 'controls' }, el('label', {}, 'Metryka ', metricSel)), lineBox);
    const allLabels = new Set();
    Object.values(cat.runs).forEach((r) => r.metrics.filtered && Object.keys(r.metrics.filtered.values).forEach((l) => allLabels.add(l)));
    [...CORE.filter((l) => allLabels.has(l)), ...[...allLabels].filter((l) => !CORE.includes(l)).sort()].forEach((l) => add(metricSel, el('option', { value: l }, l)));

    const visible = () => its.filter((i) => (areaSel.value === 'all' || i.area === areaSel.value) &&
      (!from.value || i.date >= from.value) && (!to.value || i.date <= to.value) &&
      (!text.value || i.series.toLowerCase().includes(text.value.toLowerCase())) &&
      (!revSel.value || i.runs.some((id) => String(cat.runs[id].revision) === revSel.value)));

    const drawMap = () => {
      const list = visible();
      const labels = new Set();
      list.forEach((i) => i.comparisons.forEach((c) => c.rows.forEach((r) => labels.add(r.m))));
      let cols = famSel.value === 'podstawowe' ? CORE.filter((l) => labels.has(l)) : famSel.value === 'wszystkie' ? [...labels].sort() : [...labels].filter((l) => family(l) === famSel.value).sort();
      const table = el('table', { class: 'map' }, el('tr', {}, el('th', {}, 'porównanie'), el('th', { class: 'num' }, 'gorszych'), el('th', { class: 'num' }, 'najgorsza'), cols.map((c) => el('th', { class: 'metric mono' }, c))));
      for (const i of list) {
        add(table, el('tr', { class: 'iter' }, el('td', { colspan: 3 + cols.length }, el('a', { href: iterationHref(i.slug) }, lastPart(i.series)),
          i.described ? null : el('span', { class: 'badge bad' }, 'brak opisu porównań'))));
        for (const c of i.comparisons) {
          const rows = Object.fromEntries(c.rows.map((r) => [r.m, r]));
          const a = c.references[0] && c.references[0].id, b = c.candidates[0] && c.candidates[0].id;
          add(table, el('tr', {}, el('td', { class: 'label small' }, comparisonLabel(c), ' ', el('span', { class: 'badge' }, c.basis === 'raw' ? 'surowa' : 'filtr'), c.problem ? el('span', { class: 'badge bad', title: c.problem }, '!') : null),
            el('td', { class: 'num status-worse' }, c.rows.length ? String(worseCount(c)) : '-'), el('td', { class: 'num' }, pct(worstChange(c))),
            cols.map((m) => {
              const r = rows[m];
              if (!r) return el('td', { class: 'cell missing' }, '');
              return el('td', { class: cellClass(r.s, r.p), title: m + '\nkandydat ' + fmt(r.c) + '\nreferencje ' + r.r.map((v) => fmt(v)).join(', ') + '\n' + r.s,
                onclick: () => a && b && (location.href = compareHref(a, b, { basis: c.basis, metric: m })) }, pct(r.p));
            })));
        }
      }
      mapBox.replaceChildren(table);
    };
    const drawLine = () => {
      lineBox.replaceChildren();
      const label = metricSel.value;
      const points = visible().filter((i) => i.area === 'vp37_implementation_tests').slice().reverse().map((i) => {
        const refs = i.runs.filter((id) => cat.runs[id].kind === 'reference');
        return { i, v: median(refs.map((id) => cat.runs[id].metrics.filtered && cat.runs[id].metrics.filtered.values[label])) };
      }).filter((pt) => pt.v !== null);
      if (!points.length) { add(lineBox, el('p', { class: 'muted' }, 'Brak opisanych referencji z tą metryką.')); return; }
      const plot = new Plot(lineBox, { height: 240, yLabel: label, xFormat: (v) => (points[Math.round(v)] ? points[Math.round(v)].i.date.slice(5) : ''),
        xTickValues: (a, b) => points.map((_, k) => k).filter((k) => k >= a && k <= b), tipX: (v) => (points[Math.round(v)] ? lastPart(points[Math.round(v)].i.series) : '') });
      plot.set([{ kind: 'line', name: 'mediana referencji', color: css('--accent'), x: points.map((_, k) => k), y: points.map((pt) => pt.v) },
        { kind: 'points', name: '', noTip: true, color: css('--accent'), x: points.map((_, k) => k), y: points.map((pt) => pt.v) }], -0.5, points.length - 0.5);
    };
    [areaSel, famSel, revSel, from, to, text].forEach((c) => c.addEventListener('input', () => { drawMap(); drawLine(); }));
    metricSel.addEventListener('change', drawLine);
    drawMap();
    drawLine();
  }

  // ---------------------------------------------------------- A/B page
  async function comparePage() {
    const cat = await need('', 'catalog');
    const root = app();
    root.replaceChildren();
    const params = new URLSearchParams(location.hash.slice(1));
    const ids = Object.keys(cat.runs).sort();
    const runSelect = (value) => {
      const s = el('select', {}, el('option', { value: '' }, '- wybierz przebieg -'));
      const groups = {};
      ids.forEach((id) => (groups[cat.runs[id].series] = groups[cat.runs[id].series] || []).push(id));
      Object.keys(groups).sort().reverse().forEach((g) => add(s, el('optgroup', { label: g }, groups[g].map((id) =>
        el('option', { value: id, selected: id === value }, cat.runs[id].name + (cat.runs[id].kind ? ' [' + cat.runs[id].kind + ']' : ''))))));
      return s;
    };
    const aSel = runSelect(params.get('a')), bSel = runSelect(params.get('b'));
    const testSel = el('select', {}), basisSel = el('select', {}, ['filtered', 'raw'].map((b) => el('option', { value: b, selected: b === (params.get('basis') || 'filtered') }, b === 'raw' ? 'pozycja surowa' : 'po filtrze')));
    const norm = el('input', { type: 'checkbox', checked: true });
    add(root, el('h1', {}, 'Porównanie A/B'), el('div', { class: 'controls' }, el('label', {}, el('strong', { style: 'color:var(--a)' }, 'A'), aSel), el('label', {}, el('strong', { style: 'color:var(--b)' }, 'B'), bSel)),
      el('div', { class: 'controls' }, el('label', {}, 'Skoki: ', testSel), el('label', {}, norm, 'położenie w % skoku'), el('label', {}, 'Metryki: ', basisSel)),
      el('p', { class: 'small muted' }, 'Strona pokazuje liczby; werdykt bramki wydaje report.md iteracji (AGENTS.md §12).'));
    const chartBox = el('section', { class: 'panel' }), tableBox = el('section', { class: 'panel' });
    add(root, chartBox, tableBox);
    let steps = { a: [], b: [] };
    const save = () => {
      const q = new URLSearchParams({ a: aSel.value, b: bSel.value, basis: basisSel.value });
      if (testSel.value) q.set('test', testSel.value);
      if (params.get('metric')) q.set('metric', params.get('metric'));
      history.replaceState(null, '', '#' + q.toString());
    };
    const drawSteps = () => {
      chartBox.replaceChildren(el('h2', {}, 'Skoki nałożone, wyrównane do chwili skoku'));
      const key = testSel.value;
      if (!key) { add(chartBox, el('p', { class: 'muted' }, 'Brak wspólnych skoków (testy top/topzero) w obu przebiegach.')); return; }
      const layers = [];
      for (const [side, color] of [['a', '--a'], ['b', '--b']]) {
        steps[side].filter((s) => s.test + ' ' + s.percent === key).forEach((s, k) => {
          const x = s.dt, y = s.adj.map((v, i) => {
            if (!norm.checked) return v;
            const start = s.adj[s.dt.findIndex((t) => t >= 0)] ?? s.adj[0];
            const goal = s.tar[s.tar.length - 1];
            return goal === start ? null : ((v - start) / (goal - start)) * 100;
          });
          layers.push({ kind: 'line', name: side.toUpperCase() + ' ' + (s.index || k + 1) + '/' + (s.count || '?'), color: css(color), x, y, alpha: 0.85, noLegend: k > 0 });
        });
      }
      const plot = new Plot(chartBox, { height: 320, yLabel: norm.checked ? '% skoku' : 'adj Hz', xFormat: (v) => fmt(v, 0) + ' ms' });
      plot.set(layers, -300, 2500);
      add(chartBox, el('p', { class: 'small muted' }, 'Skoki A: ' + steps.a.filter((s) => s.test + ' ' + s.percent === key).length + ', B: ' + steps.b.filter((s) => s.test + ' ' + s.percent === key).length + '. Położenie adj względem chwili linii TEST (oś w ms).'));
    };
    const drawTable = () => {
      tableBox.replaceChildren(el('h2', {}, 'Metryki A -> B'));
      const a = cat.runs[aSel.value], b = cat.runs[bSel.value];
      if (!a || !b) { add(tableBox, el('p', { class: 'muted' }, 'Wybierz dwa przebiegi.')); return; }
      const ma = a.metrics[basisSel.value], mb = b.metrics[basisSel.value];
      if (!ma || !mb) { add(tableBox, el('p', { class: 'warn' }, 'Brak przypisanych metryk (' + basisSel.value + ') dla ' + (!ma ? 'A' : 'B') + '. Strona ich nie liczy.')); return; }
      add(tableBox, el('p', { class: 'small mono' }, 'A: ' + ma.file + ' · B: ' + mb.file));
      const labels = [...new Set([...Object.keys(ma.values), ...Object.keys(mb.values)])];
      const focus = params.get('metric');
      let worse = 0;
      const rows = labels.map((l) => {
        const va = ma.values[l], vb = mb.values[l];
        const missing = va === undefined || vb === undefined;
        const change = !missing && va !== 0 ? ((vb - va) / Math.abs(va)) * 100 : null;
        const status = missing ? 'missing' : vb - va > 0 ? 'worse' : 'no_increase';
        if (status === 'worse') worse++;
        return el('tr', { style: l === focus ? 'outline:2px solid var(--accent)' : null }, el('td', { class: 'mono' }, l), el('td', { class: 'num' }, fmt(va)), el('td', { class: 'num' }, fmt(vb)),
          el('td', { class: 'num' }, pct(change)), el('td', { class: 'status-' + status }, status === 'worse' ? 'gorzej' : status === 'no_increase' ? 'bez wzrostu' : 'brak',
            change !== null && Math.abs(change) < SMALL_CHANGE && status !== 'missing' ? el('span', { class: 'soft-note' }, ' (mała zmiana)') : null));
      });
      add(tableBox, el('p', {}, 'Metryk B gorszych od A (próg 0): ', el('strong', { class: 'status-worse' }, String(worse)), ' z ' + labels.length + '.'),
        el('div', { class: 'scroll' }, el('table', {}, el('tr', {}, el('th', {}, 'metryka'), el('th', { class: 'num' }, 'A'), el('th', { class: 'num' }, 'B'), el('th', { class: 'num' }, 'zmiana'), el('th', {}, 'ocena (próg 0)')), rows)));
    };
    const reload = async () => {
      save();
      const a = cat.runs[aSel.value], b = cat.runs[bSel.value];
      steps = { a: a ? await need(a.slug, 'steps') : [], b: b ? await need(b.slug, 'steps') : [] };
      const keys = (list) => new Set(list.map((s) => s.test + ' ' + s.percent));
      const ka = keys(steps.a), kb = keys(steps.b);
      const common = [...ka].filter((k) => kb.has(k)).sort();
      const wanted = testSel.value || params.get('test');
      testSel.replaceChildren(...common.map((k) => el('option', { value: k, selected: k === wanted }, k + ' %')));
      drawSteps();
      drawTable();
    };
    [aSel, bSel].forEach((s) => s.addEventListener('change', reload));
    testSel.addEventListener('change', () => { save(); drawSteps(); });
    norm.addEventListener('change', drawSteps);
    basisSel.addEventListener('change', () => { save(); drawTable(); });
    reload();
  }

  VP37.start = function () {
    const page = VP37.page || {};
    const run = { run: () => runPage(page), iteration: () => iterationPage(page), overview: overviewPage, compare: comparePage }[page.kind];
    if (!run) return;
    Promise.resolve(run()).catch((error) => app().append(el('div', { class: 'warn' }, 'Błąd strony: ' + error.message)));
  };
  VP37.markdown = markdown;
})();
