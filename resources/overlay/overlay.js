// Vocal Ink caption overlay: receives captions from the app over a WebSocket
// and reveals them word by word. Styled via URL query parameters (docs/OBS.md).
(function () {
  'use strict';

  var params = new URLSearchParams(window.location.search);

  function text(name, fallback) {
    var v = params.get(name);
    return v === null || v.trim() === '' ? fallback : v.trim();
  }

  function number(name, fallback, min, max) {
    var v = parseFloat(params.get(name));
    return isFinite(v) ? Math.min(max, Math.max(min, v)) : fallback;
  }

  function choice(name, allowed, fallback) {
    var v = (params.get(name) || '').trim().toLowerCase();
    return allowed.indexOf(v) >= 0 ? v : fallback;
  }

  function flag(name, fallback) {
    var v = params.get(name);
    if (v === null) return fallback;
    return v === '' || /^(1|true|yes|on)$/i.test(v.trim());
  }

  // "#" starts the URL fragment, so hex colours may be given without it: color=ffcc00, bg=000000aa.
  function color(name, fallback) {
    var v = text(name, '');
    if (!v) return fallback;
    if (/^(none|transparent)$/i.test(v)) return 'transparent';
    if (/^([0-9a-f]{3,4}|[0-9a-f]{6}|[0-9a-f]{8})$/i.test(v)) v = '#' + v;
    return window.CSS && CSS.supports && CSS.supports('color', v) ? v : fallback;
  }

  var style = choice('style', ['subtitles', 'bubble', 'plain'], 'subtitles');
  var align = choice('align', ['center', 'left', 'right'], 'center');
  var cfg = {
    style: style,
    position: choice('position', ['bottom', 'top', 'middle'], 'bottom'),
    align: align,
    tail: choice('tail', ['left', 'center', 'right', 'none'], align),
    font: text('font', ''),
    size: number('size', 42, 10, 300),
    color: color('color', style === 'bubble' ? '#1c1838' : '#ffffff'),
    bg: color('bg', style === 'bubble' ? '#ffffff' : style === 'plain' ? 'transparent' : 'rgba(12, 11, 22, 0.74)'),
    outline: number('outline', style === 'plain' ? 3 : 0, 0, 24),
    outlineColor: color('outlinecolor', '#000000'),
    reveal: choice('reveal', ['word', 'instant'], 'word'),
    wps: number('wps', 3.5, 0.5, 30),
    hold: number('hold', 4, -1, 86400),
    lines: Math.round(number('lines', 2, 1, 30)),
    width: number('width', style === 'bubble' ? 42 : style === 'plain' ? 80 : 70, 10, 100),
    roll: flag('roll', style !== 'bubble'),
    name: flag('name', false),
    indicator: flag('indicator', false),
    motion: flag('motion', true),
    demo: flag('demo', false)
  };

  var root = document.documentElement;
  var box = document.getElementById('box');
  var label = document.getElementById('label');
  var viewport = document.getElementById('viewport');
  var scroller = document.getElementById('scroller');
  var lineHeight = Math.round(cfg.size * 1.3);

  var reducedMotion = !cfg.motion ||
    (window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches);

  // ---- Appearance --------------------------------------------------------

  function outlineShadow(px, col) {
    if (px <= 0) return '0 0 0 transparent';
    var shadows = [];
    var rings = Math.max(1, Math.ceil(px / 3));
    for (var ring = 1; ring <= rings; ring++) {
      var r = px * ring / rings;
      var steps = Math.max(8, Math.round(r * 6));
      for (var i = 0; i < steps; i++) {
        var a = (i / steps) * Math.PI * 2;
        shadows.push((Math.cos(a) * r).toFixed(2) + 'px ' + (Math.sin(a) * r).toFixed(2) + 'px 0 ' + col);
      }
    }
    return shadows.join(', ');
  }

  function fontFamily(value) {
    // A single family name gets quoted; a list is used as given.
    var family = value.indexOf(',') >= 0 ? value : '"' + value.replace(/["\\]/g, '') + '"';
    return family + ', var(--font-default)';
  }

  function applyConfig() {
    root.setAttribute('data-style', cfg.style);
    root.setAttribute('data-position', cfg.position);
    root.setAttribute('data-align', cfg.align);
    root.setAttribute('data-tail', cfg.tail);
    root.style.setProperty('--size', cfg.size + 'px');
    root.style.setProperty('--lh', lineHeight + 'px');
    root.style.setProperty('--color', cfg.color);
    root.style.setProperty('--bg', cfg.bg);
    root.style.setProperty('--maxw', cfg.width + 'vw');
    root.style.setProperty('--outline-shadow', outlineShadow(cfg.outline, cfg.outlineColor));
    if (cfg.font) root.style.setProperty('--font', fontFamily(cfg.font));
    root.classList.toggle('no-bg', cfg.bg === 'transparent');
    root.classList.toggle('reduced-motion', reducedMotion);
  }

  // ---- Caption state -----------------------------------------------------

  var state = { speaking: false, listening: false, captionVisible: false, voice: '' };
  var current = null;   // message being revealed: { id, el, words, index, timer }
  var holdTimer = 0;
  var cleanupTimer = 0;
  var firstLine = 0;    // index of the top visible line

  var cjk = /[\u3040-\u30ff\u3400-\u4dbf\u4e00-\u9fff\uf900-\ufaff\uff66-\uff9f]/;

  function buildMessage(message, words) {
    var p = document.createElement('p');
    p.className = 'msg';
    var tokens = message.split(/\s+/).filter(function (t) { return t !== ''; });
    // The last two words stay together, so no line holds a single leftover word.
    var glue = tokens.length >= 4 && !cjk.test(tokens[tokens.length - 1]) ? tokens.length - 2 : -1;
    var parent = p;
    tokens.forEach(function (token, i) {
      if (i > 0) parent.appendChild(document.createTextNode(' '));
      if (i === glue) {
        parent = document.createElement('span');
        parent.className = 'keep';
        p.appendChild(parent);
      }
      // Scripts written without spaces are revealed character by character.
      var pieces = cjk.test(token) && token.length > 1 ? Array.from(token) : [token];
      pieces.forEach(function (piece) {
        var span = document.createElement('span');
        span.className = 'w';
        span.textContent = piece;
        parent.appendChild(span);
        words.push(span);
      });
    });
    return p;
  }

  function wordDelay(word) {
    var base = 1000 / cfg.wps;
    if (cjk.test(word) && word.length === 1) return base * 0.4;
    var letters = word.replace(/[^0-9A-Za-z\u00C0-\u024F\u0370-\u03FF\u0400-\u04FF]/g, '').length || word.length;
    var weight = Math.min(1.7, Math.max(0.6, 0.55 + letters * 0.09));
    if (/[.!?\u2026]["'\u201d\u2019)\]]*$/.test(word)) weight += 0.9;
    else if (/[,;:\u2013\u2014]["'\u201d\u2019)\]]*$/.test(word)) weight += 0.35;
    return base * weight;
  }

  function showCaption(id, message, voice) {
    clearTimeout(holdTimer);
    clearTimeout(cleanupTimer);
    if (current) revealAll(current);

    var keep = cfg.roll && state.captionVisible && scroller.children.length > 0;
    if (keep) {
      for (var i = 0; i < scroller.children.length; i++) scroller.children[i].classList.add('old');
      prune();
    } else {
      scroller.textContent = '';
      firstLine = 0;
      setScroll(0, true);
    }

    var words = [];
    var el = buildMessage(message, words);
    scroller.appendChild(el);
    current = { id: id, el: el, words: words, index: 0, timer: 0 };
    var appearing = !box.classList.contains('show');
    state.captionVisible = true;
    state.voice = voice || '';
    render(appearing);

    if (cfg.reveal === 'instant') revealAll(current);
    else step(current);
  }

  function step(msg) {
    if (msg !== current || msg.index >= msg.words.length) return;
    var word = msg.words[msg.index++];
    word.classList.add('on');
    layout();
    msg.timer = setTimeout(function () { step(msg); }, wordDelay(word.textContent));
  }

  function revealAll(msg) {
    clearTimeout(msg.timer);
    for (; msg.index < msg.words.length; msg.index++) msg.words[msg.index].classList.add('on');
    layout();
  }

  function endCaption(id) {
    if (!current || current.id !== id) return;
    revealAll(current);
    current = null;
    scheduleHide();
  }

  function scheduleHide() {
    clearTimeout(holdTimer);
    if (cfg.hold < 0) return; // keep until the next caption
    holdTimer = setTimeout(hideCaption, cfg.hold * 1000);
  }

  function hideCaption() {
    clearTimeout(holdTimer);
    if (current) {
      clearTimeout(current.timer);
      current = null;
    }
    state.captionVisible = false;
    render();
    clearTimeout(cleanupTimer);
    cleanupTimer = setTimeout(function () {
      if (state.captionVisible) return;
      scroller.textContent = '';
      firstLine = 0;
      setScroll(0, true);
      setHeight(0, true);
    }, 600);
  }

  // Drops earlier messages that have scrolled out of view, without visible movement.
  function prune() {
    var removed = 0;
    while (scroller.children.length > 1) {
      var first = scroller.children[0];
      var lines = Math.round(first.offsetHeight / lineHeight);
      if (lines > firstLine) break;
      removed += lines;
      firstLine -= lines;
      scroller.removeChild(first);
    }
    if (removed) setScroll(firstLine, true);
  }

  function setScroll(line, instant) {
    if (instant) {
      scroller.style.transition = 'none';
      scroller.style.transform = 'translateY(' + (-line * lineHeight) + 'px)';
      void scroller.offsetHeight; // apply before re-enabling the transition
      scroller.style.transition = '';
    } else {
      scroller.style.transform = 'translateY(' + (-line * lineHeight) + 'px)';
    }
  }

  function setHeight(px, instant) {
    if (instant) viewport.style.transition = 'none';
    viewport.style.height = px + 'px';
    if (instant) {
      void viewport.offsetHeight;
      viewport.style.transition = '';
    }
  }

  function lineOf(el) {
    return Math.max(0, Math.round(el.offsetTop / lineHeight));
  }

  // Sizes the text window to the lines revealed so far (at most `lines`) and keeps
  // the newest word in view. Unrevealed words already take their final place, so
  // the width doesn't jump while words appear.
  function layout(instant) {
    if (!state.captionVisible) {
      // Only the indicator stays: shrink to it. A box that fades out keeps its size.
      if (box.classList.contains('show')) setHeight(0, false);
      return;
    }
    var totalLines = Math.max(1, Math.round(scroller.offsetHeight / lineHeight));
    var visible = Math.min(cfg.lines, totalLines);
    var focus = 0;
    if (current && current.words.length) {
      var word = current.index > 0 ? current.words[current.index - 1] : current.words[0];
      focus = lineOf(word);
    } else {
      focus = totalLines - 1;
    }
    var revealed = Math.min(totalLines, focus + 1);
    visible = Math.min(visible, revealed);
    firstLine = revealed - visible;
    setHeight(visible * lineHeight, instant);
    setScroll(firstLine, instant);
  }

  function render(instant) {
    var signal = cfg.indicator && (state.speaking || state.listening);
    var listening = cfg.indicator && state.listening && !state.speaking;
    var meta = '';
    if (listening) meta = 'listening\u2026';
    else if (cfg.name && state.captionVisible && state.voice) meta = state.voice;
    else if (cfg.indicator && state.speaking) meta = 'speaking\u2026';

    label.textContent = meta;
    box.classList.toggle('listening', listening);
    box.classList.toggle('signal-on', signal);
    box.classList.toggle('has-meta', signal || meta !== '');
    box.classList.toggle('meta-only', !state.captionVisible);
    box.classList.toggle('show', state.captionVisible || signal);
    layout(instant);
  }

  // ---- Messages from the app ---------------------------------------------

  function handle(msg) {
    if (!msg || typeof msg !== 'object') return;
    switch (msg.type) {
      case 'caption':
        showCaption(msg.id, String(msg.text || ''), msg.voice ? String(msg.voice) : '');
        break;
      case 'end':
        endCaption(msg.id);
        break;
      case 'clear':
        hideCaption();
        break;
      case 'speaking':
        state.speaking = !!msg.value;
        render();
        break;
      case 'listening':
        state.listening = !!msg.value;
        render();
        break;
    }
  }

  var retryDelay = 1000;

  function connect() {
    var host = window.location.host || '127.0.0.1:7342';
    var scheme = window.location.protocol === 'https:' ? 'wss://' : 'ws://';
    var socket;
    try {
      socket = new WebSocket(scheme + host + '/ws');
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
      handle(msg);
    };
    socket.onclose = function () {
      if (opened) {
        // The app went away: finish what is on screen.
        if (current) endCaption(current.id);
        state.speaking = false;
        state.listening = false;
        render();
      }
      retry();
    };
  }

  function retry() {
    setTimeout(connect, retryDelay);
    retryDelay = Math.min(5000, Math.round(retryDelay * 1.6));
  }

  // ---- demo=1: sample captions for positioning the source in OBS ---------

  var samples = [
    'Hey chat! Thanks so much for the follow, welcome in.',
    'Give me one second, I am going to try this jump again.',
    'Okay\u2026 that was not my finest moment. Clip it anyway!'
  ];

  function demo() {
    var n = 0;
    function next() {
      var id = ++n;
      var message = samples[(id - 1) % samples.length];
      handle({ type: 'speaking', value: true });
      handle({ type: 'caption', id: id, text: message, voice: 'Demo voice' });
      var words = message.split(/\s+/).length;
      setTimeout(function () {
        handle({ type: 'end', id: id });
        handle({ type: 'speaking', value: false });
        var pause = id % samples.length === 0 ? Math.max(0, cfg.hold) * 1000 + 1500 : 1200;
        setTimeout(next, pause);
      }, (words / cfg.wps) * 1000 + 900);
    }
    next();
  }

  applyConfig();
  render();
  if (cfg.demo) demo();
  else connect();

  // Exposed for testing the page without the app.
  window.vocalInkOverlay = { handle: handle, config: cfg };
})();
