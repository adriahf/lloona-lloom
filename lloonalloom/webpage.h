#ifndef WEBPAGE_H
#define WEBPAGE_H

#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"=====(
<!DOCTYPE html>
<html lang="ca">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Control Lloona-Lloom</title>
    <style>
        :root { --primary: #0d6efd; --bg-dark: #121212; --card-bg: #1e1e1e; }
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: var(--bg-dark); color: #fff; padding: 20px; margin: 0; user-select: none; }
        .card { background: var(--card-bg); padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        h3 { margin-top: 0; border-bottom: 1px solid #333; padding-bottom: 10px; }
        button { padding: 10px; border-radius: 8px; border: none; font-size: 14px; font-weight: bold; cursor: pointer; flex: 1; margin: 0 4px; }
        .btn-active { background: var(--primary); color: white; }
        .btn-inactive { background: #333; color: #aaa; }
        .btn-primary { background: #198754; color: white; width: 100%; padding: 15px; font-size: 16px; margin: 15px 0 0 0; }
        .btn-danger { background: #dc3545; color: white; width: 100%; padding: 12px; font-size: 14px; margin: 10px 0 0 0; }
        .row { display: flex; justify-content: space-between; margin-bottom: 15px; margin-left: -4px; margin-right: -4px; }
        label { display: block; margin-bottom: 8px; font-size: 14px; font-weight: 500; color: #ddd;}
        input[type=range] { width: 100%; margin-bottom: 15px; }
        input[type=datetime-local], input[type=text] { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #444; background: #222; color: #fff; font-size: 16px; box-sizing: border-box; margin-bottom: 15px;}
        .disabled { display: none; }
        
        /* CANVAS DE DIBUIX - Gris més clar (#444) per imitar millor l'alumini mantinguent contrast */
        .canvas-container { width: 100%; max-width: 500px; margin: 0 auto; aspect-ratio: 1/1; background: #444444; border-radius: 50%; border: 3px solid #666; position: relative; overflow: hidden; touch-action: none; box-shadow: inset 0 0 30px rgba(0,0,0,0.6); }
        canvas { width: 100%; height: 100%; display: block; }
        
        /* CAIXA D'EINES I HISTÒRIAL */
        .toolbar { display: flex; gap: 6px; margin-bottom: 20px; background: #2a2a2a; padding: 8px; border-radius: 12px; align-items: stretch; }
        .tool-label { flex: 1; text-align: center; background: #333; padding: 10px 2px; border-radius: 8px; cursor: pointer; transition: 0.2s; border: 2px solid transparent; font-size: 13px; font-weight: bold;}
        .tool-label input { display: none; }
        .tool-label:has(input:checked) { background: #444; border-color: var(--primary); color: #fff; }
        .action-btn { background: #333; color: #fff; border: 1px solid #444; border-radius: 8px; width: 42px; cursor: pointer; font-size: 16px; display: flex; align-items: center; justify-content: center; transition: 0.2s; }
        .action-btn:active { background: #555; }
        .action-btn:disabled { opacity: 0.3; cursor: not-allowed; }
        
        /* PALETA DE COLORS PERSONALITZADA COMPACTA (CSS GRID) */
        .palette-box { background: #252525; padding: 15px; border-radius: 12px; border: 1px solid #444; margin-bottom: 20px; }
        .palette-grid { display: grid; grid-template-columns: 120px 1fr; gap: 15px; align-items: center; }
        .wheel-column { display: flex; flex-direction: column; align-items: center; }
        .sliders-column { display: flex; flex-direction: column; gap: 10px; }
        .wheel-canvas { width: 120px; height: 120px; border-radius: 50%; cursor: crosshair; touch-action: none; box-shadow: 0 0 10px rgba(0,0,0,0.5); }
        .color-preview { width: 30px; height: 30px; border-radius: 50%; border: 2px solid #555; background: #ff8800; margin-top: 10px; }
        .value-badge { float: right; background: #111; padding: 2px 8px; border-radius: 6px; font-family: monospace; font-size: 12px; color: #0d6efd; }
        
        /* BOTÓ DE FOC TOGGLE (Substitueix el checkbox antic) */
        .fire-toggle { background: #333; border: 1px solid #555; color: #aaa; padding: 10px; border-radius: 8px; cursor: pointer; text-align: center; font-size: 14px; font-weight: bold; transition: 0.2s; user-select: none; }
        .fire-toggle.active { background: #4a1500; border-color: #ff4500; color: #ff8800; }
        
        /* DRECERES */
        .quick-actions { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-top: 15px; }
        .quick-btn { background: #333; color: #ddd; padding: 12px; border-radius: 8px; border: 1px solid #444; font-size: 13px; font-weight: bold; }
        .quick-btn:active { background: #555; }
        
        #astroStatusLabel { background: #2c2c2c; padding: 10px; border-radius: 8px; font-weight: bold; font-size: 15px; text-align: center; margin-bottom: 20px; border: 1px solid #444; }
    </style>
</head>
<body>
    <h2 style="text-align: center; margin-bottom: 15px;">🌒 Lloona-Lloom</h2>
    
    <div id="astroStatusLabel">Connectant al maquinari...</div>
    
    <div class="card">
        <div class="row" style="margin-bottom:0;">
            <button id="btnAstro" class="btn-active" onclick="setMode('astro')">Temps Real</button>
            <button id="btnSim" class="btn-inactive" onclick="setMode('simulacio')">Simulació</button>
            <button id="btnManual" class="btn-inactive" onclick="setMode('manual')">Disseny</button>
        </div>
    </div>

    <!-- PANELL SIMULACIÓ -->
    <div id="simControls" class="card disabled">
        <h3>Viatge en el Temps</h3>
        <p style="font-size: 13px; color: #aaa; margin-top: 0;">Tria una data per renderitzar l'estat exacte d'aquell moment.</p>
        <input type="datetime-local" id="simDate">
        <button class="btn-primary" onclick="sendSimulation()">Simular</button>
    </div>

    <!-- PANELL MANUAL (ESTUDI DE DISSENY) -->
    <div id="manualControls" class="card disabled">
        
        <h3 style="margin-bottom: 15px;">💾 Configuracions Guardades</h3>
        <div style="display: flex; gap: 10px; margin-bottom: 15px;">
            <input type="text" id="presetName" maxlength="20" placeholder="Nom (ex: Posta de Sol)" style="margin: 0; flex: 1;">
            <button class="btn-primary" style="margin: 0; width: auto; padding: 10px 15px; background: #0d6efd;" onclick="savePreset()">Guardar</button>
        </div>
        <div id="presetsList" style="display: flex; flex-direction: column; gap: 10px;"></div>

        <hr style="border-color: #333; margin: 25px 0;">

        <!-- CAIXA D'EINES AMB HISTÒRIAL -->
        <h3>Caixa d'Eines</h3>
        <div class="toolbar">
            <label class="tool-label">
                <input type="radio" name="tool" value="paint" checked onchange="updateActiveTool()">
                🖌️ Pintar
            </label>
            <label class="tool-label">
                <input type="radio" name="tool" value="erase" onchange="updateActiveTool()">
                🧽 Goma
            </label>
            <label class="tool-label">
                <input type="radio" name="tool" value="picker" onchange="updateActiveTool()">
                💧 Copiar
            </label>
            <div style="width: 2px; background: #444; margin: 0 2px; border-radius: 2px;"></div>
            <button class="action-btn" id="btnUndo" onclick="undoState()" disabled title="Desfer">↩️</button>
            <button class="action-btn" id="btnRedo" onclick="redoState()" disabled title="Refer">↪️</button>
        </div>

        <!-- PALETA DE COLORS PRO COMPACTA -->
        <div class="palette-box">
            <div class="palette-grid">
                <div class="wheel-column">
                    <canvas id="colorWheel" class="wheel-canvas" width="120" height="120"></canvas>
                    <div id="colorPreview" class="color-preview"></div>
                </div>
                <div class="sliders-column">
                    <div style="margin-bottom: 2px;">
                        <label style="margin-bottom: 4px; font-size: 13px;">Brillantor: <span id="briVal" class="value-badge">100%</span></label>
                        <input type="range" id="brightness" min="0" max="100" value="100" oninput="updateColorFromSliders()" style="margin-bottom: 0;">
                    </div>
                    <div style="margin-bottom: 2px;">
                        <label style="margin-bottom: 4px; font-size: 13px;">Blanc (W): <span id="wVal" class="value-badge">0</span></label>
                        <input type="range" id="whiteChannel" min="0" max="255" value="0" oninput="updateColorFromSliders()" style="margin-bottom: 0;">
                    </div>
                    <!-- BOTÓ TOGGLE COMPACTE DE FOC -->
                    <div id="fireToggleBtn" class="fire-toggle" onclick="toggleFireBrush()">
                        🔥 Foc: OFF
                    </div>
                </div>
            </div>
        </div>

        <!-- CANVAS D'ESBORRANY -->
        <div class="canvas-container" id="canvasContainer">
            <canvas id="ledCanvas" width="500" height="500"></canvas>
        </div>

        <div class="quick-actions">
            <button class="quick-btn" onclick="fillRing(0)">🪣 Omplir Exterior</button>
            <button class="quick-btn" onclick="fillRing(1)">🪣 Omplir Interior</button>
            <button class="quick-btn" onclick="fillAll()">🪣 Omplir Tot</button>
            <button class="quick-btn" onclick="eraseAll()" style="color: #ff6b6b; border-color: #5c2020;">🗑️ Netejar Llenç</button>
        </div>

        <button class="btn-primary" onclick="commitChanges()">🚀 Sincronitzar amb la Làmpada</button>
        <button class="btn-danger" onclick="discardChanges()">📥 Llegir estat actual de la làmpada</button>
        
    </div>

    <script>
        // --- CONSTANTS I ESTRUCTURA BASE ---
        const TOTAL_LEDS = 144;
        const NUM_OUTER = 72;
        const NUM_INNER = 72;
        const VISUAL_BOOST = 4; 

        let leds = []; 
        let activeTool = 'paint'; 
        let baseColorRGB = { r: 255, g: 136, b: 0 }; 
        let activeColor = { r: 255, g: 136, b: 0, w: 0 }; 
        let isFireBrush = false; // Substitueix l'antic checkbox

        // Sistema d'Històrial
        let historyStack = [];
        let historyIndex = -1;
        let hasUnsavedChanges = false;

        let canvas, ctx, wheelCanvas, wheelCtx;
        let isDragging = false;
        let animationFrameId = null;

        // --- SISTEMA D'HISTÒRIAL (Undo/Redo) ---
        function saveState() {
            if (historyIndex < historyStack.length - 1) {
                historyStack = historyStack.slice(0, historyIndex + 1);
            }
            historyStack.push(JSON.parse(JSON.stringify(leds)));
            if (historyStack.length > 20) { // Límit de memòria
                historyStack.shift();
            } else {
                historyIndex++;
            }
            updateUndoRedoButtons();
        }

        function undoState() {
            if (historyIndex > 0) {
                historyIndex--;
                leds = JSON.parse(JSON.stringify(historyStack[historyIndex]));
                requestRender();
                updateUndoRedoButtons();
            }
        }

        function redoState() {
            if (historyIndex < historyStack.length - 1) {
                historyIndex++;
                leds = JSON.parse(JSON.stringify(historyStack[historyIndex]));
                requestRender();
                updateUndoRedoButtons();
            }
        }

        function updateUndoRedoButtons() {
            document.getElementById('btnUndo').disabled = historyIndex <= 0;
            document.getElementById('btnRedo').disabled = historyIndex >= historyStack.length - 1;
        }

        // --- RODA DE COLORS I BOTÓ FOC ---
        function toggleFireBrush() {
            isFireBrush = !isFireBrush;
            let btn = document.getElementById('fireToggleBtn');
            if(isFireBrush) {
                btn.classList.add('active');
                btn.innerText = '🔥 Foc: ON';
            } else {
                btn.classList.remove('active');
                btn.innerText = '🔥 Foc: OFF';
            }
            // Si estem esborrant o copiant i toquem el foc, passem a pintar per comoditat
            if(activeTool === 'erase' || activeTool === 'picker') {
                document.querySelector('input[value="paint"]').checked = true;
                updateActiveTool();
            }
        }

        function initColorWheel() {
            wheelCanvas = document.getElementById('colorWheel');
            wheelCtx = wheelCanvas.getContext('2d', { willReadFrequently: true });
            let radius = wheelCanvas.width / 2;
            let cx = radius, cy = radius;

            for(let angle = 0; angle < 360; angle++) {
                let startAngle = (angle - 1) * Math.PI / 180;
                let endAngle = (angle + 1) * Math.PI / 180;
                wheelCtx.beginPath();
                wheelCtx.moveTo(cx, cy);
                wheelCtx.arc(cx, cy, radius, startAngle, endAngle);
                wheelCtx.closePath();
                let grad = wheelCtx.createRadialGradient(cx, cy, 0, cx, cy, radius);
                grad.addColorStop(0, 'white');
                grad.addColorStop(1, `hsl(${angle}, 100%, 50%)`);
                wheelCtx.fillStyle = grad;
                wheelCtx.fill();
            }

            wheelCanvas.addEventListener('pointerdown', handleWheelPointer);
            wheelCanvas.addEventListener('pointermove', (e) => { if (e.buttons > 0) handleWheelPointer(e); });
        }

        function handleWheelPointer(e) {
            let rect = wheelCanvas.getBoundingClientRect();
            let x = (e.clientX - rect.left) * (wheelCanvas.width / rect.width);
            let y = (e.clientY - rect.top) * (wheelCanvas.height / rect.height);
            
            let dx = x - wheelCanvas.width/2;
            let dy = y - wheelCanvas.height/2;
            if (Math.sqrt(dx*dx + dy*dy) > wheelCanvas.width/2) return;

            let pixel = wheelCtx.getImageData(x, y, 1, 1).data;
            if (pixel[3] > 0) {
                baseColorRGB = { r: pixel[0], g: pixel[1], b: pixel[2] };
                updateColorFromSliders();
                
                if(activeTool === 'erase' || activeTool === 'picker') {
                    document.querySelector('input[value="paint"]').checked = true;
                    updateActiveTool();
                }
            }
        }

        function updateColorFromSliders() {
            let bri = parseInt(document.getElementById('brightness').value);
            let w = parseInt(document.getElementById('whiteChannel').value);
            
            activeColor.r = Math.round(baseColorRGB.r * (bri / 100));
            activeColor.g = Math.round(baseColorRGB.g * (bri / 100));
            activeColor.b = Math.round(baseColorRGB.b * (bri / 100));
            activeColor.w = w;

            document.getElementById('briVal').innerText = bri + '%';
            document.getElementById('wVal').innerText = w;
            
            let pR = Math.min(255, (activeColor.r + activeColor.w) * VISUAL_BOOST);
            let pG = Math.min(255, (activeColor.g + activeColor.w) * VISUAL_BOOST);
            let pB = Math.min(255, (activeColor.b + activeColor.w) * VISUAL_BOOST);
            document.getElementById('colorPreview').style.backgroundColor = `rgb(${pR}, ${pG}, ${pB})`;
        }

        function applyEyedropper(ledIndex) {
            let led = leds[ledIndex];
            let maxRGB = Math.max(led.r, led.g, led.b);
            let bri = 0;

            if (maxRGB > 0) {
                bri = Math.round((maxRGB / 255) * 100);
                baseColorRGB.r = Math.round((led.r / maxRGB) * 255);
                baseColorRGB.g = Math.round((led.g / maxRGB) * 255);
                baseColorRGB.b = Math.round((led.b / maxRGB) * 255);
            } else {
                baseColorRGB = { r: 255, g: 255, b: 255 }; 
            }

            document.getElementById('brightness').value = bri;
            document.getElementById('whiteChannel').value = led.w;
            
            // Sincronitza el botó de foc
            if (led.f !== isFireBrush) toggleFireBrush();

            updateColorFromSliders();

            document.querySelector('input[value="paint"]').checked = true;
            updateActiveTool();
        }

        // --- INICIALITZACIÓ CANVAS LÀMPADA ---
        function initLedData() {
            leds = [];
            for(let i = 0; i < NUM_OUTER; i++) {
                let angle = (i * 360 / NUM_OUTER - 90) * Math.PI / 180;
                leds.push({
                    index: i, ring: 0, r: 0, g: 0, b: 0, w: 0, f: false,
                    cx: 250 + 220 * Math.cos(angle), cy: 250 + 220 * Math.sin(angle)
                });
            }
            for(let i = 0; i < NUM_INNER; i++) {
                let angle = (i * 360 / NUM_INNER - 90) * Math.PI / 180;
                leds.push({
                    index: NUM_OUTER + i, ring: 1, r: 0, g: 0, b: 0, w: 0, f: false,
                    cx: 250 + 170 * Math.cos(angle), cy: 250 + 170 * Math.sin(angle)
                });
            }
            
            // Creem l'estat base (buit) a l'historial
            historyStack = [];
            historyIndex = -1;
            saveState(); 
        }

        // --- MOTOR D'ANIMACIÓ (Foc i Pintura fluïda) ---
        function requestRender() {
            if (!animationFrameId) {
                animationFrameId = requestAnimationFrame(renderLoop);
            }
        }

        function renderLoop() {
            renderCanvas();
            if (leds.some(l => l.f)) {
                animationFrameId = requestAnimationFrame(renderLoop);
            } else {
                animationFrameId = null;
            }
        }

        function renderCanvas() {
            if (!ctx) return;
            ctx.clearRect(0, 0, 500, 500);

            // Anells exteriors de suport
            ctx.strokeStyle = '#555'; ctx.lineWidth = 2;
            ctx.beginPath(); ctx.arc(250, 250, 220, 0, 2 * Math.PI); ctx.stroke();
            ctx.beginPath(); ctx.arc(250, 250, 170, 0, 2 * Math.PI); ctx.stroke();

            let t = Date.now(); 

            for (let led of leds) {
                let flicker = 1.0;
                
                if (led.f) {
                    let wave = Math.sin((t / 150) + led.index);
                    flicker = 0.5 + (0.4 * Math.random()) + (0.1 * wave);
                    if (flicker > 1.0) flicker = 1.0;
                }

                let cssR = Math.min(255, (led.r + led.w) * VISUAL_BOOST * flicker);
                let cssG = Math.min(255, (led.g + led.w) * VISUAL_BOOST * flicker);
                let cssB = Math.min(255, (led.b + led.w) * VISUAL_BOOST * flicker);

                ctx.beginPath();
                ctx.arc(led.cx, led.cy, 5, 0, 2 * Math.PI); 
                
                if(led.r === 0 && led.g === 0 && led.b === 0 && led.w === 0) {
                    ctx.fillStyle = '#222'; // LED apagat
                    ctx.strokeStyle = '#444'; ctx.lineWidth = 1;
                    ctx.fill(); ctx.stroke();
                } else {
                    ctx.fillStyle = `rgb(${cssR}, ${cssG}, ${cssB})`; 
                    ctx.fill();
                    
                    if (led.f) {
                        ctx.shadowBlur = 10;
                        ctx.shadowColor = `rgba(255, 69, 0, ${flicker})`;
                        ctx.fill();
                        ctx.shadowBlur = 0; 
                    }
                }
            }
        }

        function initCanvasInteraction() {
            canvas = document.getElementById('ledCanvas');
            ctx = canvas.getContext('2d');
            canvas.oncontextmenu = function(e) { e.preventDefault(); return false; };

            canvas.addEventListener('pointerdown', (e) => {
                isDragging = true;
                hasUnsavedChanges = false;
                handlePointerEvent(e);
                canvas.setPointerCapture(e.pointerId);
                e.preventDefault(); 
            });
            canvas.addEventListener('pointermove', (e) => {
                if (!isDragging) return;
                handlePointerEvent(e);
                e.preventDefault();
            });
            canvas.addEventListener('pointerup', (e) => {
                if (isDragging) {
                    isDragging = false;
                    canvas.releasePointerCapture(e.pointerId);
                    if (hasUnsavedChanges) {
                        saveState(); // Guardem fotograma a l'historial en aixecar el dit
                        hasUnsavedChanges = false;
                    }
                }
            });
        }

        function handlePointerEvent(evt) {
            let rect = canvas.getBoundingClientRect();
            let x = (evt.clientX - rect.left) * (canvas.width / rect.width);
            let y = (evt.clientY - rect.top) * (canvas.height / rect.height);
            let hitRadius = 15;

            for (let led of leds) {
                let dx = x - led.cx; let dy = y - led.cy;
                if (Math.sqrt(dx * dx + dy * dy) < hitRadius) {
                    
                    if (activeTool === 'picker') { applyEyedropper(led.index); isDragging = false; return; } 
                    
                    let tr = (activeTool === 'erase') ? 0 : activeColor.r;
                    let tg = (activeTool === 'erase') ? 0 : activeColor.g;
                    let tb = (activeTool === 'erase') ? 0 : activeColor.b;
                    let tw = (activeTool === 'erase') ? 0 : activeColor.w;
                    let tf = (activeTool === 'paint') ? isFireBrush : false;

                    if (activeTool === 'paint') {
                        if (led.r !== tr || led.g !== tg || led.b !== tb || led.w !== tw || led.f !== tf) {
                            led.r = tr; led.g = tg; led.b = tb; led.w = tw; led.f = tf;
                            hasUnsavedChanges = true;
                            requestRender();
                        }
                    } else if (activeTool === 'erase') {
                        if (led.r !== 0 || led.w !== 0 || led.f !== false) {
                            led.r = 0; led.g = 0; led.b = 0; led.w = 0; led.f = false;
                            hasUnsavedChanges = true;
                            requestRender();
                        }
                    }
                }
            }
        }

        function updateActiveTool() {
            activeTool = document.querySelector('input[name="tool"]:checked').value;
            let canvasEl = document.getElementById('ledCanvas');
            if(activeTool === 'picker') canvasEl.style.cursor = 'cell';
            else if(activeTool === 'erase') canvasEl.style.cursor = 'not-allowed';
            else canvasEl.style.cursor = 'crosshair';
        }

        // --- EINES RÀPIDES (Dreceres) ---
        function fillRing(ringId) {
            let tf = isFireBrush;
            for(let led of leds) {
                if(led.ring === ringId) { led.r = activeColor.r; led.g = activeColor.g; led.b = activeColor.b; led.w = activeColor.w; led.f = tf; }
            }
            requestRender();
            saveState();
        }
        function fillAll() {
            let tf = isFireBrush;
            for(let led of leds) { led.r = activeColor.r; led.g = activeColor.g; led.b = activeColor.b; led.w = activeColor.w; led.f = tf; }
            requestRender();
            saveState();
        }
        function eraseAll(render = true) {
            for(let led of leds) { led.r = 0; led.g = 0; led.b = 0; led.w = 0; led.f = false; }
            if(render) {
                requestRender();
                saveState();
            }
        }

        // --- COMUNICACIÓ AMB L'ESP32 ---
        const toHex = (n) => n.toString(16).padStart(2, '0').toUpperCase();

        async function commitChanges() {
            let extHex = "";
            let intHex = "";
            for(let i=0; i<NUM_OUTER; i++) {
                extHex += toHex(leds[i].r) + toHex(leds[i].g) + toHex(leds[i].b) + toHex(leds[i].w) + (leds[i].f ? "1" : "0");
            }
            for(let i=0; i<NUM_INNER; i++) {
                let idx = NUM_OUTER + i;
                intHex += toHex(leds[idx].r) + toHex(leds[idx].g) + toHex(leds[idx].b) + toHex(leds[idx].w) + (leds[idx].f ? "1" : "0");
            }

            let payload = { ext: extHex, int: intHex };

            try {
                let btn = document.querySelector('button[onclick="commitChanges()"]');
                let originalText = btn.innerText;
                btn.innerText = "Sincronitzant..."; btn.style.opacity = "0.7";

                let res = await fetch('/api/led', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(payload) });
                if (!res.ok) alert("Error de xarxa en sincronitzar.");
                setTimeout(() => { btn.innerText = originalText; btn.style.opacity = "1"; }, 300);
            } catch (e) { alert("No s'ha pogut contactar amb la làmpada."); }
        }

        function discardChanges() {
            fetch('/api/colors').then(res => res.json()).then(data => {
                let extHex = data.ext; let intHex = data.int;
                for (let i = 0; i < NUM_OUTER; i++) {
                    leds[i].r = parseInt(extHex.substring(i*9, i*9+2), 16) || 0;
                    leds[i].g = parseInt(extHex.substring(i*9+2, i*9+4), 16) || 0;
                    leds[i].b = parseInt(extHex.substring(i*9+4, i*9+6), 16) || 0;
                    leds[i].w = parseInt(extHex.substring(i*9+6, i*9+8), 16) || 0;
                    leds[i].f = (extHex.charAt(i*9+8) === '1');
                }
                for (let i = 0; i < NUM_INNER; i++) {
                    let idx = NUM_OUTER + i;
                    leds[idx].r = parseInt(intHex.substring(i*9, i*9+2), 16) || 0;
                    leds[idx].g = parseInt(intHex.substring(i*9+2, i*9+4), 16) || 0;
                    leds[idx].b = parseInt(intHex.substring(i*9+4, i*9+6), 16) || 0;
                    leds[idx].w = parseInt(intHex.substring(i*9+6, i*9+8), 16) || 0;
                    leds[idx].f = (intHex.charAt(i*9+8) === '1');
                }
                requestRender();
                // Reiniciem l'historial un cop agafem l'estat del hardware
                historyStack = [];
                historyIndex = -1;
                saveState();
            }).catch(err => console.error("Error llegint estat", err));
        }

        // --- SISTEMA D'ESTATS I PRESETS ---
        function updateStatusUI() {
            fetch('/api/status').then(response => response.json()).then(data => {
                let lbl = document.getElementById('astroStatusLabel');
                if (data.mode === 'manual') {
                    lbl.innerHTML = '✋ Lloona-Lloom Lliure (Estudi)';
                    lbl.style.borderColor = '#0d6efd';
                } else {
                    lbl.innerHTML = (data.is_daytime ? '☀️ Dia' : '🌙 Nit') + ` &nbsp;|&nbsp; Lluna: ${(data.fraction * 100).toFixed(1)}%`;
                    lbl.style.borderColor = data.is_daytime ? '#f39c12' : '#8e44ad';
                }
                if(data.mode === "manual") changeModeTabs('manual');
                else if(data.mode === "simulacio") changeModeTabs('simulacio');
                else changeModeTabs('astro');
            });
        }

        function changeModeTabs(m) {
            document.getElementById('btnAstro').className = (m === 'astro') ? 'btn-active' : 'btn-inactive';
            document.getElementById('btnSim').className = (m === 'simulacio') ? 'btn-active' : 'btn-inactive';
            document.getElementById('btnManual').className = (m === 'manual') ? 'btn-active' : 'btn-inactive';
            
            document.getElementById('manualControls').classList.add('disabled');
            document.getElementById('simControls').classList.add('disabled');
            
            if(m === 'manual') {
                document.getElementById('manualControls').classList.remove('disabled');
                if(leds.length === 0) {
                    initColorWheel();
                    initLedData(); // Això ja fa saveState()
                    initCanvasInteraction();
                    updateColorFromSliders(); 
                }
                loadPresets(); 
                requestRender(); 
            }
            if(m === 'simulacio') document.getElementById('simControls').classList.remove('disabled');
        }

        function setMode(m) { fetch('/api/mode?set=' + m).then(() => { changeModeTabs(m); updateStatusUI(); }); }
        function sendSimulation() {
            let dateVal = document.getElementById('simDate').value;
            if(!dateVal) return;
            fetch('/api/simulate?ts=' + Math.floor(new Date(dateVal).getTime() / 1000)).then(res => { if(res.ok) updateStatusUI(); });
        }

        function loadPresets() {
            fetch('/api/presets/list').then(res => res.json()).then(names => {
                let html = names.length === 0 ? '<p style="color:#aaa;font-size:13px;text-align:center;">No hi ha dissenys guardats.</p>' : '';
                names.forEach(name => {
                    html += `<div class="preset-item">
                        <span style="font-weight:bold;">${name}</span>
                        <div style="display:flex;gap:8px;">
                            <button style="background:#198754;padding:6px 12px;margin:0;font-size:13px;" onclick="loadPreset('${name}')">▶️ Pujar</button>
                            <button style="background:#dc3545;padding:6px 10px;margin:0;font-size:13px;" onclick="deletePreset('${name}')">🗑️</button>
                        </div>
                    </div>`;
                });
                document.getElementById('presetsList').innerHTML = html;
            });
        }

        function savePreset() {
            let name = document.getElementById('presetName').value.trim();
            if(!name) return alert('Posa un nom al disseny.');
            commitChanges().then(() => {
                fetch('/api/presets/save?name=' + encodeURIComponent(name), { method: 'POST' }).then(res => {
                    if(res.ok) { document.getElementById('presetName').value = ''; loadPresets(); }
                });
            });
        }

        function loadPreset(name) {
            fetch('/api/presets/load?name=' + encodeURIComponent(name), { method: 'POST' }).then(res => {
                if(res.ok) { setMode('manual'); setTimeout(discardChanges, 150); }
            });
        }

        function deletePreset(name) {
            if(confirm('Segur que vols esborrar: ' + name + '?')) {
                fetch('/api/presets/delete?name=' + encodeURIComponent(name), { method: 'POST' }).then(res => { if(res.ok) loadPresets(); });
            }
        }

        window.onload = updateStatusUI;
    </script>
</body>
</html>
)=====";

#endif