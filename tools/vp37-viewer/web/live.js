/* Live adapter for the shared canvas renderer. No build step or dependencies. */
(function () {
  'use strict';
  const VP37 = window.VP37;
  const { t: tr } = VP37.i18n;
  const { Plot, el, add, fmt, css } = VP37.ui;
  const LIMITS = Object.freeze({ C: 4096, IPULSE: 4096, CFG: 512, ADJ: 512,
    fields: 256, events: 256, text: 256, traceSamples: 2048, traces: 4 });
  const finite = (v) => typeof v === 'number' && Number.isFinite(v);
  class Ring {
    constructor(capacity) { this.capacity = capacity; this.data = []; this.start = 0; }
    push(row) {
      if (this.data.length < this.capacity) this.data.push(row);
      else { this.data[this.start] = row; this.start = (this.start + 1) % this.capacity; }
    }
    rows() { return this.data.slice(this.start).concat(this.data.slice(0, this.start)); }
  }
  // Panel membership is presentation; descriptions and units come only from the server.
  const PANELS = [
    ["Position","Compare the requested target, slew-limited target and measured position relative to the sensor baseline.","C",["tar","des","adj"],null],
    ["Position error","Positive error means the measured position is below the control target.","C",["err"],null],
    ["Sensor frequency","Raw and filtered frequencies include the baseline; position is shown separately.","C",["hzraw","hz"],null],
    ["Actuator command","Compare the final PWM output with the base command before PID correction.","C",["pwm","ff"],null],
    ["PID correction","Individual PID contributions and their combined, limited correction; limits apply to the correction.","C",["P","I","D","corr","lo","hi"],null],
    ["Controller states","Filled bands indicate active states; faint bands indicate inactive states.","C",["ih","cg","sf","hw","rest"],"states"],
    ["Current","ON-phase current statistics, the current target and the observed current error.","IPULSE",["Ion","I95","ctar","cerr"],null],
    ["Current correction","Current control contribution to the PWM command, in native counts.","IPULSE",["cpwm"],null],
    ["Supply voltage","Compare the voltage used by the loop with the value retained by compensation.","C",["V","Vc"],null],
    ["Voltage compensation","Voltage correction factor applied by the control loop.","C",["vcor"],null],
    ["Thermal compensation","Thermal scaling used by the control loop.","C",["tsc"],null],
    ["Fuel temperature","Fuel temperature used by the controller.","C",["ft"],null],
    ["Loop timing","Compare the loop period, feedback age and I2C read duration.","C",["pdt","age","ious"],null],
    ["Execution timing","Time spent collecting the current scan and executing the control step.","CFG",["scanus","execus"],null],
    ["Adjustometer frequency","Frequency reported directly by the position sensor.","ADJ",["f"],null],
    ["Baseline deviation","Signed deviation from the sensor baseline, in Hz.","ADJ",["d"],null],
    ["Adjustometer supply","Raw reading in decivolts: 120 dV corresponds to 12 V.","ADJ",["v"],null],
    ["Adjustometer temperature","Fuel temperature reported directly by the sensor.","ADJ",["ft"],null],
    ["Adjustometer status","Each band shows one status bit. All clear means no reported fault; unnamed bits retain their numbers.","ADJ",["s"],"bits"]
  ];
  class LiveModel {
    constructor(catalog) {
      this.fields = new Map();
      for (const f of catalog.slice(0, LIMITS.fields)) this.fields.set(f.kind + '.' + f.key, f);
      this.buffers = Object.fromEntries(['C', 'IPULSE', 'CFG', 'ADJ'].map((k) => [k, new Ring(LIMITS[k])]));
      this.events = new Ring(LIMITS.events); this.traces = []; this.pending = [];
      this.traceCount = 0; this.traceBad = ''; this.traceId = 0;
      this.segment = 0; this.epoch = null; this.next = null; this.ready = false;
      this.latest = null; this.offset = null; this.lastUs = null; this.boundary = "Reception started";
      this.ignored = 0; this.snapshot = null; this.range = null; this.selected = null;
    }
    finishTrace(expected, reason) {
      if (!this.traceCount && expected === null) return;
      const complete = !reason && !this.traceBad && expected === this.traceCount;
      const trace = { id: ++this.traceId, rows: this.pending, expected, received: this.traceCount,
        complete, reason: reason || this.traceBad || (complete ? '' : "Sample count differs from trace end") };
      this.traces.push(trace);
      if (this.traces.length > LIMITS.traces) this.traces.shift();
      this.pending = []; this.traceCount = 0; this.traceBad = '';
      if (expected !== null && !this.snapshot) this.selectTrace(trace.id);
      return trace;
    }
    gap(reason) {
      this.finishTrace(null, reason);
      this.segment++; this.offset = null; this.lastUs = null; this.boundary = reason;
    }
    disconnect() { this.gap("Disconnected - lost data is not replayed"); this.ready = false; }
    receive(message) {
      if (message.type === 'hello' || message.type === 'reset') {
        if (message.protocol !== 1 || !Number.isSafeInteger(message.next) || !Number.isSafeInteger(message.epoch))
          throw new Error("Unsupported protocol");
        this.gap(message.type === 'hello' ? "New connection - no replay" : "File / epoch changed");
        this.epoch = message.epoch; this.next = message.next; this.ready = true;
        return;
      }
      if (message.type !== 'samples' || !this.ready) return;
      if (!Number.isSafeInteger(message.seq) || !Array.isArray(message.records)) throw new Error("Invalid batch");
      if (message.epoch !== this.epoch || message.seq !== this.next) {
        this.gap("Stream gap");
        // Old/duplicate batches must not be drawn a second time.
        if (message.epoch === this.epoch && message.seq < this.next) return;
        this.epoch = message.epoch;
      }
      this.next = message.seq + 1;
      for (const r of message.records) this.record(r);
    }
    record(r) {
      if (!finite(r.t)) return;
      if (r.kind === 'trace:' && /^HAL_OK samples:\d+\s*$/.test(r.text || '')) {
        this.finishTrace(null, 'Next trace started before trace end');
        return;
      }
      if (r.kind === 'trace') {
        const end = /^end samples:(\d+)\s*$/.exec(r.text || '');
        if (end) this.finishTrace(Number(end[1]), '');
        return;
      }
      const kind = r.kind === 'T' ? 'C' : r.kind;
      if (r.values && (this.buffers[kind])) {
        const values = Object.create(null);
        for (const key of Object.keys(r.values)) {
          if (this.fields.has(kind + '.' + key)) values[key] = finite(r.values[key]) ? r.values[key] : null;
          else this.ignored = Math.min(Number.MAX_SAFE_INTEGER, this.ignored + 1);
        }
        if (r.kind === 'T') {
          this.traceCount = Math.min(Number.MAX_SAFE_INTEGER, this.traceCount + 1);
          const prev = this.pending[this.pending.length - 1];
          if (!finite(values.us)) this.traceBad = "Missing MCU timestamp us";
          if (prev && r.t <= prev.source) this.traceBad = "Trace time is not increasing";
          if (this.pending.length < LIMITS.traceSamples)
            this.pending.push({ kind: 'C', t: r.t, source: r.t, segment: this.segment, values });
          else this.traceBad = "Trace sample limit exceeded";
          return;
        }
        if (kind === 'C' && finite(values.us)) {
          if (r.restart || (this.lastUs !== null && ((values.us - this.lastUs) >>> 0) > 0x7fffffff)) this.gap("MCU clock restarted");
          this.lastUs = values.us;
        }
        if (this.offset === null) {
          // New transport sessions may reuse epochs and timestamps. Keep them apart.
          this.offset = this.latest === null ? -r.t : this.latest + 0.1 - r.t;
          this.events.push({ t: r.t + this.offset, source: r.t, segment: this.segment, text: this.boundary, gap: true });
        }
        const row = { kind, t: r.t + this.offset, source: r.t, segment: this.segment, values };
        this.buffers[kind].push(row);
        this.latest = Math.max(this.latest === null ? row.t : this.latest, row.t);
      } else if (typeof r.text === 'string') {
        this.events.push({ t: this.offset === null ? (this.latest || 0) : r.t + this.offset,
          source: r.t, segment: this.segment,
          text: (String(r.kind) + ': ' + r.text).slice(0, LIMITS.text) });
      }
    }
    current() {
      if (this.snapshot) return this.snapshot;
      const b = this.latest === null ? 60 : this.latest;
      return this.slice(Math.max(0, b - 60), Math.max(0.05, b));
    }
    slice(a, b) {
      const rows = Object.fromEntries(Object.entries(this.buffers).map(([kind, ring]) =>
        [kind, ring.rows().filter((r) => r.t >= a && r.t <= b).sort((x, y) => x.t - y.t)]));
      return { rows, events: this.events.rows().filter((e) => e.t >= a && e.t <= b), a, b };
    }
    pause() {
      if (!this.snapshot) { this.snapshot = this.current(); this.range = [this.snapshot.a, this.snapshot.b]; }
    }
    resume() { this.snapshot = null; this.range = null; this.selected = null; }
    selectTrace(id) {
      const trace = this.traces.find((t) => t.id === id);
      if (!trace || !trace.rows.length) return;
      const origin = trace.rows[0].source;
      const rows = trace.rows.map((r) => ({ ...r, t: r.source - origin }));
      const a = 0, b = Math.max(0.000001, rows[rows.length - 1].t);
      this.snapshot = { rows: { C: rows }, events: [], a, b };
      this.range = [a, b]; this.selected = trace;
    }
    zoom(a, b) {
      this.pause();
      const lo = Math.max(this.snapshot.a, a), hi = Math.min(this.snapshot.b, b);
      if (hi > lo) this.range = [lo, hi];
    }
    view() {
      const data = this.current();
      return Object.assign({}, data, { a: this.range ? this.range[0] : data.a, b: this.range ? this.range[1] : data.b });
    }
    csv() {
      const view = this.view();
      const quote = (v) => {
        const text = String(v);
        return '"' + (/^[\s]*[=+@-]/.test(text) ? "'" : '') + text.replace(/"/g, '""') + '"';
      };
      const lines = ['time_s,source_s,segment,kind,field,value,unit'];
      for (const rows of Object.values(view.rows)) for (const r of rows) {
        if (r.t < view.a || r.t > view.b) continue;
        for (const [key, value] of Object.entries(r.values)) {
          const field = this.fields.get(r.kind + '.' + key);
          lines.push([r.t, r.source, r.segment, quote(this.selected ? 'T' : r.kind), quote(key),
            value === null ? '' : value, quote(field.unit)].join(','));
        }
      }
      for (const e of view.events) if (e.t >= view.a && e.t <= view.b)
        lines.push([e.t, e.source, e.segment, 'event', quote(e.text), '', ''].join(','));
      return lines.join('\r\n') + '\r\n';
    }
  }
  const COLORS = ['--tar', '--des', '--adj', '--err', '--better', '--worse', '--accent', '--muted'];
  function layerFor(model, view, kind, key, index, mode) {
    const field = model.fields.get(kind + '.' + key);
    const rows = (view.rows[kind] || []).filter((r) => r.t >= view.a && r.t <= view.b);
    const has = rows.some((r) => finite(r.values[key]));
    const info = VP37.i18n.field(field, model.selected && kind === 'C' ? 'T' : kind, key);
    const layer = { ...info, kind: mode ? 'state' : 'line', name: info.name + (has ? '' : tr(" - no data in range")),
      color: css(COLORS[index % COLORS.length]), maxGap: 2,
      x: rows.map((r) => r.t), y: rows.map((r) => r.values[key] ?? null), segment: rows.map((r) => r.segment) };
    if (mode) {
      layer.row = index;
      layer.end = rows.map((r, i) => {
        const next = rows[i + 1];
        return next && next.segment === r.segment && next.t - r.t <= 2 ? next.t : r.t;
      });
    }
    return layer;
  }
  function download(blob, name) {
    const url = URL.createObjectURL(blob);
    const a = el('a', { href: url, download: name }); document.body.append(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  }
  function createLive(root, catalog) {
    const model = new LiveModel(catalog);
    root.replaceChildren();
    let statusMessage = 'Connecting...';
    const status = el('strong', { role: 'status' }, tr(statusMessage));
    const labels = [];
    const label = (node, text) => { labels.push([node, text]); node.textContent = tr(text); return node; };
    const summary = el('span', { class: 'small mono' });
    const pause = el('button', { id: 'live-pause', onclick: () => { model.snapshot ? model.resume() : model.pause(); render(); } }, tr("Pause"));
    const traces = el('select', { id: 'live-traces', onchange: () => { if (traces.value) model.selectTrace(Number(traces.value)); else model.resume(); render(); } });
    const events = el('div', { class: 'small mono live-events' });
    const cursor = el('div', { class: 'small mono live-cursor' }, tr("Cursor: move over a chart"));
    const charts = [];
    const png = () => {
      render();
      const visible = charts.filter((c) => !model.selected || c.kind === 'C');
      const canvas = document.createElement('canvas');
      canvas.width = Math.max(320, Math.min(1600, ...visible.map((c) => c.plot.canvas.clientWidth)));
      const g = canvas.getContext('2d'), lineHeight = 20, margin = 12;
      g.font = '14px system-ui';
      const wrap = (text, color) => {
        const lines = []; let line = '';
        for (const word of text.split(/\s+/)) {
          if (line && g.measureText(line + ' ' + word).width > canvas.width - margin * 2) {
            lines.push({ text: line, color }); line = '';
          }
          line += (line ? ' ' : '') + word;
        }
        if (line) lines.push({ text: line, color });
        return lines;
      };
      const blocks = visible.map((c) => ({ chart: c, lines: [
        ...wrap(tr(c.title), css('--text')), ...wrap(tr(c.explanation), css('--muted')),
        ...c.plot.layers.flatMap((l) => wrap(l.name + (l.unit ? ' [' + l.unit + ']' : ''), l.color))
      ] }));
      const heading = wrap('VP37 ' + (model.selected ? 'T - ' + tr(model.selected.complete ? 'complete' : 'INCOMPLETE') : 'live') +
        ' | ' + summary.textContent, css('--text'));
      canvas.height = margin * 2 + heading.length * lineHeight + blocks.reduce((h, b) =>
        h + b.lines.length * lineHeight + b.chart.plot.opts.height + margin, 0);
      g.fillStyle = css('--panel'); g.fillRect(0, 0, canvas.width, canvas.height);
      g.font = '14px system-ui'; g.textBaseline = 'top';
      let y = margin;
      const write = (lines) => lines.forEach((line) => { g.fillStyle = line.color; g.fillText(line.text, margin, y); y += lineHeight; });
      write(heading);
      for (const block of blocks) {
        write(block.lines);
        g.drawImage(block.chart.plot.canvas, 0, y, canvas.width, block.chart.plot.opts.height);
        y += block.chart.plot.opts.height + margin;
      }
      return canvas;
    };
    const title = label(el('h1'), 'VP37 scope');
    const controls = el('div', { class: 'controls live-controls' }, status, pause,
      label(el('button', { id: 'live-reset', onclick: () => { if (model.snapshot) model.range = [model.snapshot.a, model.snapshot.b]; render(); } }), 'Full paused range'),
      el('button', { id: 'live-csv', onclick: () => { render(); download(new Blob([model.csv()], { type: 'text/csv;charset=utf-8' }), 'vp37-view.csv'); } }, 'CSV'),
      el('button', { id: 'live-png', onclick: () => png().toBlob((blob) => { if (blob) download(blob, 'vp37-view.png'); }) }, 'PNG'), traces, summary);
    traces.setAttribute('aria-label', tr('Current stream'));
    const help = el('p', { class: 'small muted' },
      label(el('span'), '60 s window. Pause and zoom retain the view while data continues to arrive. Drag to zoom; double-click to reset. '),
      label(el('span'), 'Live axis: view seconds, with a 0.1 s gap between sessions (not the disconnection duration). T trace: seconds from the first sample. CSV also retains source time and segment number.'));
    add(root, title, controls, help, cursor, events);
    const languageButton = VP37.i18n.mount(document.querySelector('header.top') || controls);
    let cursorTime = null, traceOptions = '';
    for (const [title, explanation, kind, keys, mode] of PANELS) {
      const panel = el('section', { class: 'panel', 'data-kind': kind, 'data-panel': kind + '.' + keys[0] });
      add(root, panel);
      const unit = model.fields.get(kind + '.' + keys[0]);
      const plot = new Plot(panel, { height: mode ? 125 : 160, yLabel: mode ? '' : (unit ? unit.unit : ''),
        y: mode ? [0, mode === 'bits' ? 8 : keys.length] : undefined,
        xFormat: (v) => fmt(v, model.selected ? 4 : 2), onZoom: (a, b) => { model.zoom(a, b); render(); },
        onReset: () => { if (model.snapshot) model.range = [model.snapshot.a, model.snapshot.b]; render(); },
        onCursor: (t) => { cursorTime = t; renderCursor(); } });
      charts.push({ title, explanation, kind, keys, mode, plot, panel });
    }
    function renderCursor() {
      const values = [];
      charts.forEach(({ plot }) => {
        plot.cursor = cursorTime; plot.draw();
        if (!finite(cursorTime)) return;
        for (const l of plot.layers) {
          let best = -1, distance = Infinity;
          l.x.forEach((t, i) => { if (Math.abs(t - cursorTime) < distance) { best = i; distance = Math.abs(t - cursorTime); } });
          if (best >= 0 && distance < 0.6 && finite(l.y[best])) values.push(l.name + '=' + fmt(l.y[best]) + (l.unit ? ' ' + l.unit : ''));
        }
      });
      cursor.textContent = finite(cursorTime) ? 't=' + fmt(cursorTime, 3) + ' s | ' + values.join(' · ') : tr("Cursor: move over a chart");
    }
    function render() {
      const view = model.view();
      pause.textContent = model.snapshot ? tr("Resume live") : tr("Pause");
      summary.textContent = fmt(view.a, 3) + '...' + fmt(view.b, 3) + ' s' + (model.snapshot ? tr(" | paused") : '') +
        (model.ignored ? tr(" | ignored unknown fields: ") + model.ignored : '');
      const selected = model.selected;
      const signature = VP37.i18n.language + ':' + model.traceId + ':' + (selected ? selected.id : '');
      if (signature !== traceOptions) {
        traceOptions = signature;
        traces.replaceChildren(el('option', { value: '' }, tr("Current stream")), ...model.traces.map((t) =>
          el('option', { value: t.id, disabled: !t.rows.length, selected: selected && selected.id === t.id },
            'T #' + t.id + ': ' + t.received + '/' + (t.expected ?? '?') + (t.complete ? tr(" - complete") : tr(" - INCOMPLETE")))));
        if (selected && !model.traces.includes(selected))
          add(traces, el('option', { value: selected.id, selected: true }, 'T #' + selected.id + tr(" - paused")));
      }
      const texts = view.events.filter((e) => e.t >= view.a && e.t <= view.b).map((e) => fmt(e.t, 3) + ' s: ' + (e.gap ? tr(e.text) : e.text));
      if (selected) texts.unshift('T #' + selected.id + ': ' + (selected.complete ? tr("complete") : tr("INCOMPLETE: ") + tr(selected.reason)));
      if (model.traceCount) texts.push(tr("Receiving T: ") + model.traceCount + tr(" samples (waiting for trace end)"));
      events.textContent = texts.join('\n');
      for (const chart of charts) {
        const { kind, keys, mode, plot } = chart;
        plot.opts.title = tr(chart.title);
        plot.opts.explanation = tr(chart.explanation);
        chart.panel.hidden = !!selected && kind !== 'C';
        let layers = keys.map((key, i) => layerFor(model, view, kind, key, i, mode));
        if (mode === 'bits') layers = Array.from({ length: 8 }, (_, i) => Object.assign({}, layers[0], {
          row: i, ...VP37.i18n.field((model.fields.get('ADJ.s')?.bits || [])[i], 'ADJ', 's[' + i + ']'),
          name: VP37.i18n.field((model.fields.get('ADJ.s')?.bits || [])[i], 'ADJ', 's[' + i + ']').name + (layers[0].y.some(finite) ? '' : tr(" - no data in range")), color: css(COLORS[i]),
          y: layers[0].y.map((v) => finite(v) ? ((v >>> i) & 1) : null) }));
        plot.marks = view.events.filter((e) => e.gap).map((e) => ({ from: e.t, color: css('--worse'), alpha: 0.8 }));
        plot.set(layers, view.a, view.b);
      }
    }
    const languageChanged = () => {
      labels.forEach(([node, text]) => node.textContent = tr(text));
      traces.setAttribute('aria-label', tr('Current stream'));
      status.textContent = tr(statusMessage);
      render(); renderCursor();
    };
    window.addEventListener('vp37-language', languageChanged);
    render();
    return { model, charts, render, png, status,
      setStatus: (text) => { statusMessage = text; status.textContent = tr(text); },
      destroy: () => { window.removeEventListener('vp37-language', languageChanged); languageButton.remove(); charts.forEach((c) => c.plot.destroy()); } };
  }
  VP37.live = { LIMITS, LiveModel, createLive, layerFor };
  VP37.startLive = async function () {
    const root = document.getElementById('app');
    let view, socket, timer, retry, request, suspended = false, generation = 0, failures = 0;
    const current = (id) => !suspended && id === generation;
    const reconnect = () => {
      if (suspended) return;
      view.setStatus('Disconnected - retrying, no replay');
      clearTimeout(retry);
      retry = setTimeout(connect, Math.min(10000, 500 * Math.pow(2, Math.min(failures++, 5))));
    };
    const connect = async () => {
      const id = generation;
      if (!current(id)) return;
      request = new AbortController();
      try {
        const response = await fetch('/api/status', { cache: 'no-store', signal: request.signal });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        const status = await response.json();
        if (!current(id)) return;
        if (!Number.isInteger(status.ws_port) || status.ws_port < 1 || status.ws_port > 65535) throw new Error(tr("WebSocket port"));
        const connection = socket = new WebSocket('ws://' + location.hostname + ':' + status.ws_port + '/ws');
        connection.onmessage = (event) => {
          if (!current(id)) return;
          try {
            view.model.receive(JSON.parse(event.data));
            failures = 0;
            view.setStatus(view.model.ready ? 'Connected' : 'Waiting for hello');
            if (event.data.includes('"present":false')) view.setStatus('Waiting for log file');
          } catch (error) { view.setStatus(error.message); connection.close(); }
        };
        connection.onclose = () => { if (current(id)) { view.model.disconnect(); reconnect(); } };
        connection.onerror = () => connection.close();
      } catch (_) { if (current(id)) reconnect(); }
    };
    const activate = async () => {
      const id = generation;
      try {
        if (!view) {
          request = new AbortController();
          const response = await fetch('/api/fields', { cache: 'no-store', signal: request.signal });
          if (!response.ok) throw new Error(tr("Field catalog: HTTP ") + response.status);
          const catalog = await response.json();
          if (!current(id)) return;
          view = createLive(root, catalog);
        }
        view.setStatus('Connecting...');
        // Rendering is bounded independently of incoming batch frequency.
        timer = setInterval(() => { if (!document.hidden) view.render(); }, 100);
        await connect();
      } catch (error) {
        if (current(id)) add(root, el('div', { class: 'warn' }, tr("View error: ") + error.message));
      }
    };
    const hide = (event) => {
      suspended = true; generation++;
      clearInterval(timer); clearTimeout(retry); request?.abort();
      if (socket) { socket.onmessage = socket.onclose = socket.onerror = null; socket.close(); socket = null; }
      if (view) { view.model.disconnect(); view.setStatus('Disconnected - retrying, no replay'); }
      if (!event.persisted) {
        view?.destroy();
        window.removeEventListener('pagehide', hide);
        window.removeEventListener('pageshow', show);
      }
    };
    const show = (event) => {
      if (event.persisted && suspended) { suspended = false; activate(); }
    };
    window.addEventListener('pagehide', hide);
    window.addEventListener('pageshow', show);
    await activate();
  };
})();
