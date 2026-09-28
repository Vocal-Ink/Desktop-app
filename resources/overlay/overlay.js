// Vocal Ink overlay page (docs/overlay-style.md): captions, chat read aloud or
// the built-in PNGtuber, styled by the profile the app pushes over a WebSocket.
// Plain JavaScript for OBS's browser source (Chromium 103); no build step and
// no requests to anything but the app.
(function () {
  'use strict';

  // ---- Constants ------------------------------------------------------------

  var PRESETS = ['subtitles', 'ink', 'bubble', 'outline', 'karaoke', 'typewriter', 'neon', 'lowerthird', 'minimal'];
  var STYLE_OPTIONS = ['style', 'position', 'align', 'tail', 'font', 'size', 'color', 'bg', 'ink', 'outline',
    'outlinecolor', 'reveal', 'wps', 'hold', 'lines', 'width', 'roll', 'name', 'indicator', 'motion'];
  var ANCHORS = ['top-left', 'top', 'top-right', 'left', 'center', 'right', 'bottom-left', 'bottom', 'bottom-right'];

  var DEFAULT_LABELS = {
    speaking: 'speaking\u2026',
    listening: 'listening\u2026',
    micLive: 'mic live',
    demo: [
      'Hey chat! Thanks so much for the follow, welcome in.',
      'Give me one second, I am going to try this jump again.',
      'Okay\u2026 that was not my finest moment. Clip it anyway!'
    ],
    badges: { broadcaster: 'Streamer', mod: 'Mod', vip: 'VIP', sub: 'Sub', founder: 'Founder' }
  };

  // The captions defaults (OverlayStyle::defaults), used until a config arrives.
  var DEFAULT_STYLE = {
    preset: 'subtitles',
    font: { family: 'Bricolage Grotesque', size: 44, weight: 700, letterSpacing: 0, lineHeight: 1.25, uppercase: false, italic: false },
    colors: { text: '#ffffff', ink: '#b48cff', unspoken: '#ffffff', unspokenOpacity: 35, background: '#120d1f',
      backgroundOpacity: 72, border: '#ffffff', borderOpacity: 12, outline: '#000000', shadow: '#000000', name: '#d9c6ff' },
    box: { padding: 18, radius: 14, borderWidth: 0, shadow: 30, maxWidth: 70, align: 'center', tail: 'none' },
    effects: { outline: 0, textShadow: 40, glow: 0 },
    position: { anchor: 'bottom', offsetX: 0, offsetY: 0, margin: 48 },
    animation: { enter: 'rise', word: 'ink', exit: 'fade', speed: 100 },
    timing: { reveal: 'audio', wps: 2.6, hold: 4 },
    history: { lines: 1, roll: false, maxLines: 3 },
    name: { show: false, position: 'above', text: '' },
    indicator: { show: false, style: 'dot', position: 'before' },
    customCss: '',
    chat: { maxMessages: 5, showBadges: true, useNameColors: true, fadeAfter: 30, direction: 'up', highlightReading: true },
    avatar: {
      images: { idle: '', talking: '', blink: '', talkingBlink: '', micLive: '' },
      threshold: 8, motion: 'bounce', motionOnlyWhileTalking: true, intensity: 60, blinkEvery: 4, flip: false,
      size: 60, shadow: 0, micLiveGlow: true, dimWhenIdle: 0, anchor: 'bottom-left'
    }
  };

  var CJK = /[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\uff66-\uff9f]/;
  var RTL = /[\u0590-\u08ff\ufb1d-\ufdff\ufe70-\ufefc]/;

  // ---- Small helpers --------------------------------------------------------

  function isObject(v) { return v !== null && typeof v === 'object' && !Array.isArray(v); }

  function merge(base, over) {
    var out = {};
    var k;
    for (k in base) if (Object.prototype.hasOwnProperty.call(base, k)) out[k] = isObject(base[k]) ? merge(base[k], {}) : base[k];
    if (!isObject(over)) return out;
    for (k in over) {
      if (!Object.prototype.hasOwnProperty.call(over, k)) continue;
      out[k] = isObject(over[k]) && isObject(out[k]) ? merge(out[k], over[k]) : (isObject(over[k]) ? merge({}, over[k]) : over[k]);
    }
    return out;
  }

  function setPath(obj, path, value) {
    var keys = path.split('.');
    var o = obj;
    for (var i = 0; i < keys.length - 1; i++) {
      if (!isObject(o[keys[i]])) o[keys[i]] = {};
      o = o[keys[i]];
    }
    o[keys[keys.length - 1]] = value;
  }

  function getPath(obj, path) {
    var keys = path.split('.');
    var o = obj;
    for (var i = 0; i < keys.length; i++) {
      if (!isObject(o)) return undefined;
      o = o[keys[i]];
    }
    return o;
  }

  function clamp(v, lo, hi) { return Math.min(hi, Math.max(lo, v)); }
  function num(v, fallback) { v = Number(v); return isFinite(v) ? v : fallback; }
  function pick(v, allowed, fallback) { return allowed.indexOf(v) >= 0 ? v : fallback; }

  function flag(v) {
    if (v === null || v === undefined) return false;
    v = String(v).trim();
    return v === '' || /^(1|true|yes|on)$/i.test(v);
  }

  function el(tag, cls, parent) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (parent) parent.appendChild(e);
    return e;
  }

  var SVGNS = 'http://www.w3.org/2000/svg';
  function svg(tag, attrs, parent) {
    var e = document.createElementNS(SVGNS, tag);
    for (var k in attrs) if (Object.prototype.hasOwnProperty.call(attrs, k)) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }

  function hexRgb(hex) {
    var m = /^#([0-9a-f]{6})$/i.exec(String(hex || ''));
    if (!m) {
      var s = /^#([0-9a-f])([0-9a-f])([0-9a-f])$/i.exec(String(hex || ''));
      if (!s) return null;
      m = [0, s[1] + s[1] + s[2] + s[2] + s[3] + s[3]];
    }
    var n = parseInt(m[1], 16);
    return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
  }

  function safeHex(hex, fallback) { return hexRgb(hex) ? String(hex).toLowerCase() : fallback; }

  function rgba(hex, alpha) {
    var c = hexRgb(hex) || [0, 0, 0];
    return 'rgba(' + c[0] + ', ' + c[1] + ', ' + c[2] + ', ' + (Math.round(clamp(alpha, 0, 1) * 1000) / 1000) + ')';
  }

  function toHex(c) {
    return '#' + c.map(function (v) { return ('0' + Math.round(clamp(v, 0, 255)).toString(16)).slice(-2); }).join('');
  }

  function mix(hex, other, t) {
    var a = hexRgb(hex) || [0, 0, 0];
    var b = hexRgb(other) || [0, 0, 0];
    return toHex([a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t]);
  }

  function luminance(hex) {
    var c = hexRgb(hex) || [0, 0, 0];
    var l = c.map(function (v) { v /= 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); });
    return 0.2126 * l[0] + 0.7152 * l[1] + 0.0722 * l[2];
  }

  function contrast(a, b) {
    var la = luminance(a);
    var lb = luminance(b);
    return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05);
  }

  var reducedMotion = !!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);

  function now() { return (window.performance && performance.now) ? performance.now() : Date.now(); }

  // ---- Page state -------------------------------------------------------------

  var html = document.documentElement;
  var root = document.getElementById('vi-root');
  var params = new URLSearchParams(window.location.search);
  var demo = params.has('demo') && flag(params.get('demo'));
  var requestedProfile = (params.get('profile') || '').trim().toLowerCase();
  if (!/^[a-z0-9-]{1,32}$/.test(requestedProfile)) requestedProfile = '';

  var state = {
    kind: 'captions',
    style: DEFAULT_STYLE,
    config: null,
    labels: DEFAULT_LABELS,
    speaking: false,
    listening: false,
    mic: false,
    talking: false,
    level: 0,
    viseme: '',
    levelAt: 0,
    captionId: null,
    captionChat: ''
  };
  var view = null;
  var customStyle = null;

  function label(key) {
    var v = state.labels[key];
    return typeof v === 'string' && v ? v : DEFAULT_LABELS[key];
  }

  function markReady() { html.classList.add('vi-ready'); }
  setTimeout(markReady, 1500);

  // ---- URL options (docs/overlay-style.md "Which wins on a page") ----------------

  function styleOptions(search) {
    var p = search instanceof URLSearchParams ? search : new URLSearchParams(String(search || '').replace(/^\?/, ''));
    var out = [];
    STYLE_OPTIONS.forEach(function (k) {
      if (p.has(k)) out.push(k + '=' + p.get(k));
    });
    return out.sort().join('&');
  }

  var urlOptions = styleOptions(params);

  // Old colour values: hex without "#", rgb()/rgba(), names, none/transparent.
  function legacyColor(value) {
    var v = String(value || '').trim();
    if (!v) return null;
    if (/^(none|transparent)$/i.test(v)) return { hex: '#000000', opacity: 0 };
    var h = /^#?([0-9a-f]{3,4}|[0-9a-f]{6}|[0-9a-f]{8})$/i.exec(v);
    if (h) {
      var d = h[1].toLowerCase();
      if (d.length <= 4) d = d.split('').map(function (c) { return c + c; }).join('');
      return { hex: '#' + d.slice(0, 6), opacity: d.length === 8 ? Math.round(parseInt(d.slice(6), 16) * 100 / 255) : 100 };
    }
    if (!(window.CSS && CSS.supports && CSS.supports('color', v))) return null;
    var probe = el('span', '', document.body);
    probe.style.color = v;
    var m = /rgba?\(([\d.]+),\s*([\d.]+),\s*([\d.]+)(?:,\s*([\d.]+))?\)/.exec(getComputedStyle(probe).color);
    document.body.removeChild(probe);
    if (!m) return null;
    return { hex: toHex([+m[1], +m[2], +m[3]]), opacity: m[4] === undefined ? 100 : Math.round(+m[4] * 100) };
  }

  // Mirrors OverlayStyle::fromQuery: the old URL options as a partial style.
  function legacyStyle(p) {
    var s = {};
    function text(k) { var v = p.get(k); return v === null ? '' : v.trim(); }
    function choice(k, allowed) { var v = text(k).toLowerCase(); return allowed.indexOf(v) >= 0 ? v : ''; }
    function number(k, path) { var v = parseFloat(text(k)); if (isFinite(v)) setPath(s, path, v); return v; }
    var style = choice('style', PRESETS.concat(['plain']));
    var position = choice('position', ['bottom', 'top', 'middle']);
    var align = choice('align', ['center', 'left', 'right']);
    var tail = choice('tail', ['left', 'center', 'right', 'none']);
    if (style === 'plain') {
      setPath(s, 'preset', 'outline');
      setPath(s, 'font.uppercase', false);
      setPath(s, 'font.letterSpacing', 0);
      setPath(s, 'font.weight', 700);
      setPath(s, 'font.size', 42);
      setPath(s, 'effects.outline', 3);
      setPath(s, 'box.maxWidth', 80);
    } else if (style) {
      setPath(s, 'preset', style);
    }
    if (position || align || style === 'bubble') {
      var v = position || 'bottom';
      var h = align || 'center';
      var anchor = v === 'middle' ? (h === 'center' ? 'center' : h) : (h === 'center' ? v : v + '-' + h);
      setPath(s, 'position.anchor', anchor);
      if (style === 'bubble') { setPath(s, 'position.offsetX', 0); setPath(s, 'position.offsetY', 0); }
    }
    if (align) setPath(s, 'box.align', align);
    if (tail) setPath(s, 'box.tail', tail);
    else if (style === 'bubble') setPath(s, 'box.tail', align || 'center');
    if (text('font')) setPath(s, 'font.family', text('font'));
    number('size', 'font.size');
    var c = legacyColor(text('color'));
    if (c && c.opacity > 0) setPath(s, 'colors.text', c.hex);
    c = legacyColor(text('bg'));
    if (c) {
      if (c.opacity > 0) setPath(s, 'colors.background', c.hex);
      setPath(s, 'colors.backgroundOpacity', c.opacity);
    }
    c = legacyColor(text('ink'));
    if (c && c.opacity > 0) setPath(s, 'colors.ink', c.hex);
    number('outline', 'effects.outline');
    c = legacyColor(text('outlinecolor'));
    if (c && c.opacity > 0) setPath(s, 'colors.outline', c.hex);
    var reveal = choice('reveal', ['word', 'instant']);
    if (reveal) setPath(s, 'timing.reveal', reveal === 'word' ? 'audio' : 'instant');
    number('wps', 'timing.wps');
    var hold = parseFloat(text('hold'));
    if (isFinite(hold)) setPath(s, 'timing.hold', hold > 60 ? -1 : hold);
    number('lines', 'history.maxLines');
    number('width', 'box.maxWidth');
    if (p.has('roll')) {
      var roll = flag(p.get('roll'));
      setPath(s, 'history.roll', roll);
      setPath(s, 'history.lines', roll ? 2 : 1);
    }
    if (p.has('name')) setPath(s, 'name.show', flag(p.get('name')));
    if (p.has('indicator')) setPath(s, 'indicator.show', flag(p.get('indicator')));
    if (p.has('motion') && !flag(p.get('motion'))) {
      setPath(s, 'animation.enter', 'fade');
      setPath(s, 'animation.word', 'fade');
      setPath(s, 'animation.exit', 'fade');
    }
    return s;
  }

  // The same ranges as OverlayStyle for what URL options can touch.
  var URL_RANGES = {
    'font.size': [12, 160, true], 'font.weight': [300, 900, true], 'font.letterSpacing': [-5, 30],
    'effects.outline': [0, 16, true], 'box.maxWidth': [20, 100, true], 'timing.wps': [1, 8], 'timing.hold': [-1, 60],
    'history.maxLines': [1, 10, true], 'history.lines': [1, 6, true], 'colors.backgroundOpacity': [0, 100, true],
    'position.offsetX': [-50, 50], 'position.offsetY': [-50, 50]
  };

  function withUrlOptions(style, presets, legacyQuery) {
    if (!urlOptions || urlOptions === styleOptions(legacyQuery)) return style; // follows the profile
    var partial = legacyStyle(params);
    var out = style;
    if (partial.preset && presets && isObject(presets[partial.preset])) out = merge(out, presets[partial.preset]);
    out = merge(out, partial);
    Object.keys(URL_RANGES).forEach(function (path) {
      var v = getPath(partial, path);
      if (v === undefined) return;
      var r = URL_RANGES[path];
      v = clamp(num(v, getPath(style, path)), r[0], r[1]);
      if (path === 'timing.hold' && v < 0) v = -1;
      setPath(out, path, r[2] ? Math.round(v) : v);
    });
    var family = String(out.font.family || '').replace(/["'\\;{}<>()\/:@!*]/g, '').trim();
    out.font.family = family || style.font.family;
    return out;
  }

  // ---- Fonts --------------------------------------------------------------------

  var fontLoads = {};

  function familyList(family) {
    return String(family || '').split(',').map(function (f) { return f.trim(); }).filter(Boolean);
  }

  function cssFamily(family) {
    var list = familyList(family).map(function (f) { return '"' + f.replace(/["\\]/g, '') + '"'; });
    list.push('var(--vi-fallback)');
    return list.join(', ');
  }

  // Loads the bundled faces the style uses (FontFace API: no inline CSS needed).
  function loadFonts(style, fonts) {
    if (!window.FontFace || !document.fonts || !Array.isArray(fonts)) return Promise.resolve();
    var wanted = familyList(style.font.family).map(function (f) { return f.toLowerCase(); });
    var loads = [];
    fonts.forEach(function (f) {
      if (!f || typeof f.url !== 'string' || !/^\/fonts\/[A-Za-z0-9._-]+$/.test(f.url)) return;
      if (wanted.indexOf(String(f.family).toLowerCase()) < 0) return;
      if (!fontLoads[f.url]) {
        var face = new FontFace(String(f.family), 'url("' + f.url + '")', {
          weight: String(num(f.weight, 400)),
          style: f.style === 'italic' ? 'italic' : 'normal'
        });
        document.fonts.add(face);
        fontLoads[f.url] = face.load().catch(function () {});
      }
      loads.push(fontLoads[f.url]);
    });
    return Promise.all(loads);
  }

  // ---- Applying a style ----------------------------------------------------------

  function wordAnimation(style) {
    var w = style.animation.word;
    if (reducedMotion && ['rise', 'pop', 'bounce', 'glow'].indexOf(w) >= 0) return 'fade';
    return w;
  }

  function motionType(type) {
    if (!reducedMotion || type === 'none') return type;
    return 'fade';
  }

  function applyStyle(style) {
    var s = style;
    var r = root.style;
    var c = s.colors;
    var textShadow = s.effects.textShadow / 100;
    var glow = s.effects.glow / 100;
    var shadowHex = safeHex(c.shadow, '#000000');
    var inkHex = safeHex(c.ink, '#b48cff');
    var noBox = c.backgroundOpacity === 0 && (s.box.borderWidth === 0 || c.borderOpacity === 0);
    var boxShadow = s.box.shadow / 100;

    r.setProperty('--vi-font', cssFamily(s.font.family));
    r.setProperty('--vi-size', s.font.size + 'px');
    r.setProperty('--vi-weight', String(s.font.weight));
    r.setProperty('--vi-ls', (s.font.letterSpacing / 100) + 'em');
    r.setProperty('--vi-lh', String(s.font.lineHeight));
    r.setProperty('--vi-transform', s.font.uppercase ? 'uppercase' : 'none');
    r.setProperty('--vi-font-style', s.font.italic ? 'italic' : 'normal');
    r.setProperty('--vi-text', safeHex(c.text, '#ffffff'));
    r.setProperty('--vi-ink', inkHex);
    r.setProperty('--vi-unspoken', rgba(c.unspoken, c.unspokenOpacity / 100));
    r.setProperty('--vi-bg', rgba(c.background, c.backgroundOpacity / 100));
    r.setProperty('--vi-border', rgba(c.border, c.borderOpacity / 100));
    r.setProperty('--vi-border-w', s.box.borderWidth + 'px');
    r.setProperty('--vi-outline-color', safeHex(c.outline, '#000000'));
    r.setProperty('--vi-name', safeHex(c.name, '#d9c6ff'));
    r.setProperty('--vi-pad', s.box.padding + 'px');
    r.setProperty('--vi-radius', s.box.radius + 'px');
    r.setProperty('--vi-box-shadow', noBox || boxShadow === 0 ? 'none'
      : '0 0.12em 0.9em ' + rgba(shadowHex, 0.55 * boxShadow) + ', 0 0.02em 0.12em ' + rgba(shadowHex, 0.25 * boxShadow));
    r.setProperty('--vi-box-drop', boxShadow === 0 ? 'none'
      : 'drop-shadow(0 0.1em 0.35em ' + rgba(shadowHex, 0.5 * boxShadow) + ')');
    r.setProperty('--vi-maxw', s.box.maxWidth + 'vw');
    r.setProperty('--vi-align', s.box.align);
    r.setProperty('--vi-outline', s.effects.outline + 'px');
    r.setProperty('--vi-text-shadow', textShadow === 0 ? 'none'
      : '0 0.04em 0.1em ' + rgba(shadowHex, 0.9 * textShadow) + ', 0 0.03em 0.45em ' + rgba(shadowHex, 0.65 * textShadow));
    // Neon: a tight bright halo in the ink colour, then wider and softer ones.
    r.setProperty('--vi-glow', glow === 0 ? 'none'
      : '0 0 0.03em ' + rgba(mix(inkHex, '#ffffff', 0.5), glow) + ', 0 0 0.08em ' + rgba(inkHex, glow) + ', 0 0 0.2em '
        + rgba(inkHex, glow) + ', 0 0 0.45em ' + rgba(inkHex, glow) + ', 0 0 0.9em ' + rgba(inkHex, 0.75 * glow)
        + ', 0 0 1.7em ' + rgba(inkHex, 0.55 * glow));
    r.setProperty('--vi-margin', s.position.margin + 'px');
    r.setProperty('--vi-dx', s.position.offsetX + 'vw');
    r.setProperty('--vi-dy', s.position.offsetY + 'vh');
    r.setProperty('--vi-dur', String(Math.round(10000 / clamp(s.animation.speed, 25, 300)) / 100));

    var anchor = state.kind === 'avatar' ? s.avatar.anchor : s.position.anchor;
    var cls = ['vi-kind-' + state.kind, 'vi-preset-' + s.preset, 'vi-anchor-' + pick(anchor, ANCHORS, 'bottom'),
      'vi-align-' + s.box.align, 'vi-np-' + s.name.position, 'vi-ind-' + s.indicator.style,
      'vi-ind-pos-' + s.indicator.position];
    if (s.box.tail !== 'none') cls.push('vi-tail-' + s.box.tail);
    if (state.kind === 'captions') cls.push('vi-wa-' + wordAnimation(s));
    if (s.name.show || state.kind === 'chat') cls.push('vi-show-name');
    if (s.indicator.show) cls.push('vi-show-ind');
    if (s.effects.outline > 0) cls.push('vi-has-outline');
    if (glow > 0) cls.push('vi-has-glow');
    if (noBox) cls.push('vi-no-box');
    if (s.history.roll) cls.push('vi-roll');
    if (reducedMotion) cls.push('vi-reduced');
    root.className = cls.join(' ');

    // Custom CSS goes after the page's own (already sanitized by the app).
    var css = typeof s.customCss === 'string' ? s.customCss.slice(0, 8000).replace(/<\//g, '') : '';
    if (css && !customStyle) {
      customStyle = document.createElement('style');
      document.head.appendChild(customStyle);
    }
    if (customStyle) customStyle.textContent = css;
  }

  // Box enter/exit animations (Web Animations API).
  function enterFrames(type, anchor) {
    var side = /right$/.test(anchor) ? 1 : -1;
    switch (type) {
      case 'fade': return [{ opacity: 0 }, { opacity: 1 }];
      case 'rise': return [{ opacity: 0, transform: 'translateY(0.45em)' }, { opacity: 1, transform: 'none' }];
      case 'pop': return [{ opacity: 0, transform: 'scale(0.8)' }, { opacity: 1, transform: 'scale(1.03)', offset: 0.6 },
        { opacity: 1, transform: 'none' }];
      case 'slide': return [
        { opacity: 0, transform: 'translateX(' + (side * 1.4) + 'em)', clipPath: side < 0 ? 'inset(0 100% 0 0)' : 'inset(0 0 0 100%)' },
        { opacity: 1, transform: 'none', clipPath: 'inset(0 0 0 0)' }];
      default: return null;
    }
  }

  function exitFrames(type) {
    switch (type) {
      case 'fade': return [{ opacity: 1 }, { opacity: 0 }];
      case 'sink': return [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'translateY(0.5em)' }];
      case 'shrink': return [{ opacity: 1, transform: 'none' }, { opacity: 0, transform: 'scale(0.86)' }];
      default: return null;
    }
  }

  function animate(elem, frames, ms, easing, fill) {
    if (!frames || !elem.animate) return null;
    return elem.animate(frames, { duration: ms * num(root.style.getPropertyValue('--vi-dur'), 1), easing: easing, fill: fill || 'none' });
  }

  function originFor(style, anchor) {
    var tail = style.box.tail;
    if (tail === 'left') return '12% 115%';
    if (tail === 'right') return '88% 115%';
    if (tail === 'center') return '50% 115%';
    var v = /^top/.test(anchor) ? '0%' : (/^bottom/.test(anchor) ? '100%' : '50%');
    var h = /left$/.test(anchor) ? '0%' : (/right$/.test(anchor) ? '100%' : '50%');
    return h + ' ' + v;
  }

  // ---- Text: words, reveal timing ---------------------------------------------------

  // How long a word takes to say, relative to others, and the pause after it.
  function wordTiming(word) {
    if (CJK.test(word) && word.length === 1) return { say: 0.42, pause: /[。！？]/.test(word) ? 0.8 : 0 };
    var letters = word.replace(/[^\p{L}\p{N}]/gu, '').length || word.length;
    var say = Math.min(1.8, Math.max(0.5, 0.45 + letters * 0.1));
    var pause = 0;
    if (/[.!?\u2026]["'\u201d\u2019)\]]*$/.test(word)) pause = 0.9;
    else if (/[,;:\u2013\u2014]["'\u201d\u2019)\]]*$/.test(word)) pause = 0.35;
    return { say: say, pause: pause };
  }

  // Builds word (or letter) spans for `text` into `parent`. Each unit gets
  // data-t (for the outline copy and the ink layer) and a start/end fraction.
  function buildText(parent, text, letters) {
    var tokens = String(text).split(/\s+/).filter(function (t) { return t !== ''; });
    var units = [];
    var timings = [];
    var words = [];
    // The last two words stay together, so no line holds a single leftover word.
    var glue = tokens.length >= 4 && !CJK.test(tokens[tokens.length - 1]) ? tokens.length - 2 : -1;
    var into = parent;
    tokens.forEach(function (token, i) {
      if (i > 0) into.appendChild(document.createTextNode(' '));
      if (i === glue) into = el('span', 'vi-keep', parent);
      var pieces = CJK.test(token) && token.length > 1 ? Array.from(token) : [token];
      pieces.forEach(function (piece) {
        var w = el('span', 'vi-u vi-word', into);
        w.setAttribute('data-t', piece);
        var t = wordTiming(piece);
        words.push(w);
        if (letters) {
          w.className = 'vi-word vi-split';
          var chars = Array.from(piece);
          chars.forEach(function (ch, j) {
            var c = el('span', 'vi-u vi-ch', w);
            c.setAttribute('data-t', ch);
            c.textContent = ch;
            units.push(c);
            timings.push({ say: t.say / chars.length, pause: j === chars.length - 1 ? t.pause + 0.08 : 0 });
          });
        } else {
          w.textContent = piece;
          units.push(w);
          timings.push(t);
        }
      });
    });
    var total = 0;
    timings.forEach(function (t) { total += t.say + t.pause; });
    total = total || 1;
    var at = 0;
    var starts = [];
    var ends = [];
    timings.forEach(function (t) {
      starts.push(at / total);
      ends.push((at + t.say) / total);
      at += t.say + t.pause;
    });
    return { units: units, words: words, starts: starts, ends: ends, weight: total, rtl: RTL.test(text) };
  }

  // Reveals units as the voice goes: from the real playback position ("audio"),
  // or an estimate from words per second until the first progress arrives.
  function Reader(built, opts) {
    this.units = built.units;
    this.starts = built.starts;
    this.ends = built.ends;
    this.revealed = 0;
    this.startAt = now();
    this.estMs = Math.max(600, built.weight / Math.max(1, opts.wps) * 1000);
    this.mode = opts.reveal;
    this.f = 0;
    this.fAt = 0;
    this.total = 0;
    this.hasProgress = false;
    this.ended = false;
    this.onReveal = opts.onReveal || function () {};
    this.wet = null;
  }

  Reader.prototype.progress = function (f, total) {
    f = clamp(num(f, 0), 0, 1);
    if (f < this.f && this.hasProgress) return; // never go backwards
    this.f = f;
    this.fAt = now();
    this.total = Math.max(0, num(total, 0));
    this.hasProgress = true;
    wake();
  };

  Reader.prototype.target = function (t) {
    if (this.ended || this.mode === 'instant') return 1;
    if (this.mode === 'audio' && this.hasProgress) {
      var span = this.total > 0 ? this.total : this.estMs;
      return Math.min(1, this.f + Math.min(t - this.fAt, 350) / span);
    }
    return Math.min(1, (t - this.startAt) / this.estMs);
  };

  Reader.prototype.durationMs = function () {
    if (this.mode === 'audio' && this.hasProgress && this.total > 0) return this.total;
    return this.estMs;
  };

  Reader.prototype.step = function (t) {
    var f = this.target(t);
    var changed = false;
    var lead = this.mode === 'instant' || this.ended ? 1 : 0.004;
    while (this.revealed < this.units.length && this.starts[this.revealed] <= f + lead) {
      this.reveal(this.revealed, this.ended || this.mode === 'instant');
      this.revealed++;
      changed = true;
    }
    if (changed) this.onReveal(this);
    return this.revealed < this.units.length;
  };

  Reader.prototype.reveal = function (i, quick) {
    var u = this.units[i];
    var ms = quick ? 90 : clamp((this.ends[i] - this.starts[i]) * this.durationMs(), 70, 900);
    u.style.setProperty('--vi-fill', Math.round(ms) + 'ms');
    if (this.wet && this.wet !== u) {
      this.wet.classList.remove('wet');
      this.wet.classList.add('dry');
    }
    u.classList.add('spoken');
    u.classList.add('wet');
    this.wet = u;
  };

  Reader.prototype.finish = function () {
    this.ended = true;
    this.step(now());
    var self = this;
    var wet = this.wet;
    setTimeout(function () {
      if (wet && self.wet === wet) {
        wet.classList.remove('wet');
        wet.classList.add('dry');
      }
    }, 650);
  };

  var readers = [];
  var ticking = false;
  var frameHooks = []; // run every frame while something moves (wave indicator, avatar)

  function wake() {
    if (ticking) return;
    ticking = true;
    requestAnimationFrame(tick);
  }

  function tick() {
    var t = now();
    readers = readers.filter(function (r) { return r.step(t); });
    var busy = readers.length > 0;
    frameHooks.forEach(function (hook) { if (hook(t)) busy = true; });
    if (busy) requestAnimationFrame(tick);
    else ticking = false;
  }

  function startReader(reader) {
    readers.push(reader);
    wake();
  }

  function stopReader(reader) {
    readers = readers.filter(function (r) { return r !== reader; });
  }

  // ---- Indicator (speaking / listening / mic live) ------------------------------------

  var MIC_PATH = 'M12 14.5a3.5 3.5 0 0 0 3.5-3.5V5a3.5 3.5 0 0 0-7 0v6a3.5 3.5 0 0 0 3.5 3.5zm6-3.5a6 6 0 0 1-12 0H4a8 8 0 0 0 7 7.94V21h2v-2.06A8 8 0 0 0 20 11h-2z';

  function Indicator(parent) {
    this.el = el('span', 'vi-ind', parent);
    var icon = el('span', 'vi-ind-icon', this.el);
    el('span', 'vi-dot', icon);
    var bars = el('span', 'vi-bars', icon);
    for (var i = 0; i < 3; i++) el('i', '', bars);
    this.wave = el('span', 'vi-wave', icon);
    this.waveBars = [];
    for (var j = 0; j < 5; j++) this.waveBars.push(el('i', '', this.wave));
    var mic = svg('svg', { 'class': 'vi-mic', viewBox: '0 0 24 24', 'aria-hidden': 'true' }, icon);
    svg('path', { d: MIC_PATH }, mic);
    this.label = el('span', 'vi-ind-label', this.el);
    this.shown = 0;
    var self = this;
    this.hook = function (t) { return self.frame(t); };
    frameHooks.push(this.hook);
  }

  Indicator.prototype.destroy = function () {
    var hook = this.hook;
    frameHooks = frameHooks.filter(function (h) { return h !== hook; });
  };

  Indicator.prototype.frame = function (t) {
    if (!state.speaking || state.style.indicator.style !== 'wave') {
      if (this.shown > 0.001) {
        this.shown = 0;
        this.waveBars.forEach(function (b) { b.style.transform = ''; });
      }
      return false;
    }
    var target = t - state.levelAt > 600 ? 0 : state.level;
    this.shown += (target - this.shown) * (target > this.shown ? 0.55 : 0.2);
    var shape = [0.55, 0.85, 1, 0.8, 0.5];
    var s = this.shown;
    this.waveBars.forEach(function (b, i) {
      var wobble = 0.85 + 0.15 * Math.sin(t / 90 + i * 1.7);
      b.style.transform = 'scaleY(' + clamp(0.3 + s * shape[i] * wobble, 0.3, 1).toFixed(3) + ')';
    });
    return s > 0.01;
  };

  // Returns whether anything is signalled; sets the box state classes.
  Indicator.prototype.update = function (box) {
    var show = state.style.indicator.show;
    var speaking = show && state.speaking;
    var listening = show && state.listening && !state.speaking;
    var mic = show && state.mic && !state.speaking && !state.listening;
    box.classList.toggle('vi-signal', speaking || listening || mic);
    box.classList.toggle('vi-state-speaking', speaking);
    box.classList.toggle('vi-state-listening', listening);
    box.classList.toggle('vi-state-mic', mic);
    this.label.textContent = speaking ? label('speaking') : listening ? label('listening') : mic ? label('micLive') : '';
    if (speaking) wake();
    return speaking || listening || mic;
  };

  // ---- Captions ----------------------------------------------------------------------

  function CaptionsView() {
    this.stage = el('div', 'vi-stage', root);
    this.shift = el('div', 'vi-shift', this.stage);
    this.box = el('div', 'vi-box vi-empty', this.shift);
    this.indicator = new Indicator(this.box);
    this.content = el('div', 'vi-content', this.box);
    this.nameAbove = el('div', 'vi-name vi-name-above', this.content);
    this.linesEl = el('div', 'vi-lines', this.content);
    this.nameBelow = el('div', 'vi-name vi-name-below', this.content);
    this.lines = [];
    this.current = null;
    this.visible = false;
    this.exitAnim = null;
  }

  CaptionsView.prototype.destroy = function () {
    this.lines.forEach(function (l) { clearTimeout(l.expire); stopReader(l.reader); });
    this.indicator.destroy();
    root.removeChild(this.stage);
  };

  CaptionsView.prototype.lineHeight = function () {
    var px = parseFloat(getComputedStyle(this.box).lineHeight);
    return px > 0 ? px : state.style.font.size * state.style.font.lineHeight;
  };

  CaptionsView.prototype.setStyle = function () {
    var self = this;
    this.lines.forEach(function (l) { self.layout(l, true); });
    this.render();
  };

  CaptionsView.prototype.relayout = function () { this.setStyle(); };

  CaptionsView.prototype.add = function (msg) {
    var style = state.style;
    if (this.current && !this.current.reader.ended) this.finish(this.current);
    var max = style.history.lines;
    while (this.lines.length >= max) this.remove(this.lines[0], style.history.roll && this.visible);

    var line = { id: msg.id, voice: String(msg.voice || ''), text: String(msg.text || '') };
    line.el = el('div', 'vi-line vi-current', this.linesEl);
    line.viewport = el('div', 'vi-viewport', line.el);
    line.textEl = el('div', 'vi-text', line.viewport);
    if (style.name.position === 'inline') {
      var inlineName = el('span', 'vi-name vi-name-inline', line.textEl);
      inlineName.textContent = style.name.text || line.voice;
    }
    var letters = wordAnimation(style) === 'type';
    var built = buildText(line.textEl, line.text, letters);
    line.el.classList.toggle('vi-rtl', built.rtl);
    if (built.rtl) line.el.setAttribute('dir', 'rtl');
    if (letters) {
      line.caret = el('span', 'vi-caret', null);
      if (built.units.length) built.units[0].parentNode.insertBefore(line.caret, built.units[0]);
      else line.textEl.appendChild(line.caret);
    }
    var self = this;
    line.reader = new Reader(built, {
      wps: style.timing.wps,
      reveal: style.timing.reveal,
      onReveal: function (reader) {
        if (line.caret && reader.revealed > 0) {
          var last = reader.units[reader.revealed - 1];
          last.parentNode.insertBefore(line.caret, last.nextSibling);
        }
        self.layout(line, false);
      }
    });
    line.lineHeight = 0;
    this.current = line;
    this.lines.push(line);
    this.ages();

    var appearing = !this.visible;
    this.layout(line, true);
    if (!appearing && this.linesEl.children.length > 1) {
      // Grow from nothing inside a box that is already there.
      var target = line.viewport.style.height;
      line.viewport.style.transition = 'none';
      line.viewport.style.height = '0px';
      void line.viewport.offsetHeight;
      line.viewport.style.transition = '';
      line.viewport.style.height = target;
    }
    this.render(appearing);
    if (style.timing.reveal === 'instant') line.reader.finish();
    else startReader(line.reader);
  };

  CaptionsView.prototype.ages = function () {
    var n = this.lines.length;
    this.lines.forEach(function (l, i) {
      var age = n - 1 - i;
      l.el.classList.remove('vi-age-1', 'vi-age-2', 'vi-age-3');
      if (age > 0) l.el.classList.add('vi-age-' + Math.min(age, 3));
      l.el.classList.toggle('vi-current', age === 0);
    });
  };

  // The window of history.maxLines lines follows the word being spoken.
  CaptionsView.prototype.layout = function (line, instant) {
    var style = state.style;
    var lh = this.lineHeight();
    var maxLines = style.history.maxLines;
    var total = Math.max(1, Math.round(line.textEl.offsetHeight / lh));
    var reader = line.reader;
    var focusUnit = reader.revealed > 0 ? reader.units[reader.revealed - 1] : reader.units[0];
    var focusWord = focusUnit && focusUnit.classList.contains('vi-ch') ? focusUnit.parentNode : focusUnit;
    var focus = focusWord ? Math.round(focusWord.offsetTop / lh) : 0;
    var allVisible = wordAnimation(style) === 'ink';
    var visible;
    var first;
    if (allVisible) {
      visible = Math.min(total, maxLines);
      first = clamp(focus - (maxLines >= 3 ? 1 : 0), 0, total - visible);
    } else {
      var revealedLines = reader.revealed > 0 ? focus + 1 : 1;
      visible = Math.min(maxLines, revealedLines);
      first = revealedLines - visible;
    }
    // Lines outside the window fade out instead of being clipped, so outlines
    // and glows are never cut.
    var scrolling = total > maxLines;
    line.el.classList.toggle('vi-scrolling', scrolling);
    if (scrolling || line.scrolled) {
      line.scrolled = scrolling;
      var items = line.textEl.querySelectorAll('.vi-word, .vi-name-inline');
      for (var i = 0; i < items.length; i++) {
        var row = Math.round(items[i].offsetTop / lh);
        items[i].classList.toggle('vi-off', scrolling && (row < first || row >= first + visible));
      }
    }
    if (instant) line.viewport.style.transition = 'none';
    line.viewport.style.height = (visible * lh) + 'px';
    line.textEl.style.transition = instant ? 'none' : '';
    line.textEl.style.transform = first > 0 ? 'translateY(' + (-first * lh) + 'px)' : '';
    if (instant) {
      void line.viewport.offsetHeight;
      line.viewport.style.transition = '';
      line.textEl.style.transition = '';
    }
  };

  CaptionsView.prototype.finish = function (line) {
    stopReader(line.reader);
    line.reader.finish();
    line.el.classList.add('vi-done');
    this.layout(line, false);
  };

  CaptionsView.prototype.end = function (id) {
    var line = null;
    this.lines.forEach(function (l) { if (l.id === id) line = l; });
    if (!line) return;
    this.finish(line);
    var hold = state.style.timing.hold;
    if (hold < 0) return; // until the next caption
    var self = this;
    clearTimeout(line.expire);
    line.expire = setTimeout(function () { self.remove(line, true); }, hold * 1000);
  };

  CaptionsView.prototype.remove = function (line, animated) {
    var i = this.lines.indexOf(line);
    if (i < 0) return;
    clearTimeout(line.expire);
    stopReader(line.reader);
    this.lines.splice(i, 1);
    if (this.current === line) this.current = null;
    this.ages();
    if (!this.lines.length) {
      // The last one leaves with the box (unless the indicator keeps it up).
      if (this.render()) {
        if (line.el.parentNode) line.el.parentNode.removeChild(line.el);
      }
      else this.pendingRemoval = line;
      return;
    }
    if (!animated || reducedMotion) {
      if (line.el.parentNode) line.el.parentNode.removeChild(line.el);
      return;
    }
    var h = line.el.offsetHeight;
    var a = animate(line.el, [
      { height: h + 'px', opacity: getComputedStyle(line.el).opacity, transform: 'none' },
      { height: '0px', opacity: 0, transform: 'translateY(-0.35em)' }], 380, 'cubic-bezier(0.2, 0.8, 0.2, 1)', 'forwards');
    line.el.style.overflow = 'hidden';
    var done = function () { if (line.el.parentNode) line.el.parentNode.removeChild(line.el); };
    if (a) a.onfinish = done; else done();
  };

  CaptionsView.prototype.clear = function () {
    this.lines.forEach(function (l) { clearTimeout(l.expire); stopReader(l.reader); });
    this.lines = [];
    this.current = null;
    // If the box stays up for the indicator, the words go at once; otherwise
    // they leave with the box.
    if (this.render()) this.linesEl.textContent = '';
  };

  // Shows or hides the box; returns whether it stays visible.
  CaptionsView.prototype.render = function () {
    var style = state.style;
    var box = this.box;
    var signal = this.indicator.update(box);
    var hasLines = this.lines.length > 0;
    var show = hasLines || signal;
    // While the box leaves, it keeps showing what it showed.
    if (show || !this.visible) box.classList.toggle('vi-empty', !hasLines);
    var newest = this.lines[this.lines.length - 1];
    var name = style.name.text || (newest ? newest.voice : '');
    if (show) {
      this.nameAbove.textContent = name;
      this.nameBelow.textContent = name;
    }
    var anchor = style.position.anchor;
    var self = this;
    if (show) {
      if (this.exitAnim) {
        this.exitAnim.cancel();
        this.exitAnim = null;
        if (this.pendingRemoval && this.pendingRemoval.el.parentNode) this.pendingRemoval.el.parentNode.removeChild(this.pendingRemoval.el);
        this.pendingRemoval = null;
      }
      if (!this.visible) {
        this.visible = true;
        box.classList.add('vi-shown');
        box.style.transformOrigin = originFor(style, anchor);
        animate(box, enterFrames(motionType(style.animation.enter), anchor), 420,
          style.animation.enter === 'pop' ? 'cubic-bezier(0.3, 1.2, 0.5, 1)' : 'cubic-bezier(0.2, 0.8, 0.2, 1)');
      }
      return true;
    }
    if (this.visible && !this.exitAnim) {
      box.style.transformOrigin = originFor(style, anchor);
      var finish = function () {
        self.exitAnim = null;
        self.visible = false;
        box.classList.remove('vi-shown');
        box.classList.toggle('vi-empty', !self.lines.length);
        if (self.pendingRemoval && self.pendingRemoval.el.parentNode) self.pendingRemoval.el.parentNode.removeChild(self.pendingRemoval.el);
        self.pendingRemoval = null;
        if (!self.lines.length) self.linesEl.textContent = '';
        if (a) a.cancel();
      };
      var a = animate(box, exitFrames(motionType(style.animation.exit)), 380, 'ease-in', 'forwards');
      this.exitAnim = a || { cancel: function () {} };
      if (a) a.onfinish = finish; else finish();
    }
    return false;
  };

  CaptionsView.prototype.handle = function (msg) {
    switch (msg.type) {
      case 'caption':
        state.captionId = msg.id;
        this.add(msg);
        break;
      case 'progress':
        this.lines.forEach(function (l) { if (l.id === msg.id) l.reader.progress(msg.f, msg.total); });
        break;
      case 'end':
        this.end(msg.id);
        break;
      case 'clear':
        this.clear();
        break;
      case 'speaking':
      case 'listening':
      case 'mic':
        this.render();
        break;
    }
  };

  // ---- Chat ----------------------------------------------------------------------------

  function badgeText(badge) {
    var names = isObject(state.labels.badges) ? state.labels.badges : DEFAULT_LABELS.badges;
    var t = names[badge] || DEFAULT_LABELS.badges[badge] || badge;
    return String(t).slice(0, 16);
  }

  // A chatter's colour, made readable on the message box.
  function chatterColor(color) {
    var style = state.style;
    if (!style.chat.useNameColors || !/^#[0-9a-f]{6}$/i.test(String(color || ''))) return style.colors.name;
    var bg = style.colors.backgroundOpacity >= 35 ? style.colors.background : '#101010';
    var towards = luminance(bg) > 0.4 ? '#000000' : '#ffffff';
    var c = String(color).toLowerCase();
    for (var i = 0; i < 6 && contrast(c, bg) < 3.2; i++) c = mix(c, towards, 0.25);
    return c;
  }

  function ChatView() {
    this.stage = el('div', 'vi-stage', root);
    this.shift = el('div', 'vi-shift', this.stage);
    this.list = el('div', 'vi-chat', this.shift);
    this.items = [];
    this.reading = null; // { item, captionId, reader }
  }

  ChatView.prototype.destroy = function () {
    this.items.forEach(function (it) { clearTimeout(it.fade); });
    if (this.reading) stopReader(this.reading.reader);
    root.removeChild(this.stage);
  };

  // The look is edited live: existing messages are rebuilt with the new layout.
  ChatView.prototype.setStyle = function () {
    var self = this;
    while (this.items.length > state.style.chat.maxMessages) this.remove(this.items[0], false);
    this.items.forEach(function (it) {
      self.renderItem(it);
      if (self.reading && self.reading.item === it) {
        var old = self.reading.reader;
        stopReader(old);
        var reader = new Reader(it.built, { wps: state.style.timing.wps, reveal: 'audio' });
        if (old.hasProgress) reader.progress(old.f, old.total);
        self.reading.reader = reader;
        startReader(reader);
      }
    });
    this.order();
  };

  ChatView.prototype.order = function () {
    var down = state.style.chat.direction === 'down';
    var list = this.list;
    var items = down ? this.items.slice().reverse() : this.items;
    items.forEach(function (it) { list.appendChild(it.slot); });
  };

  ChatView.prototype.relayout = function () {};

  // (Re)builds the message box from item.msg. Chat is untrusted: text only.
  ChatView.prototype.renderItem = function (item) {
    var style = state.style;
    var msg = item.msg;
    var box = item.box;
    box.textContent = '';
    box.classList.toggle('vi-chat-action', msg.action);
    var head = el('div', 'vi-chat-head', null);
    var badges = el('span', 'vi-badges', head);
    msg.badges.forEach(function (b) {
      var pill = el('span', 'vi-badge vi-badge-' + b.replace(/[^a-z0-9_-]/g, ''), badges);
      pill.textContent = badgeText(b);
    });
    if (!style.chat.showBadges || !msg.badges.length) badges.style.display = 'none';
    var name = el('span', 'vi-chat-name', head);
    name.textContent = msg.name;
    var color = chatterColor(msg.color);
    name.style.color = color;
    box.style.setProperty('--vi-chatter', color);

    var text = el('div', 'vi-text', null);
    if (style.name.position === 'inline') text.appendChild(head);
    item.built = buildText(text, msg.text, false);
    if (item.built.rtl) box.setAttribute('dir', 'rtl');
    if (style.name.position === 'above') box.appendChild(head);
    box.appendChild(text);
    if (style.name.position === 'below') box.appendChild(head);
    box.style.transformOrigin = originFor(style, style.position.anchor);
  };

  ChatView.prototype.add = function (msg) {
    var style = state.style;
    var fadeMs = style.chat.fadeAfter * 1000;
    var age = Math.max(0, num(msg.age, 0));
    if (fadeMs > 0 && age >= fadeMs) return; // replayed, but already gone
    if (this.find(msg.id)) return;

    var badges = Array.isArray(msg.badges) ? msg.badges.filter(function (b) { return typeof b === 'string'; }).slice(0, 6) : [];
    var item = { msg: { id: String(msg.id || ''), login: String(msg.login || ''), name: String(msg.name || msg.login || ''),
      color: msg.color, text: String(msg.text || ''), badges: badges, action: !!msg.action } };
    item.slot = el('div', 'vi-chat-slot', null);
    item.box = el('div', 'vi-box vi-chat-message', item.slot);
    this.renderItem(item);

    if (style.chat.direction === 'down') this.list.insertBefore(item.slot, this.list.firstChild);
    else this.list.appendChild(item.slot);
    this.items.push(item);
    var anchor = style.position.anchor;
    item.box.style.transformOrigin = originFor(style, anchor);
    if (!age) animate(item.box, enterFrames(motionType(style.animation.enter), anchor), 380, 'cubic-bezier(0.2, 0.8, 0.2, 1)');

    while (this.items.length > style.chat.maxMessages) this.remove(this.items[0], true);
    var self = this;
    if (fadeMs > 0) item.fade = setTimeout(function () { self.remove(item, true); }, fadeMs - age);

    if (msg.captionId !== undefined && msg.captionId === state.captionId) this.read(item, msg.captionId);
    else if (state.captionChat && state.captionChat === item.msg.id) this.read(item, state.captionId);
  };

  ChatView.prototype.find = function (id) {
    for (var i = 0; i < this.items.length; i++) if (this.items[i].msg.id === String(id)) return this.items[i];
    return null;
  };

  ChatView.prototype.remove = function (item, animated) {
    var i = this.items.indexOf(item);
    if (i < 0) return;
    this.items.splice(i, 1);
    clearTimeout(item.fade);
    if (this.reading && this.reading.item === item) {
      stopReader(this.reading.reader);
      this.reading = null;
    }
    var slot = item.slot;
    var gone = function () { if (slot.parentNode) slot.parentNode.removeChild(slot); };
    if (!animated) return gone();
    var h = slot.offsetHeight;
    var out = animate(item.box, exitFrames(motionType(state.style.animation.exit) === 'none' ? 'fade' : motionType(state.style.animation.exit)),
      320, 'ease-in', 'forwards');
    var collapse = animate(slot, [{ height: h + 'px', marginTop: '0px' }, { height: '0px', marginTop: '-0.35em' }], 320,
      'cubic-bezier(0.2, 0.8, 0.2, 1)', 'forwards');
    slot.style.overflow = 'hidden';
    var a = collapse || out;
    if (a) a.onfinish = gone; else gone();
  };

  ChatView.prototype.read = function (item, captionId) {
    if (!state.style.chat.highlightReading) return;
    this.stopReading(false);
    item.box.classList.add('vi-reading');
    item.box.classList.remove('vi-read');
    var reader = new Reader(item.built, { wps: state.style.timing.wps, reveal: state.style.timing.reveal === 'instant' ? 'instant' : 'audio' });
    this.reading = { item: item, captionId: captionId, reader: reader };
    startReader(reader);
  };

  ChatView.prototype.stopReading = function (finished) {
    var r = this.reading;
    if (!r) return;
    this.reading = null;
    stopReader(r.reader);
    if (finished) r.reader.finish();
    var box = r.item.box;
    setTimeout(function () {
      box.classList.remove('vi-reading');
      box.classList.add('vi-read');
    }, finished ? 900 : 0);
  };

  ChatView.prototype.handle = function (msg) {
    var self = this;
    switch (msg.type) {
      case 'chat':
        this.add(msg);
        break;
      case 'caption':
        state.captionId = msg.id;
        state.captionChat = typeof msg.chatId === 'string' ? msg.chatId : '';
        if (state.captionChat) {
          var item = this.find(state.captionChat);
          if (item) this.read(item, msg.id);
        }
        break;
      case 'progress':
        if (this.reading && this.reading.captionId === msg.id) this.reading.reader.progress(msg.f, msg.total);
        break;
      case 'end':
        if (this.reading && this.reading.captionId === msg.id) this.stopReading(true);
        if (state.captionId === msg.id) state.captionChat = '';
        break;
      case 'clear':
        this.stopReading(true);
        break;
      case 'chatDelete':
        var gone = this.find(msg.id);
        if (gone) this.remove(gone, true);
        break;
      case 'chatClearUser':
        var login = String(msg.login || '').toLowerCase();
        this.items.slice().forEach(function (it) { if (it.msg.login === login) self.remove(it, true); });
        break;
      case 'chatClear':
        this.items.slice().forEach(function (it) { self.remove(it, true); });
        break;
    }
  };

  // ---- Avatar (built-in PNGtuber) --------------------------------------------------------

  function AvatarView() {
    this.stage = el('div', 'vi-stage', root);
    this.shift = el('div', 'vi-shift', this.stage);
    this.avatar = el('div', 'vi-avatar', this.shift);
    this.look = el('div', 'vi-avatar-look', this.avatar);
    this.motion = el('div', 'vi-avatar-motion', this.look);
    this.flip = el('div', 'vi-avatar-flip', this.motion);
    this.images = {};
    this.imageKey = '';
    this.blob = null;
    this.level = 0;
    this.open = false;
    this.openedAt = 0;
    this.talkEnv = 0;
    this.blinking = false;
    this.nextBlink = now() + 2500;
    this.shown = '';
    var self = this;
    this.hook = function (t) { return self.frame(t); };
    frameHooks.push(this.hook);
    wake();
  }

  AvatarView.prototype.destroy = function () {
    var hook = this.hook;
    frameHooks = frameHooks.filter(function (h) { return h !== hook; });
    root.removeChild(this.stage);
  };

  AvatarView.prototype.relayout = function () {};

  AvatarView.prototype.setStyle = function () {
    var a = state.style.avatar;
    this.avatar.style.setProperty('--vi-avatar-size', a.size + 'vh');
    this.avatar.classList.toggle('vi-avatar-flipped', !!a.flip);
    var imgs = a.images || {};
    var key = ['idle', 'talking', 'blink', 'talkingBlink', 'micLive'].map(function (k) { return imgs[k] || ''; }).join('|');
    if (key !== this.imageKey) {
      this.imageKey = key;
      this.flip.textContent = '';
      this.images = {};
      this.blob = null;
      this.shown = '';
      var self = this;
      var ids = {};
      ['idle', 'talking', 'blink', 'talkingBlink', 'micLive'].forEach(function (k) {
        var id = imgs[k];
        if (typeof id !== 'string' || !/^[0-9a-f]{16}\.(png|gif|webp|jpg)$/.test(id)) return;
        if (!ids[id]) {
          var img = el('img', 'vi-avatar-img', self.flip);
          img.alt = '';
          img.draggable = false;
          img.decoding = 'async';
          img.src = '/assets/' + id;
          ids[id] = img;
        }
        self.images[k] = ids[id];
      });
      if (!this.images.idle) this.blob = new InkBlob(this.flip);
    }
    if (this.blob) this.blob.setColor(state.style.colors.ink);
    this.filter();
    wake();
  };

  AvatarView.prototype.filter = function () {
    var a = state.style.avatar;
    var parts = [];
    var talking = this.open || state.talking;
    if (a.dimWhenIdle > 0 && !talking) parts.push('brightness(' + (1 - a.dimWhenIdle / 100 * 0.8).toFixed(3) + ')');
    if (a.shadow > 0) {
      parts.push('drop-shadow(0 ' + (0.6 + a.shadow / 40).toFixed(2) + 'vh ' + (0.8 + a.shadow / 25).toFixed(2) + 'vh '
        + rgba(state.style.colors.shadow, 0.25 + a.shadow / 160) + ')');
    }
    if (a.micLiveGlow && state.mic) {
      parts.push('drop-shadow(0 0 0.35vh ' + state.style.colors.ink + ')');
      parts.push('drop-shadow(0 0 1.4vh ' + rgba(state.style.colors.ink, 0.85) + ')');
    }
    this.look.style.setProperty('--vi-avatar-filter', parts.length ? parts.join(' ') : 'none');
  };

  AvatarView.prototype.frame = function (t) {
    var a = state.style.avatar;
    var raw = t - state.levelAt > 800 ? 0 : state.level;
    // Smooth: open fast, close a little slower.
    this.level += (raw - this.level) * (raw > this.level ? 0.6 : 0.25);
    var threshold = a.threshold / 100;
    var wasOpen = this.open;
    var fresh = t - state.levelAt < 800;
    if (fresh) {
      if (!this.open && this.level >= threshold) { this.open = true; this.openedAt = t; }
      else if (this.open && this.level < threshold * 0.7 && t - this.openedAt > 90) this.open = false;
    } else {
      this.open = state.talking; // no levels: follow the app's talking flag
    }
    if (wasOpen !== this.open) this.filter();
    this.talkEnv += ((this.open || state.talking ? 1 : 0) - this.talkEnv) * 0.08;

    // Blinking.
    if (a.blinkEvery > 0 && (this.blob || this.images.blink)) {
      if (!this.blinking && t >= this.nextBlink) {
        this.blinking = true;
        this.blinkEnd = t + 140;
      } else if (this.blinking && t >= this.blinkEnd) {
        this.blinking = false;
        this.nextBlink = t + a.blinkEvery * 1000 * (0.7 + Math.random() * 0.6);
      }
    } else {
      this.blinking = false;
    }

    // Which picture.
    if (this.blob) {
      this.blob.update(this.open ? this.level : 0, this.open ? state.viseme : '', this.blinking);
    } else {
      var k = 'idle';
      if (this.open) k = this.blinking && this.images.talkingBlink ? 'talkingBlink' : (this.images.talking ? 'talking' : 'idle');
      else if (this.blinking && this.images.blink) k = 'blink';
      else if (state.mic && this.images.micLive) k = 'micLive';
      var img = this.images[k] || this.images.idle;
      if (img && this.shown !== img) {
        if (this.shown) this.shown.classList.remove('vi-on');
        img.classList.add('vi-on');
        this.shown = img;
      }
    }

    // Motion.
    var motion = reducedMotion ? 'none' : a.motion;
    var k2 = a.intensity / 100;
    var gate = a.motionOnlyWhileTalking ? this.talkEnv : 1;
    var L = this.level;
    var h = this.avatar.offsetHeight || window.innerHeight * 0.6;
    var transform = '';
    if (motion === 'bounce') {
      transform = 'translateY(' + (-L * h * 0.07 * k2 * gate).toFixed(2) + 'px)';
    } else if (motion === 'squash') {
      var sq = L * k2 * gate;
      transform = 'scale(' + (1 - sq * 0.06).toFixed(4) + ', ' + (1 + sq * 0.1).toFixed(4) + ')';
    } else if (motion === 'shake') {
      var sh = L * k2 * gate;
      transform = 'translateX(' + (Math.sin(t / 37) * h * 0.012 * sh).toFixed(2) + 'px) rotate(' + (Math.sin(t / 53) * 4 * sh).toFixed(2) + 'deg)';
    } else if (motion === 'float') {
      var fl = Math.sin(t / 3200 * Math.PI * 2) * h * 0.018 * k2 * gate;
      transform = 'translateY(' + (fl - L * h * 0.025 * k2 * gate).toFixed(2) + 'px)';
    }
    this.motion.style.transform = transform;
    return true;
  };

  AvatarView.prototype.handle = function (msg) {
    if (msg.type === 'avatar' || msg.type === 'mic') this.filter();
  };

  // The built-in character when no image is set: an ink drop with a face.
  function InkBlob(parent) {
    var s = svg('svg', { 'class': 'vi-blob', viewBox: '0 0 200 224', 'aria-hidden': 'true' }, parent);
    var defs = svg('defs', {}, s);
    var id = 'vi-blob-' + Math.random().toString(36).slice(2, 8);
    var grad = svg('linearGradient', { id: id, x1: '0.2', y1: '0', x2: '0.75', y2: '1' }, defs);
    this.stopTop = svg('stop', { offset: '0' }, grad);
    this.stopBottom = svg('stop', { offset: '1' }, grad);
    this.body = svg('path', {
      d: 'M100 10 C114 40 178 82 178 142 C178 188 144 214 100 214 C56 214 22 188 22 142 C22 82 86 40 100 10 Z',
      fill: 'url(#' + id + ')'
    }, s);
    svg('ellipse', { cx: '66', cy: '96', rx: '10', ry: '22', fill: '#ffffff', opacity: '0.28', transform: 'rotate(28 66 96)' }, s);
    svg('ellipse', { cx: '58', cy: '162', rx: '11', ry: '6', fill: '#ff8fc8', opacity: '0.5' }, s);
    svg('ellipse', { cx: '142', cy: '162', rx: '11', ry: '6', fill: '#ff8fc8', opacity: '0.5' }, s);
    var eyes = [];
    [74, 126].forEach(function (x) {
      var g = svg('g', { 'class': 'vi-lid' }, s);
      svg('ellipse', { cx: String(x), cy: '134', rx: '14', ry: '17', fill: '#ffffff' }, g);
      svg('circle', { cx: String(x + 2), cy: '137', r: '8', fill: '#1b1030' }, g);
      svg('circle', { cx: String(x + 5), cy: '132', r: '3', fill: '#ffffff' }, g);
      eyes.push(g);
    });
    this.svg = s;
    this.smile = svg('path', { d: 'M86 170 Q100 182 114 170', fill: 'none', stroke: '#1b1030', 'stroke-width': '5', 'stroke-linecap': 'round' }, s);
    this.mouth = svg('g', {}, s);
    this.mouthOuter = svg('ellipse', { cx: '100', cy: '174', rx: '10', ry: '6', fill: '#2a0d33' }, this.mouth);
    this.tongue = svg('ellipse', { cx: '100', cy: '180', rx: '6', ry: '3', fill: '#ff6f9f' }, this.mouth);
    this.lastKey = '';
  }

  InkBlob.prototype.setColor = function (ink) {
    var c = safeHex(ink, '#b48cff');
    this.stopTop.setAttribute('stop-color', mix(c, '#ffffff', 0.18));
    this.stopBottom.setAttribute('stop-color', mix(c, '#1a0a2e', 0.42));
  };

  var VISEMES = { A: [1.15, 1.0], I: [1.5, 0.45], U: [0.6, 0.75], E: [1.3, 0.65], O: [0.85, 1.0] };

  InkBlob.prototype.update = function (level, viseme, blinking) {
    var open = level > 0.02;
    var shape = VISEMES[viseme] || [1, 0.85];
    var rx = (8 + level * 6) * shape[0];
    var ry = (3 + level * 15) * shape[1];
    var key = (open ? rx.toFixed(1) + ',' + ry.toFixed(1) : 'closed') + (blinking ? 'b' : '');
    if (key === this.lastKey) return;
    this.lastKey = key;
    this.svg.classList.toggle('vi-blinking', blinking);
    this.smile.style.display = open ? 'none' : '';
    this.mouth.style.display = open ? '' : 'none';
    if (open) {
      this.mouthOuter.setAttribute('rx', rx.toFixed(2));
      this.mouthOuter.setAttribute('ry', ry.toFixed(2));
      this.mouthOuter.setAttribute('cy', (170 + ry * 0.5).toFixed(2));
      this.tongue.setAttribute('cy', (170 + ry * 1.05).toFixed(2));
      this.tongue.setAttribute('rx', Math.min(rx * 0.6, 7).toFixed(2));
      this.tongue.setAttribute('ry', Math.min(ry * 0.35, 4).toFixed(2));
    }
  };

  // ---- Messages -------------------------------------------------------------------------

  function makeView(kind) {
    if (view) view.destroy();
    state.kind = kind;
    if (kind === 'chat') view = new ChatView();
    else if (kind === 'avatar') view = new AvatarView();
    else view = new CaptionsView();
  }

  function onConfig(msg) {
    var profile = isObject(msg.profile) ? msg.profile : {};
    var kind = pick(profile.kind, ['captions', 'chat', 'avatar'], 'captions');
    state.config = msg;
    state.labels = merge(DEFAULT_LABELS, isObject(msg.labels) ? msg.labels : {});
    if (!Array.isArray(state.labels.demo) || !state.labels.demo.length) state.labels.demo = DEFAULT_LABELS.demo;
    var base = isObject(msg.style) ? merge(DEFAULT_STYLE, msg.style) : DEFAULT_STYLE;
    state.style = withUrlOptions(base, isObject(msg.presets) ? msg.presets : null, msg.legacyQuery);
    var kindChanged = kind !== state.kind || !view;
    if (kindChanged) state.kind = kind;
    applyStyle(state.style);
    if (kindChanged) makeView(kind);
    view.setStyle();
    var fonts = loadFonts(state.style, msg.fonts);
    var timeout = new Promise(function (resolve) { setTimeout(resolve, 1200); });
    Promise.race([fonts, timeout]).then(function () {
      markReady();
      if (view) view.relayout();
    });
    fonts.then(function () { if (view) view.relayout(); });
  }

  var build = null;

  function handle(msg) {
    if (!isObject(msg) || typeof msg.type !== 'string') return;
    switch (msg.type) {
      case 'hello':
        if (build && msg.build && msg.build !== build) {
          window.location.reload(); // the app was updated: get the new page
          return;
        }
        build = msg.build || build;
        return;
      case 'config':
        onConfig(msg);
        return;
      case 'speaking':
        state.speaking = !!msg.value;
        break;
      case 'listening':
        state.listening = !!msg.value;
        break;
      case 'mic':
        state.mic = !!msg.live;
        break;
      case 'avatar':
        state.talking = !!msg.talking;
        state.mic = !!msg.mic;
        break;
      case 'level':
        state.level = clamp(num(msg.v, 0), 0, 1);
        state.viseme = typeof msg.viseme === 'string' ? msg.viseme : '';
        state.levelAt = now();
        wake();
        break;
    }
    if (view) view.handle(msg);
  }

  // Messages from the app. In demo mode only the look comes from the app.
  function fromServer(msg) {
    if (demo && isObject(msg) && msg.type !== 'hello' && msg.type !== 'config') return;
    handle(msg);
  }

  var retryDelay = 1000;

  function connect() {
    if (!window.location.host || !window.WebSocket) return;
    var scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    var url = scheme + window.location.host + '/ws' + (requestedProfile ? '?profile=' + encodeURIComponent(requestedProfile) : '');
    var socket;
    try {
      socket = new WebSocket(url);
    } catch (e) {
      retry();
      return;
    }
    var opened = false;
    socket.onopen = function () {
      opened = true;
      retryDelay = 1000;
    };
    socket.onmessage = function (event) {
      var msg;
      try { msg = JSON.parse(event.data); } catch (e) { return; }
      fromServer(msg);
    };
    socket.onclose = function () {
      if (opened && !demo) {
        // The app went away: finish what is on screen.
        if (view && state.kind === 'captions' && view.current) view.end(view.current.id);
        handle({ type: 'speaking', value: false });
        handle({ type: 'listening', value: false });
        handle({ type: 'level', v: 0 });
      }
      retry();
    };
  }

  function retry() {
    setTimeout(connect, retryDelay);
    retryDelay = Math.min(10000, Math.round(retryDelay * 1.6));
  }

  // ---- demo=1: sample content for placing the source in OBS ------------------------------

  var DEMO_CHAT = [
    { name: 'PixelPanda', color: '#ff7ad9', badges: ['sub'], text: 'that jump was so clean omg' },
    { name: 'NightOwl_42', color: '#1e90ff', badges: ['mod'], text: 'Welcome in everyone, be nice in chat!' },
    { name: 'mossy', color: '#00c78c', badges: [], text: 'what game is this?' },
    { name: 'KitKat', color: '#ffb000', badges: ['vip'], text: 'first time here, love the captions', action: false },
    { name: 'Streamer', color: '#b48cff', badges: ['broadcaster'], text: 'waves at chat', action: true }
  ];

  function startDemo() {
    var n = 0;
    function sentence() { var list = state.labels.demo; return String(list[n % list.length]); }

    function speak(text, chatId, done) {
      var id = 100000 + (++n);
      var words = text.split(/\s+/).length;
      var total = Math.round(words / 2.6 * 1000 * 1.1) + 400;
      handle({ type: 'speaking', value: true });
      handle({ type: 'caption', id: id, text: text, voice: 'Demo voice', chatId: chatId || undefined });
      var start = now();
      var timer = setInterval(function () {
        var played = now() - start;
        var f = clamp(played / total, 0, 1);
        handle({ type: 'progress', id: id, f: f, ms: Math.round(played), total: total, known: true });
        var syllable = Math.abs(Math.sin(played / 95)) * (0.55 + 0.45 * Math.abs(Math.sin(played / 410)));
        handle({ type: 'level', v: f < 1 ? syllable : 0, viseme: 'AIUEO'.charAt(Math.floor(played / 160) % 5) });
        if (played >= total) {
          clearInterval(timer);
          handle({ type: 'level', v: 0 });
          handle({ type: 'end', id: id });
          handle({ type: 'speaking', value: false });
          done();
        }
      }, 66);
    }

    function captionsLoop() {
      speak(sentence(), '', function () { setTimeout(captionsLoop, 1400); });
    }

    var chatN = 0;
    function chatLoop() {
      var sample = DEMO_CHAT[chatN % DEMO_CHAT.length];
      var id = 'demo-' + (++chatN);
      handle({ type: 'chat', id: id, login: sample.name.toLowerCase(), name: sample.name, color: sample.color,
        badges: sample.badges, text: sample.text, action: !!sample.action });
      if (chatN % 2 === 1) {
        speak(sample.name + ' says: ' + sample.text, id, function () { setTimeout(chatLoop, 900); });
      } else {
        setTimeout(chatLoop, 2200);
      }
    }

    function avatarLoop() {
      var talking = false;
      function toggle() {
        talking = !talking;
        handle({ type: 'avatar', talking: talking, mic: false });
        if (talking) {
          var start = now();
          var len = 2600 + Math.random() * 1400;
          var timer = setInterval(function () {
            var t = now() - start;
            var v = Math.abs(Math.sin(t / 90)) * (0.5 + 0.5 * Math.abs(Math.sin(t / 380)));
            handle({ type: 'level', v: t < len ? v : 0, viseme: 'AIUEO'.charAt(Math.floor(t / 150) % 5) });
            if (t >= len) {
              clearInterval(timer);
              toggle();
            }
          }, 50);
        } else {
          setTimeout(toggle, 1500);
        }
      }
      toggle();
    }

    setTimeout(function () {
      if (state.kind === 'chat') chatLoop();
      else if (state.kind === 'avatar') avatarLoop();
      else captionsLoop();
    }, 700);
  }

  // ---- Start ----------------------------------------------------------------------------

  state.style = withUrlOptions(DEFAULT_STYLE, null, '');
  applyStyle(state.style);
  makeView('captions');
  view.setStyle();
  window.addEventListener('resize', function () { if (view) view.relayout(); });
  connect();
  if (demo) {
    // Wait briefly for the look from the app (demo pages work without it too).
    var begin = function () { startDemo(); };
    if (window.location.host) setTimeout(begin, 600);
    else begin();
  }

  // For tests and screenshots without the app.
  window.vocalInkOverlay = {
    handle: handle,
    state: state,
    version: 2
  };
})();
