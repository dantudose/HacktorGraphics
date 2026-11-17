/* Hacktor Watch website animation. UI artwork is exported from the firmware. */
(() => {
  'use strict';

  const params = new URLSearchParams(location.search);
  const canvas = document.getElementById('watch-canvas');
  const ctx = canvas.getContext('2d', { alpha: true });
  const status = document.getElementById('demo-status');
  const toggle = document.getElementById('playback-toggle');
  const root = document.documentElement;
  root.dataset.background = params.get('background') === 'transparent' ? 'transparent' : 'dark';
  root.dataset.embed = String(params.get('embed') === '1');
  root.dataset.controls = String(params.get('controls') !== '0');

  const SIZE = 240;
  const LOOP = 12;
  const HOLD_ANALOG = 5;
  const SWIPE = .8;
  const RETURN_START = LOOP - SWIPE;
  const CASE = { centerX: 640.5, centerY: 840, radius: 486, scale: .61 };
  const CENTER = { x: 480, y: 600 };
  const COLORS = { black: '#000000', white: '#ffffff', secondary: '#c0c0c0', mint: '#a0ebaa', divider: '#404040', red: '#c80000', brightRed: '#ff0000', blue: '#008cff' };
  const sample = { steps: 1957, battery: 79, charging: false };
  const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
  const FULL_DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
  const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
  const face = document.createElement('canvas');
  face.width = face.height = SIZE;
  const f = face.getContext('2d');
  const screen = document.createElement('canvas');
  screen.width = screen.height = SIZE;
  const s = screen.getContext('2d');
  const art = document.createElement('canvas');
  art.width = canvas.width;
  art.height = canvas.height;
  const a = art.getContext('2d');

  let metadata, images;
  let playing = false;
  let visible = true;
  let request = 0;
  let origin = performance.now();
  let elapsed = 0;
  let lastFrame = -Infinity;
  let playbackMode = params.get('mode') === 'fixed' ? 'fixed' : 'live';
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
  const tintCache = new Map();

  const image = url => new Promise((resolve, reject) => {
    const img = new Image();
    img.onload = () => resolve(img);
    img.onerror = () => reject(new Error(`Unable to load watch artwork: ${url}`));
    img.src = url;
  });
  const roundRect = (c, x, y, w, h, r, color) => {
    c.fillStyle = color;
    c.beginPath();
    c.roundRect(x, y, w, h, r);
    c.fill();
  };
  const circle = (c, x, y, radius, color) => {
    c.fillStyle = color;
    c.beginPath();
    c.arc(x, y, radius, 0, Math.PI * 2);
    c.fill();
  };

  function tint(name, color) {
    const key = `${name}:${color}`;
    if (tintCache.has(key)) return tintCache.get(key);
    const source = images[name];
    const result = document.createElement('canvas');
    result.width = source.width;
    result.height = source.height;
    const c = result.getContext('2d');
    c.drawImage(source, 0, 0);
    c.globalCompositeOperation = 'source-in';
    c.fillStyle = color;
    c.fillRect(0, 0, result.width, result.height);
    tintCache.set(key, result);
    return result;
  }

  // Each glyph is rasterized by the same LovyanGFX font used on the watch.
  function fontInfo(name) { return metadata.fonts[name]; }
  function glyph(font, char) { return font.glyphs[char.charCodeAt(0)] || font.glyphs[63]; }
  function width(text, fontName = 'font2') {
    const font = fontInfo(fontName);
    const chars = [...String(text)];
    if (!chars.length) return 0;
    const last = glyph(font, chars.pop());
    const start = glyph(font, String(text)[0]);
    return chars.reduce((sum, char) => sum + glyph(font, char).advance, 0) +
      Math.max(last.advance, last.inkWidth + last.xOffset) + Math.max(0, -start.xOffset);
  }
  function text(value, x, y, options = {}) {
    const str = String(value);
    if (!str) return 0;
    const name = options.font || 'font2';
    const font = fontInfo(name);
    const total = width(str, name);
    const align = options.align || 'left';
    const datum = options.datum || 'top';
    let cursor = x - (align === 'center' ? Math.floor(total / 2) : align === 'right' ? total : 0);
    let top = y - (datum === 'middle' ? Math.floor(font.height / 2) : datum === 'baseline' ? font.baseline : 0);
    const atlas = tint(font.image, options.color || COLORS.secondary);
    const first = glyph(font, str[0]);
    cursor += first.leadingAdjustment || 0;
    for (const char of str) {
      const item = glyph(font, char);
      f.drawImage(atlas, item.x, item.y, font.cellWidth, font.cellHeight,
        cursor - font.padding - (item.leadingAdjustment || 0), top - font.padding,
        font.cellWidth, font.cellHeight);
      cursor += item.advance;
    }
    return total;
  }

  function drawBattery() {
    const x = 145, y = 115;
    f.fillStyle = COLORS.secondary;
    f.fillRect(x, y, 16, 1);
    f.fillRect(x, y + 11, 16, 1);
    f.fillRect(x, y, 1, 12);
    f.fillRect(x + 15, y, 1, 12);
    f.fillRect(x + 16, y + 3, 3, 6);
    f.fillStyle = sample.battery > 20 ? COLORS.blue : COLORS.brightRed;
    f.fillRect(x + 2, y + 2, Math.floor(14 * sample.battery / 100), 8);
    if (sample.charging) {
      f.fillStyle = COLORS.white;
      f.beginPath(); f.moveTo(x + 7, y + 2); f.lineTo(x + 11, y + 2); f.lineTo(x + 8, y + 7); f.fill();
      f.beginPath(); f.moveTo(x + 8, y + 5); f.lineTo(x + 12, y + 5); f.lineTo(x + 9, y + 10); f.fill();
    }
    text(`${String(sample.battery).padStart(3, ' ')}%`, x + 20, y - 2);
  }

  function analog(date) {
    f.clearRect(0, 0, SIZE, SIZE);
    f.drawImage(images['analog-dial.png'], 0, 0);
    const second = date.getSeconds() + date.getMilliseconds() / 1000;
    const minute = date.getMinutes() + second / 60;
    const hour = date.getHours() % 12 + minute / 60;
    text(`${DAYS[date.getDay()]} ${String(date.getDate()).padStart(2, '0')} ${MONTHS[date.getMonth()]}`, 120, 74, { align: 'center', datum: 'middle' });
    drawBattery();
    const count = sample.steps.toLocaleString('en-US');
    const rowWidth = 17 + 4 + width(count);
    const rowX = 120 - Math.floor(rowWidth / 2);
    f.drawImage(tint('shoe-icon.png', COLORS.secondary), rowX, 165);
    text(count, rowX + 21, 170, { datum: 'middle' });
    const dot = (position, color) => {
      const angle = position * Math.PI / 30;
      const radius = position % 5 === 0 ? 4 : 1;
      const dialRadius = 1200 / 10.4;
      circle(f, 120 + Math.sin(angle) * dialRadius, 120 - Math.cos(angle) * dialRadius, radius, color);
    };
    dot(date.getMinutes(), COLORS.brightRed);
    dot(date.getHours() * 5 % 60, COLORS.brightRed);
    dot(date.getSeconds(), COLORS.red);
    const hand = (name, degrees, pivotX, pivotY, scaleX = 1) => {
      f.save();
      f.translate(120, 120);
      f.rotate(degrees * Math.PI / 180);
      f.scale(scaleX, 1);
      f.drawImage(images[name], -pivotX, -pivotY);
      f.restore();
    };
    hand('hour-hand.png', hour * 30, 3, 100);
    hand('minute-hand.png', minute * 6, 3, 100);
    hand('second-hand.png', second * 6, 1, 100, .7);
    circle(f, 120, 120, 6, COLORS.red);
    f.strokeStyle = '#000';
    f.lineWidth = 1;
    f.beginPath(); f.arc(120, 120, 2, 0, Math.PI * 2); f.stroke();
  }

  function digits(value, x, y, color) {
    const atlas = tint('digital-digits.png', color);
    const str = String(value).padStart(2, '0');
    for (let i = 0; i < 2; i++) {
      f.drawImage(atlas, Number(str[i]) * 44, 0, 44, 68, x + i * 44, y, 44, 68);
    }
  }

  function digital(date) {
    f.fillStyle = COLORS.black;
    f.fillRect(0, 0, SIZE, SIZE);
    digits(date.getHours(), 28, 61, COLORS.white);
    digits(date.getMinutes(), 28, 133, COLORS.mint);
    const baselineText = (str, x, y, font = 'font2', color = COLORS.secondary) => text(str, x, y, { font, color, datum: 'baseline' });
    const right = 134;
    const dayWidth = baselineText(date.getDate(), right, 49, 'roboto-thin24', COLORS.mint);
    baselineText(MONTHS[date.getMonth()], right + dayWidth + 4, 49, 'font2', COLORS.white);
    baselineText(FULL_DAYS[date.getDay()], right, 69);
    f.fillStyle = COLORS.divider; f.fillRect(right, 78, 79, 1);
    baselineText(sample.steps, right, 103, 'roboto-thin24', COLORS.mint);
    baselineText('Steps', right, 121);
    f.fillStyle = COLORS.divider; f.fillRect(right, 128, 79, 1);
    baselineText(`${sample.battery}%`, right, 154, 'roboto-thin24', COLORS.mint);
    baselineText(sample.charging ? 'Charging' : 'Power', right, 172);
    f.fillStyle = COLORS.divider; f.fillRect(right, 179, 67, 1);
    baselineText((sample.steps * .747 / 1000).toFixed(1), right, 204, 'roboto-thin24', COLORS.mint);
    baselineText('km est.', right, 221);
  }

  function drawCase() {
    a.clearRect(0, 0, art.width, art.height);
    const gold = a.createLinearGradient(340, 0, 620, 0);
    gold.addColorStop(0, '#c59508');
    gold.addColorStop(.08, '#e7bb14');
    gold.addColorStop(.46, '#ffdc38');
    gold.addColorStop(.89, '#f1c51b');
    gold.addColorStop(1, '#bf9008');
    a.save();
    a.shadowColor = '#00000060'; a.shadowBlur = 40; a.shadowOffsetY = 15;
    roundRect(a, 342, 42, 276, 330, [42, 42, 8, 8], gold);
    roundRect(a, 342, 833, 276, 325, [8, 8, 60, 60], gold);
    a.restore();
    a.strokeStyle = '#91690640'; a.lineWidth = 2;
    for (let x of [352, 608]) {
      a.beginPath(); a.moveTo(x, 83); a.lineTo(x, 294); a.stroke();
      a.beginPath(); a.moveTo(x, 922); a.lineTo(x, 1094); a.stroke();
    }
    for (let y of [1004, 1041, 1078, 1115]) {
      roundRect(a, 454, y, 52, 11, 5.5, '#6e520a');
      roundRect(a, 455, y + 2, 50, 8, 4, '#263039');
    }
    // Use the repository's original transparent front-view CAD render.
    // Canvas lighting keeps the gray CAD material close to the physical dark case.
    a.save();
    a.filter = 'brightness(0.48) contrast(1.25)';
    a.shadowColor = '#00000050'; a.shadowBlur = 22; a.shadowOffsetY = 10;
    a.drawImage(images['watch-case-front.png'], CENTER.x - CASE.centerX * CASE.scale,
      CENTER.y - CASE.centerY * CASE.scale,
      images['watch-case-front.png'].width * CASE.scale,
      images['watch-case-front.png'].height * CASE.scale);
    a.restore();
  }

  function timeline(seconds) {
    const phase = ((seconds % LOOP) + LOOP) % LOOP;
    if (phase < HOLD_ANALOG) return { phase, face: 'analog', offset: 0 };
    if (phase < HOLD_ANALOG + SWIPE) return { phase, face: 'analog', next: 'digital', offset: -SIZE * swipeProgress((phase - HOLD_ANALOG) / SWIPE) };
    if (phase < RETURN_START) return { phase, face: 'digital', offset: 0 };
    return { phase, face: 'digital', next: 'analog', offset: SIZE * swipeProgress((phase - RETURN_START) / SWIPE) };
  }

  function swipeProgress(value) {
    // A deliberate finger-length drag followed by the watch's cubic settling.
    const t = Math.max(0, Math.min(1, value));
    const release = .75, distance = .72;
    if (t < release) return distance * (1 - Math.cos(Math.PI * t / release)) / 2;
    const rest = (t - release) / (1 - release);
    return distance + (1 - distance) * (1 - (1 - rest) ** 3);
  }

  function dateAt(seconds, mode, phase) {
    if (mode === 'live') return new Date();
    // Reset the incoming analog face while it is hidden. Its seconds hand then
    // crosses the video seam continuously, giving a clean repeating hero loop.
    const position = phase >= RETURN_START ? phase - LOOP : phase;
    return new Date(2026, 10, 7, 10, 8, 42, 0 + Math.round(position * 1000));
  }

  function composeFace(name, date, x) {
    if (name === 'analog') analog(date); else digital(date);
    s.drawImage(face, x, 0);
  }

  function renderAt(seconds, options = {}) {
    if (!images) return;
    const mode = options.mode || playbackMode;
    const state = timeline(seconds);
    const date = dateAt(seconds, mode, state.phase);
    s.fillStyle = COLORS.black;
    s.fillRect(0, 0, SIZE, SIZE);
    composeFace(state.face, date, state.offset);
    if (state.next) composeFace(state.next, date, state.offset + (state.offset <= 0 ? SIZE : -SIZE));
    ctx.clearRect(0, 0, canvas.width, canvas.height);
    const background = options.background || root.dataset.background;
    if (background !== 'transparent') {
      ctx.fillStyle = background === 'dark' ? '#0f151a' : background;
      ctx.fillRect(0, 0, canvas.width, canvas.height);
    }
    ctx.drawImage(art, 0, 0);
    const radius = CASE.radius * CASE.scale;
    ctx.save();
    ctx.beginPath(); ctx.arc(CENTER.x, CENTER.y, radius, 0, Math.PI * 2); ctx.clip();
    ctx.fillStyle = '#000'; ctx.fillRect(CENTER.x - radius, CENTER.y - radius, radius * 2, radius * 2);
    ctx.imageSmoothingEnabled = true;
    ctx.imageSmoothingQuality = 'high';
    ctx.drawImage(screen, CENTER.x - radius, CENTER.y - radius, radius * 2, radius * 2);
    ctx.restore();
    return { phase: state.phase, face: state.face, transitioning: !!state.next, time: date.toISOString() };
  }

  function buttonState() {
    const label = playing ? 'Pause animation' : 'Play animation';
    toggle.setAttribute('aria-label', label);
    toggle.title = label;
    toggle.querySelector('path').setAttribute('d', playing ? 'M7 5h4v14H7zm6 0h4v14h-4z' : 'M8 5v14l11-7z');
    status.textContent = playing ? 'Analog and digital watchfaces animate using your local time.' : 'Watch animation paused.';
  }
  function tick(now) {
    if (!playing || !visible) return;
    if (now - lastFrame >= 1000 / 30 - .5) {
      elapsed = (now - origin) / 1000;
      renderAt(elapsed);
      lastFrame = now;
    }
    request = requestAnimationFrame(tick);
  }
  function pause() {
    if (playing) elapsed = (performance.now() - origin) / 1000;
    playing = false; cancelAnimationFrame(request); buttonState();
  }
  function play() {
    if (playing) return;
    origin = performance.now() - elapsed * 1000;
    playing = true; buttonState(); request = requestAnimationFrame(tick);
  }
  toggle.addEventListener('click', () => playing ? pause() : play());
  document.addEventListener('visibilitychange', () => {
    visible = !document.hidden;
    cancelAnimationFrame(request);
    if (visible && playing) {
      origin = performance.now() - elapsed * 1000;
      request = requestAnimationFrame(tick);
    }
  });
  const observer = new IntersectionObserver(entries => {
    visible = entries[0].isIntersecting && !document.hidden;
    cancelAnimationFrame(request);
    if (visible && playing) {
      origin = performance.now() - elapsed * 1000;
      request = requestAnimationFrame(tick);
    }
  });
  observer.observe(canvas);
  reducedMotion.addEventListener('change', event => { if (event.matches) pause(); });

  const ready = (async () => {
    const bundle = window.WATCH_DEMO_ASSETS;
    metadata = bundle ? bundle.metadata : await fetch('assets/watchface-metadata.json').then(response => {
      if (!response.ok) throw new Error('Unable to load watchface metadata');
      return response.json();
    });
    const names = ['analog-dial.png', 'hour-hand.png', 'minute-hand.png', 'second-hand.png', 'shoe-icon.png',
      'font2-atlas.png', 'roboto-thin24-atlas.png', 'digital-digits.png', 'watch-case-front.png'];
    images = Object.fromEntries(await Promise.all(names.map(async name => [name, await image(bundle ? bundle.images[name] : `assets/${name}`)])));
    drawCase();
    renderAt(0);
    buttonState();
    if (!reducedMotion.matches && params.get('autoplay') !== '0') play();
    return api;
  })().catch(error => {
    status.classList.remove('sr-only');
    status.textContent = error.message;
    toggle.hidden = true;
    throw error;
  });

  const api = {
    canvas, ready, play, pause, renderAt, duration: LOOP,
    setMode(mode) { playbackMode = mode === 'fixed' ? 'fixed' : 'live'; renderAt(elapsed); },
    setSample(values = {}) {
      if (Number.isFinite(values.steps)) sample.steps = Math.max(0, Math.floor(values.steps));
      if (Number.isFinite(values.battery)) sample.battery = Math.max(0, Math.min(100, Math.round(values.battery)));
      if (typeof values.charging === 'boolean') sample.charging = values.charging;
      renderAt(elapsed);
    },
    renderFace(name, date = new Date()) {
      if (name === 'digital') digital(date); else analog(date);
      return face;
    },
    get isPlaying() { return playing; },
  };
  window.watchDemo = api;
})();
