(() => {
  'use strict';
  const $ = id => document.getElementById(id);
  const STORAGE_KEY = 'lloona.studio.v2';
  const state = {
    view: 'llum', online: false, busy: false, status: null, polling: false,
    draft: Lamp.blank(), baseline: '', applied: '', name: '', savedSignature: '', savedName: '', history: [], historyIndex: -1,
    ring: 'int', tool: 'paint', brush: { r: 13, g: 7, b: 3, w: 0, f: false },
    hsv: { h: 24, s: 0.77, v: 13 }, keyboardLed: 0, keyboardFocus: false,
    names: [], scenesLoaded: false, sceneCache: new Map(), galleryRun: 0, selected: null,
  };
  let toastTimer, renderFrame, pollTimer, requestQueue = Promise.resolve();
  const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');

  // Serialize requests: WebServer handles one client at a time on the ESP32.
  // Never retry writes automatically; a lost response can follow a successful write.
  function api(path, { method = 'GET', body, json = false } = {}) {
    const request = async () => {
      const controller = new AbortController();
      const timeout = setTimeout(() => controller.abort(), 8000);
      try {
        const response = await fetch(path, {
          method, cache: 'no-store', signal: controller.signal,
          ...(body ? { headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body) } : {}),
        });
        const content = await response.text();
        setOnline(true);
        if (!response.ok) throw new Error(content || `Error del llum (${response.status}).`);
        return json ? JSON.parse(content) : content;
      } catch (error) {
        if (error.name === 'AbortError' || error instanceof TypeError) {
          setOnline(false);
          throw new Error('No s’ha pogut confirmar la resposta del llum. Comprova la connexió i torna-ho a provar.');
        }
        throw error;
      } finally { clearTimeout(timeout); }
    };
    const result = requestQueue.then(request);
    requestQueue = result.catch(() => {});
    return result;
  }
  function setOnline(value) {
    state.online = value;
    $('connection').className = 'connection mono ' + (value ? 'online' : 'offline');
    $('connectionText').textContent = value ? 'connectat' : 'sense connexió';
    $('networkNotice').hidden = value;
    updateDeviceButtons();
  }
  function updateDeviceButtons() {
    document.querySelectorAll('[data-device]').forEach(button => { button.disabled = !state.online || state.busy; });
    if (state.status?.mode === 'astro') $('autoButton').disabled = true;
  }
  function toast(message, error = false) {
    clearTimeout(toastTimer);
    $('toast').textContent = message;
    $('toast').className = 'toast' + (error ? ' error' : '');
    $('toast').hidden = false;
    toastTimer = setTimeout(() => { $('toast').hidden = true; }, error ? 7500 : 4000);
  }
  async function action(button, task) {
    if (state.busy) return;
    state.busy = true;
    const text = button?.textContent;
    if (button) { button.textContent = 'Un moment…'; button.setAttribute('aria-busy', 'true'); }
    updateDeviceButtons();
    try { await task(); }
    catch (error) { toast(error.message, true); }
    finally {
      state.busy = false;
      if (button) { button.textContent = text; button.removeAttribute('aria-busy'); }
      updateDeviceButtons();
    }
  }
  function confirmAction(title, description, accept = 'Continua') {
    $('confirmTitle').textContent = title;
    $('confirmText').textContent = description;
    $('acceptConfirm').textContent = accept;
    return new Promise(resolve => {
      const dialog = $('confirmDialog');
      dialog.returnValue = '';
      dialog.addEventListener('close', () => resolve(dialog.returnValue === 'accept'), { once: true });
      $('cancelConfirm').onclick = () => dialog.close('cancel');
      $('acceptConfirm').onclick = () => dialog.close('accept');
      dialog.showModal();
      $('cancelConfirm').focus();
    });
  }

  // A single renderer is shared by live output, saved scenes and the editor.
  function drawScene(canvas, scene, { editor = false, time = 0 } = {}) {
    const ctx = canvas.getContext('2d');
    const size = canvas.width, center = size / 2;
    ctx.clearRect(0, 0, size, size);
    ctx.save();
    ctx.translate(center, center);
    const disc = ctx.createRadialGradient(-size * .08, -size * .09, 0, 0, 0, size * .32);
    disc.addColorStop(0, '#fffef9'); disc.addColorStop(1, '#ecece3');
    ctx.beginPath(); ctx.arc(0, 0, size * .317, 0, Math.PI * 2);
    ctx.fillStyle = disc; ctx.fill();
    ctx.strokeStyle = '#d9ddd1'; ctx.lineWidth = size * .003; ctx.stroke();
    for (let ring = 0; ring < 2; ring++) {
      const radius = size * (ring === 0 ? .405 : .325);
      const selected = !editor || state.ring === 'both' || state.ring === (ring ? 'int' : 'ext');
      ctx.globalAlpha = selected ? 1 : .38;
      ctx.beginPath(); ctx.arc(0, 0, radius, 0, Math.PI * 2);
      ctx.strokeStyle = editor && selected ? '#bcc7af' : '#dde0d5';
      ctx.lineWidth = size * (editor ? .048 : .019); ctx.stroke();
      for (let i = 0; i < Lamp.COUNT; i++) {
        const c = scene[ring * Lamp.COUNT + i];
        const lit = c.r + c.g + c.b + c.w > 0;
        const angle = i / Lamp.COUNT * Math.PI * 2 - Math.PI / 2;
        const flicker = c.f && time ? .76 + .18 * Math.sin(time / 150 + i * 1.9) : 1;
        const color = Lamp.displayColor(c, flicker);
        ctx.strokeStyle = color;
        ctx.lineWidth = size * (editor ? .030 : .014);
        ctx.lineCap = 'round';
        ctx.shadowColor = color;
        ctx.shadowBlur = lit && !editor ? size * .038 : 0;
        ctx.beginPath(); ctx.arc(0, 0, radius, angle - .029, angle + .029); ctx.stroke();
        ctx.shadowBlur = 0;
        if (c.f && editor) {
          ctx.fillStyle = '#fffaf0'; ctx.beginPath();
          ctx.arc(Math.cos(angle) * radius, Math.sin(angle) * radius, size * .003, 0, Math.PI * 2); ctx.fill();
        }
      }
      if (editor && selected && state.keyboardFocus) {
        const a = state.keyboardLed / Lamp.COUNT * Math.PI * 2 - Math.PI / 2;
        ctx.strokeStyle = '#24382a'; ctx.lineWidth = 3;
        ctx.beginPath(); ctx.arc(Math.cos(a) * radius, Math.sin(a) * radius, size * .024, 0, Math.PI * 2); ctx.stroke();
      }
    }
    ctx.globalAlpha = 1;
    if (editor) {
      ctx.font = `${size * .020}px monospace`; ctx.textAlign = 'center'; ctx.fillStyle = '#7b8473';
      ctx.fillText('0°', 0, -size * .467);
    }
    ctx.restore();
  }
  function requestRender() {
    if (!renderFrame && !document.hidden) renderFrame = requestAnimationFrame(render);
  }
  function render(time) {
    renderFrame = null;
    const animate = !reducedMotion.matches && !document.hidden;
    let fire = false;
    if (state.view === 'estudi') {
      drawScene($('editorCanvas'), state.draft, { editor: true, time: animate ? time : 0 });
      fire = state.draft.some(c => c.f);
    } else if (state.view === 'llum') {
      const scene = state.live || Lamp.blank();
      drawScene($('liveCanvas'), scene);
    }
    if ($('sceneDialog').open && state.selected) {
      drawScene($('scenePreview'), state.selected.scene, { time: animate ? time : 0 });
      fire ||= state.selected.scene.some(c => c.f);
    }
    if (animate && fire) renderFrame = requestAnimationFrame(render);
  }

  // View navigation is deliberately independent of the lamp operating mode.
  function navigate(view) {
    if (!['llum', 'escenes', 'estudi'].includes(view)) view = 'llum';
    const changed = state.view !== view;
    state.view = view;
    document.querySelectorAll('.view').forEach(panel => { panel.hidden = panel.id !== 'view-' + view; });
    document.querySelectorAll('[data-view]').forEach(button => {
      if (button.dataset.view === view) button.setAttribute('aria-current', 'page');
      else button.removeAttribute('aria-current');
    });
    history.replaceState(null, '', '#' + view);
    if (changed) window.scrollTo({ top: 0, behavior: 'instant' });
    if (view === 'escenes') loadScenes().catch(error => toast(error.message, true));
    else state.galleryRun++;
    requestRender();
  }
  function updateStatus(data) {
    // Invalid wire data must never replace a local draft or generate fake previews.
    state.live = Lamp.decode(data);
    state.status = data;
    const automatic = data.mode === 'astro', simulated = data.mode === 'simulacio';
    const waiting = !simulated && !data.clock_synced;
    $('modeBadge').textContent = automatic ? 'cicle natural' : simulated ? 'simulació' : 'escena manual';
    $('liveLabel').textContent = waiting && automatic ? 'ESPERANT L’HORA NTP' :
      automatic || simulated ? (data.is_daytime ? '☼ COLORS DE DIA' : '☾ LLUNA DE NIT') : '◌ LA TEVA ATMOSFERA';
    $('homeDescription').textContent = automatic ? 'Una òrbita de color. Una finestra al cel.' :
      simulated ? 'Un altre moment, sota el mateix cel.' : 'Una atmosfera triada per tu.';
    $('autoTitle').textContent = 'Segueix el cel';
    $('autoDescription').textContent = automatic ? 'Colors de dia. Lluna en pondre’s el sol.' : 'El cicle natural està en pausa.';
    $('autoButton').textContent = automatic ? 'Actiu ✓' : 'Reprèn';
    const fraction = Math.max(0, Math.min(1, data.fraction));
    $('moonFraction').replaceChildren(document.createTextNode(waiting ? '—' : (fraction * 100).toFixed(1)), Object.assign(document.createElement('small'), { textContent: '%' }));
    $('moonPhase').textContent = waiting ? 'Esperant sincronització' : fraction < .02 ? 'Lluna nova' : fraction > .98 ? 'Lluna plena' : data.is_waxing ? 'Lluna creixent' : 'Lluna minvant';
    const date = new Date((simulated ? data.sim_ts : data.timestamp) * 1000);
    $('timeLabel').textContent = simulated ? 'MOMENT SIMULAT' : 'HORA LOCAL';
    $('localTime').textContent = waiting ? '—:—' : date.toLocaleTimeString('ca', { hour: '2-digit', minute: '2-digit' });
    $('localDate').textContent = waiting ? 'Connexió amb NTP pendent' : date.toLocaleDateString('ca', { day: 'numeric', month: 'short', year: 'numeric' });
    if (simulated && document.activeElement !== $('simDate')) $('simDate').value = localInput(date);
    updateDeviceButtons();
    requestRender();
  }
  async function poll() {
    if (state.polling || document.hidden) return;
    state.polling = true;
    try { updateStatus(await api('/api/status', { json: true })); }
    catch (error) { if (state.online) toast(error.message, true); }
    finally { state.polling = false; }
  }
  function schedulePoll() {
    clearTimeout(pollTimer);
    pollTimer = setTimeout(async () => {
      if (!state.busy) await poll();
      schedulePoll();
    }, state.online ? 15000 : 10000);
  }
  function localInput(date) {
    return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 16);
  }

  // Studio history records complete gestures, not individual pointer events.
  function storeDraft() {
    try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ ...Lamp.encode(state.draft), name: $('presetName').value, baseline: state.baseline, savedSignature: state.savedSignature, savedName: state.savedName })); }
    catch { /* Storage may be disabled. The in-memory editor still works. */ }
  }
  function syncDraftUI() {
    const signature = Lamp.signature(state.draft);
    const applied = signature === state.applied;
    $('undo').disabled = state.historyIndex <= 0;
    $('redo').disabled = state.historyIndex >= state.history.length - 1;
    $('draftBadge').textContent = signature === state.savedSignature && $('presetName').value.trim() === state.savedName ? 'desat' : 'esborrany';
    $('draftStatus').textContent = applied ? 'Enviat al llum ✓' : 'El teu esborrany';
    $('applyHint').textContent = applied ? 'Pots continuar editant' : 'A punt per aplicar';
    storeDraft();
    requestRender();
  }
  function checkpoint() {
    const signature = Lamp.signature(state.draft);
    if (state.history[state.historyIndex] === signature) return;
    state.history = state.history.slice(0, state.historyIndex + 1);
    state.history.push(signature);
    if (state.history.length > 30) state.history.shift();
    state.historyIndex = state.history.length - 1;
    syncDraftUI();
  }
  function restoreHistory(delta) {
    const next = state.historyIndex + delta;
    if (next < 0 || next >= state.history.length) return;
    state.historyIndex = next;
    const packed = state.history[next];
    state.draft = Lamp.decode({ ext: packed.slice(0, Lamp.COUNT * 9), int: packed.slice(Lamp.COUNT * 9) });
    syncDraftUI();
  }
  async function replaceDraft(scene, name = '', saved = false) {
    if (Lamp.signature(state.draft) !== state.baseline && !await confirmAction('Substitueix l’esborrany?', 'Tens canvis sense desar a la col·lecció. El nou llenç substituirà aquests canvis.', 'Substitueix')) return false;
    state.draft = Lamp.clone(scene);
    state.name = name;
    $('presetName').value = name;
    state.baseline = Lamp.signature(scene);
    state.savedSignature = saved ? state.baseline : '';
    state.savedName = saved ? name : '';
    state.applied = '';
    state.history = []; state.historyIndex = -1;
    checkpoint();
    navigate('estudi');
    return true;
  }
  function selectedRings() { return state.ring === 'both' ? [0, 1] : [state.ring === 'ext' ? 0 : 1]; }
  function setRing(ring) {
    state.ring = ring;
    document.querySelectorAll('[data-ring]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.ring === ring)));
    $('editorHint').textContent = ring === 'int' ? 'INTERIOR' : ring === 'ext' ? 'EXTERIOR' : 'DOS ANELLS';
    $('fillRing').textContent = ring === 'int' ? 'Omple l’interior' : ring === 'ext' ? 'Omple l’exterior' : 'Omple els dos';
    $('clearRing').textContent = ring === 'both' ? 'Neteja els dos' : 'Neteja l’anell';
    requestRender();
  }
  function setTool(tool) {
    state.tool = tool;
    document.querySelectorAll('[data-tool]').forEach(button => button.setAttribute('aria-pressed', String(button.dataset.tool === tool)));
    $('editorCanvas').style.cursor = tool === 'picker' ? 'copy' : 'crosshair';
  }
  function paintAt(index) {
    const width = Number($('brushSize').value);
    for (const ring of selectedRings()) {
      for (const i of Lamp.brushIndices(index, width)) state.draft[ring * Lamp.COUNT + i] = state.tool === 'erase' ? { r: 0, g: 0, b: 0, w: 0, f: false } : { ...state.brush };
    }
    requestRender();
  }
  function fill(rings, clear = false) {
    for (const ring of rings) for (let i = 0; i < Lamp.COUNT; i++) state.draft[ring * Lamp.COUNT + i] = clear ? { r: 0, g: 0, b: 0, w: 0, f: false } : { ...state.brush };
    checkpoint();
  }
  function editorHit(event) {
    const rect = $('editorCanvas').getBoundingClientRect();
    const dx = (event.clientX - rect.left) / rect.width - .5;
    const dy = (event.clientY - rect.top) / rect.height - .5;
    const radius = Math.hypot(dx, dy);
    // The selected ring owns a generous hit area without painting the other ring.
    if (radius < .245 || radius > .48) return null;
    const ring = state.ring === 'both' ? (radius > .365 ? 0 : 1) : selectedRings()[0];
    const angle = (Math.atan2(dy, dx) + Math.PI / 2 + Math.PI * 2) % (Math.PI * 2);
    return { ring, index: Math.round(angle / (Math.PI * 2) * Lamp.COUNT) % Lamp.COUNT };
  }
  function initEditor() {
    const canvas = $('editorCanvas');
    let pointer = null, last = null;
    const paint = event => {
      const hit = editorHit(event);
      if (!hit) { last = null; return; }
      state.keyboardLed = hit.index;
      if (state.tool === 'picker') {
        setBrush(state.draft[hit.ring * Lamp.COUNT + hit.index]);
        setTool('paint'); pointer = null;
        toast('Color i canal blanc capturats.');
        return;
      }
      const indices = last === null ? [hit.index] : Lamp.arcIndices(last, hit.index);
      indices.forEach(paintAt);
      last = hit.index;
    };
    canvas.addEventListener('pointerdown', event => {
      if (pointer !== null || (event.pointerType === 'mouse' && event.button !== 0)) return;
      pointer = event.pointerId; last = null; state.keyboardFocus = false;
      canvas.setPointerCapture(event.pointerId); paint(event); event.preventDefault();
    });
    canvas.addEventListener('pointermove', event => { if (event.pointerId === pointer) paint(event); });
    const finish = event => {
      if (event.pointerId === pointer) { pointer = null; last = null; checkpoint(); }
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    };
    ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(type => canvas.addEventListener(type, finish));
    canvas.addEventListener('keydown', event => {
      if (!['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', ' ', 'Enter'].includes(event.key)) return;
      event.preventDefault(); state.keyboardFocus = true;
      if (event.key.startsWith('Arrow')) state.keyboardLed = (state.keyboardLed + (['ArrowLeft', 'ArrowDown'].includes(event.key) ? -1 : 1) + Lamp.COUNT) % Lamp.COUNT;
      else if (state.tool === 'picker') { setBrush(state.draft[selectedRings()[0] * Lamp.COUNT + state.keyboardLed]); setTool('paint'); }
      else { paintAt(state.keyboardLed); checkpoint(); }
      canvas.setAttribute('aria-label', `Editor dels anells. LED ${state.keyboardLed + 1} de 72. Fletxes per moure, espai per pintar.`);
      requestRender();
    });
    canvas.addEventListener('blur', () => { state.keyboardFocus = false; requestRender(); });
  }

  // HSV selection is computed analytically: no pixel sampling or rounding drift.
  let wheelImage;
  function drawWheel() {
    const canvas = $('colorWheel'), ctx = canvas.getContext('2d'), size = canvas.width, radius = size / 2 - 9;
    if (!wheelImage) {
      wheelImage = ctx.createImageData(size, size);
      for (let y = 0; y < size; y++) for (let x = 0; x < size; x++) {
        const dx = x - size / 2, dy = y - size / 2, saturation = Math.hypot(dx, dy) / radius;
        if (saturation > 1) continue;
        const color = Lamp.hsv(Math.atan2(dy, dx) * 180 / Math.PI, saturation, 255), i = (y * size + x) * 4;
        wheelImage.data.set([color.r, color.g, color.b, 255], i);
      }
    }
    ctx.putImageData(wheelImage, 0, 0);
    const angle = state.hsv.h * Math.PI / 180;
    const x = size / 2 + Math.cos(angle) * radius * state.hsv.s, y = size / 2 + Math.sin(angle) * radius * state.hsv.s;
    ctx.beginPath(); ctx.arc(x, y, 11, 0, Math.PI * 2); ctx.strokeStyle = '#fff'; ctx.lineWidth = 5; ctx.stroke();
    ctx.beginPath(); ctx.arc(x, y, 14, 0, Math.PI * 2); ctx.strokeStyle = '#263b32'; ctx.lineWidth = 2; ctx.stroke();
    canvas.setAttribute('aria-valuenow', String(Math.round(state.hsv.h)));
    canvas.setAttribute('aria-valuetext', `To ${Math.round(state.hsv.h)} graus, saturació ${Math.round(state.hsv.s * 100)} per cent`);
  }
  function syncBrushUI() {
    const c = state.brush;
    $('brightness').value = state.hsv.v; $('brightnessValue').textContent = Math.round(state.hsv.v / 255 * 100) + '%';
    $('whiteChannel').value = c.w; $('whiteValue').textContent = Math.round(c.w / 255 * 100) + '%';
    $('whiteExact').value = c.w;
    $('hexColor').value = Lamp.rgbHex(c); $('colorHexLabel').textContent = Lamp.rgbHex(c);
    $('hue').value = Math.round(state.hsv.h) % 360; $('hueValue').textContent = Math.round(state.hsv.h) + '°';
    $('saturation').value = Math.round(state.hsv.s * 100); $('saturationValue').textContent = Math.round(state.hsv.s * 100) + '%';
    $('fireToggle').setAttribute('aria-pressed', String(c.f)); $('fireState').textContent = c.f ? 'on' : 'off';
    $('brushDot').style.background = Lamp.displayColor(c);
    drawWheel();
  }
  function setBrush(color) { state.brush = { ...color }; state.hsv = Lamp.toHSV(color); syncBrushUI(); }
  function updateHSV() { Object.assign(state.brush, Lamp.hsv(state.hsv.h, state.hsv.s, state.hsv.v)); setTool('paint'); syncBrushUI(); }
  function initWheel() {
    const canvas = $('colorWheel');
    let pointer = null;
    const choose = event => {
      const rect = canvas.getBoundingClientRect();
      const dx = (event.clientX - rect.left) / rect.width - .5, dy = (event.clientY - rect.top) / rect.height - .5;
      state.hsv.h = (Math.atan2(dy, dx) * 180 / Math.PI + 360) % 360;
      state.hsv.s = Math.min(1, Math.hypot(dx, dy) / (.5 - 9 / canvas.width));
      updateHSV();
    };
    canvas.addEventListener('pointerdown', event => {
      if (pointer !== null || (event.pointerType === 'mouse' && event.button !== 0)) return;
      pointer = event.pointerId; canvas.setPointerCapture(pointer); choose(event); event.preventDefault();
    });
    canvas.addEventListener('pointermove', event => { if (pointer === event.pointerId) choose(event); });
    ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(type => canvas.addEventListener(type, event => {
      if (pointer === event.pointerId) pointer = null;
      if (canvas.hasPointerCapture(event.pointerId)) canvas.releasePointerCapture(event.pointerId);
    }));
    canvas.addEventListener('keydown', event => {
      if (!event.key.startsWith('Arrow')) return;
      event.preventDefault();
      if (event.key === 'ArrowLeft') state.hsv.h = (state.hsv.h + 355) % 360;
      if (event.key === 'ArrowRight') state.hsv.h = (state.hsv.h + 5) % 360;
      if (event.key === 'ArrowUp') state.hsv.s = Math.min(1, state.hsv.s + .05);
      if (event.key === 'ArrowDown') state.hsv.s = Math.max(0, state.hsv.s - .05);
      updateHSV();
    });
    const colors = [
      ['Albercoc', 20, .7], ['Corall', 355, .7], ['Espígol', 265, .45], ['Blau', 220, .75],
      ['Aigua', 175, .7], ['Molsa', 85, .65], ['Ambre', 42, .8], ['Blanc', 0, 0],
    ];
    for (const [name, h, s] of colors) {
      const button = document.createElement('button');
      button.className = 'swatch'; button.setAttribute('aria-label', name); button.title = name;
      const dot = document.createElement('span'); dot.style.setProperty('--swatch', Lamp.rgbHex(Lamp.hsv(h, s, 235))); button.append(dot);
      button.addEventListener('click', () => { state.hsv.h = h; state.hsv.s = s; updateHSV(); });
      $('swatches').append(button);
    }
    syncBrushUI();
  }

  const inspirations = [
    { name: 'Alba lenta', subtitle: 'CORALL / AMBRE', outer: [18, 5, 4, 0], inner: [18, 10, 2, 2] },
    { name: 'Hora blava', subtitle: 'BLAU / ESPÍGOL', outer: [2, 5, 22, 0], inner: [14, 6, 21, 0] },
    { name: 'Bosc endins', subtitle: 'MOLSA / LLIMA', outer: [4, 12, 7, 0], inner: [12, 15, 3, 1] },
    { name: 'Brasa suau', subtitle: 'AMBRE / CREPITACIÓ', outer: [15, 3, 0, 0], inner: [20, 7, 1, 0], fire: true },
  ].map(p => ({ ...p, builtin: true, scene: Array.from({ length: 144 }, (_, i) => {
    const c = i < 72 ? p.outer : p.inner;
    return { r: c[0], g: c[1], b: c[2], w: c[3], f: Boolean(p.fire && i >= 72) };
  }) }));
  function sceneCard(preset, ready = true) {
    const button = document.createElement('button'); button.className = 'scene-card'; button.disabled = !ready;
    button.setAttribute('aria-label', `Obre l’escena ${preset.name}`);
    const canvas = document.createElement('canvas'); canvas.width = 320; canvas.height = 320; canvas.setAttribute('aria-hidden', 'true');
    const name = document.createElement('span'); name.className = 'scene-name'; name.textContent = preset.name;
    const meta = document.createElement('span'); meta.className = 'scene-meta mono';
    const subtitle = document.createElement('span'); subtitle.textContent = ready ? preset.subtitle || 'DESADA AL LLUM' : 'CARREGANT…';
    const arrow = document.createElement('span'); arrow.textContent = '↗'; meta.append(subtitle, arrow); button.append(canvas, name, meta);
    if (ready) drawScene(canvas, preset.scene);
    button.addEventListener('click', () => openScene(preset));
    return { button, canvas, subtitle };
  }
  async function loadScenes(refresh = false) {
    const run = ++state.galleryRun;
    if (!state.online) { if (!state.scenesLoaded) $('sceneCount').textContent = 'Connexió pendent'; return; }
    if (!state.scenesLoaded || refresh) {
      $('sceneCount').textContent = 'Llegint…';
      const names = await api('/api/presets/list', { json: true });
      if (!Array.isArray(names) || !names.every(n => typeof n === 'string')) throw new Error('No es pot llegir la col·lecció.');
      state.names = names.sort((a, b) => a.localeCompare(b, 'ca'));
      state.scenesLoaded = true;
      if (refresh) state.sceneCache.clear();
    }
    if (run !== state.galleryRun) return;
    $('sceneCount').textContent = String(state.names.length).padStart(2, '0');
    $('emptyScenes').hidden = state.names.length > 0;
    const cards = state.names.map(name => {
      const preset = state.sceneCache.get(name) || { name };
      return { name, preset, ...sceneCard(preset, Boolean(preset.scene)) };
    });
    $('savedScenes').replaceChildren(...cards.map(card => card.button));
    // Small sequential reads avoid putting the whole scene collection in ESP RAM.
    for (const card of cards) {
      if (run !== state.galleryRun || state.view !== 'escenes') return;
      if (card.preset.scene) continue;
      try {
        const data = await api('/api/presets/get?name=' + encodeURIComponent(card.name), { json: true });
        card.preset.scene = Lamp.decode(data);
        state.sceneCache.set(card.name, card.preset);
        drawScene(card.canvas, card.preset.scene); card.subtitle.textContent = 'DESADA AL LLUM'; card.button.disabled = false;
      } catch (error) {
        card.subtitle.textContent = 'NO DISPONIBLE · ↻';
        if (!state.online) return;
      }
    }
  }
  function openScene(preset) {
    state.selected = preset;
    $('sceneDialogTitle').textContent = preset.name;
    $('sceneDetail').textContent = preset.scene.some(c => c.f) ? 'Dos anells · RGBW · crepitació' : 'Dos anells · RGBW · llum contínua';
    $('deletePreset').hidden = Boolean(preset.builtin);
    $('sceneDialog').showModal(); requestRender();
  }
  async function applyScene(scene, name) {
    const payload = Lamp.encode(scene), signature = Lamp.signature(scene);
    await api('/api/led', { method: 'POST', body: payload });
    state.applied = signature;
    syncDraftUI();
    $('sceneDialog').close();
    toast(name ? `«${name}» aplicada al llum.` : 'Escena aplicada al llum.');
    await poll();
  }
  async function saveScene() {
    const name = $('presetName').value.trim();
    if (!name || [...name].length > 20 || /[\x00-\x1f\x7f/\\]/.test(name)) { toast('Escriu un nom d’1 a 20 caràcters, sense barres.', true); $('presetName').focus(); return; }
    // Check the current collection, including edits from another phone.
    const names = await api('/api/presets/list', { json: true });
    if (names.includes(name) && !await confirmAction('Substitueix l’escena?', `Ja hi ha una escena amb el nom «${name}». Vols desar-hi aquest disseny?`, 'Substitueix')) return;
    const scene = Lamp.clone(state.draft), signature = Lamp.signature(scene);
    await api('/api/presets/save?name=' + encodeURIComponent(name), { method: 'POST', body: Lamp.encode(scene) });
    state.name = name; state.baseline = signature; state.savedSignature = signature; state.savedName = name; state.scenesLoaded = false;
    state.sceneCache.delete(name);
    syncDraftUI(); toast(`«${name}» desada a la col·lecció.`);
  }

  function bindControls() {
    document.querySelectorAll('[data-view]').forEach(button => button.addEventListener('click', () => navigate(button.dataset.view)));
    document.querySelectorAll('[data-ring]').forEach(button => button.addEventListener('click', () => setRing(button.dataset.ring)));
    document.querySelectorAll('[data-tool]').forEach(button => button.addEventListener('click', () => setTool(button.dataset.tool)));
    document.querySelector('.brand').addEventListener('click', event => { event.preventDefault(); navigate('llum'); });
    $('connection').onclick = $('retryConnection').onclick = () => poll().then(() => { if (state.view === 'escenes') loadScenes().catch(error => toast(error.message, true)); });
    $('autoButton').onclick = () => action(null, async () => { await api('/api/mode?set=astro', { method: 'POST' }); await poll(); toast('El llum torna a seguir el cel.'); });
    $('simulationForm').onsubmit = event => {
      event.preventDefault();
      const date = new Date($('simDate').value);
      if (!Number.isFinite(date.getTime()) || localInput(date) !== $('simDate').value) { toast('Aquesta hora no existeix a la zona horària del mòbil.', true); return; }
      action(event.submitter, async () => { await api('/api/simulate?ts=' + Math.floor(date.getTime() / 1000), { method: 'POST' }); await poll(); toast('Moment simulat al llum.'); });
    };
    $('undo').onclick = () => restoreHistory(-1); $('redo').onclick = () => restoreHistory(1);
    $('brightness').oninput = event => { state.hsv.v = Number(event.target.value); updateHSV(); };
    $('hue').oninput = event => { state.hsv.h = Number(event.target.value); updateHSV(); };
    $('saturation').oninput = event => { state.hsv.s = Number(event.target.value) / 100; updateHSV(); };
    $('whiteChannel').oninput = event => { state.brush.w = Number(event.target.value); setTool('paint'); syncBrushUI(); };
    $('whiteExact').onchange = event => {
      const value = Number(event.target.value);
      if (!event.target.value || !Number.isInteger(value) || value < 0 || value > 255) { toast('El canal W ha d’estar entre 0 i 255.', true); syncBrushUI(); return; }
      state.brush.w = value; syncBrushUI();
    };
    $('hexColor').onchange = event => {
      const value = event.target.value;
      if (!/^#[\da-f]{6}$/i.test(value)) { toast('Fes servir un color com #0D0703.', true); syncBrushUI(); return; }
      setBrush({ ...state.brush, r: parseInt(value.slice(1, 3), 16), g: parseInt(value.slice(3, 5), 16), b: parseInt(value.slice(5, 7), 16) }); setTool('paint');
    };
    $('fireToggle').onclick = () => { state.brush.f = !state.brush.f; setTool('paint'); syncBrushUI(); };
    $('fillRing').onclick = () => fill(selectedRings()); $('clearRing').onclick = () => fill(selectedRings(), true);
    $('fillAll').onclick = () => fill([0, 1]); $('clearAll').onclick = () => fill([0, 1], true);
    $('readLamp').onclick = () => action($('readLamp'), async () => {
      const data = await api('/api/status', { json: true });
      await replaceDraft(Lamp.decode(data));
    });
    $('presetName').oninput = syncDraftUI;
    $('saveForm').onsubmit = event => { event.preventDefault(); action($('saveScene'), saveScene); };
    $('applyScene').onclick = () => action($('applyScene'), () => applyScene(Lamp.clone(state.draft), $('presetName').value.trim()));
    $('refreshScenes').onclick = () => action($('refreshScenes'), () => loadScenes(true));
    $('createScene').onclick = () => { navigate('estudi'); };
    $('closeScene').onclick = () => $('sceneDialog').close();
    $('sceneDialog').addEventListener('close', requestRender);
    $('applyPreset').onclick = () => action($('applyPreset'), () => applyScene(state.selected.scene, state.selected.name));
    $('editPreset').onclick = async () => {
      $('editPreset').disabled = true;
      try {
        if (await replaceDraft(state.selected.scene, state.selected.name, !state.selected.builtin)) $('sceneDialog').close();
      } finally { $('editPreset').disabled = false; }
    };
    $('deletePreset').onclick = () => action($('deletePreset'), async () => {
      const name = state.selected.name;
      if (!await confirmAction('Elimina aquesta escena?', `«${name}» s’eliminarà de la col·lecció del llum. Aquesta acció no es pot desfer.`, 'Elimina')) return;
      await api('/api/presets/delete?name=' + encodeURIComponent(name), { method: 'POST' });
      $('sceneDialog').close(); state.scenesLoaded = false; state.sceneCache.delete(name);
      if (state.savedName === name) { state.savedSignature = ''; state.savedName = ''; syncDraftUI(); }
      await loadScenes(); toast('Escena eliminada.');
    });
    document.addEventListener('visibilitychange', () => {
      if (document.hidden) { cancelAnimationFrame(renderFrame); renderFrame = null; clearTimeout(pollTimer); }
      else { requestRender(); poll(); schedulePoll(); }
    });
    window.addEventListener('hashchange', () => navigate(location.hash.slice(1)));
    document.addEventListener('keydown', event => {
      if (state.view !== 'estudi' || /INPUT|TEXTAREA|SELECT/.test(event.target.tagName) || $('confirmDialog').open || $('sceneDialog').open) return;
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'z') { event.preventDefault(); restoreHistory(event.shiftKey ? 1 : -1); }
    });
  }
  function init() {
    let restored = false;
    state.baseline = Lamp.signature(state.draft);
    try {
      const saved = JSON.parse(localStorage.getItem(STORAGE_KEY));
      if (saved) {
        state.draft = Lamp.decode(saved); state.name = String(saved.name || '').slice(0, 20);
        $('presetName').value = state.name;
        state.baseline = typeof saved.baseline === 'string' ? saved.baseline : Lamp.signature(state.draft);
        state.savedSignature = typeof saved.savedSignature === 'string' ? saved.savedSignature : '';
        state.savedName = typeof saved.savedName === 'string' ? saved.savedName : '';
        restored = true;
      }
    } catch { /* A corrupt or unavailable local draft must not block the lamp UI. */ }
    bindControls(); initEditor(); initWheel(); checkpoint();
    for (const preset of inspirations) $('inspirationScenes').append(sceneCard(preset).button);
    $('simDate').value = localInput(new Date());
    $('timezone').textContent = '· ' + Intl.DateTimeFormat().resolvedOptions().timeZone;
    updateDeviceButtons(); navigate(location.hash.slice(1));
    poll().then(() => { if (state.view === 'escenes') loadScenes().catch(error => toast(error.message, true)); });
    schedulePoll();
    if (restored && state.view === 'estudi') toast('Hem recuperat el teu esborrany.');
  }
  init();
})();
