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
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #121212; color: #fff; padding: 20px; margin: 0; }
        .card { background: #1e1e1e; padding: 20px; border-radius: 12px; margin-bottom: 20px; box-shadow: 0 4px 6px rgba(0,0,0,0.3); }
        h3 { margin-top: 0; border-bottom: 1px solid #333; padding-bottom: 10px; }
        button { padding: 10px; border-radius: 8px; border: none; font-size: 14px; font-weight: bold; cursor: pointer; flex: 1; margin: 0 4px; }
        .btn-active { background: #0d6efd; color: white; }
        .btn-inactive { background: #333; color: #aaa; }
        .btn-send { background: #198754; color: white; width: 100%; margin-top: 15px; font-size: 16px; padding: 15px; margin-left: 0; margin-right: 0;}
        .row { display: flex; justify-content: space-between; margin-bottom: 15px; margin-left: -4px; margin-right: -4px; }
        label { display: block; margin-bottom: 8px; font-size: 15px; }
        input[type=range] { width: 100%; margin-bottom: 15px; }
        input[type=datetime-local], input[type=text] { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #444; background: #222; color: #fff; font-size: 16px; box-sizing: border-box; margin-bottom: 15px;}
        .disabled { display: none; }
        .value-badge { float: right; background: #333; padding: 2px 8px; border-radius: 12px; font-size: 13px; font-family: monospace; }
        #colorR { accent-color: #ff4d4d; }
        #colorG { accent-color: #4dff4d; }
        #colorB { accent-color: #4d4dff; }
        #colorW { accent-color: #ffffff; }
        .slider-group { display: flex; align-items: center; gap: 10px; margin-bottom: 15px; }
        .slider-group input[type=range] { flex-grow: 1; margin: 0; }
        .btn-step { width: 42px; height: 42px; border-radius: 8px; border: none; background: #444; color: white; font-size: 24px; font-weight: bold; cursor: pointer; display: flex; align-items: center; justify-content: center; line-height: 1; padding-bottom: 4px; }
        .btn-step:active { background: #666; }
        #astroStatusLabel { background: #2c2c2c; padding: 10px; border-radius: 8px; font-weight: bold; font-size: 15px; text-align: center; margin-bottom: 20px; border: 1px solid #444; }
        .fire-box { background: #3a1c0d; border: 1px solid #ff4500; padding: 12px; border-radius: 8px; margin-top: 15px; }
        .preset-item { display: flex; justify-content: space-between; align-items: center; background: #2a2a2a; padding: 10px 15px; border-radius: 8px; }
    </style>
</head>
<body>
    <h2 style="text-align: center; margin-bottom: 15px;">🌒 Lloona-Lloom UI</h2>
    
    <div id="astroStatusLabel">Cargant estat...</div>
    
    <div class="card">
        <h3>Mode de Funcionament</h3>
        <div class="row">
            <button id="btnAstro" class="btn-active" onclick="setMode('astro')">Temps Real</button>
            <button id="btnSim" class="btn-inactive" onclick="setMode('simulacio')">Simulació</button>
            <button id="btnManual" class="btn-inactive" onclick="setMode('manual')">Manual</button>
        </div>
    </div>

    <!-- Panell de Simulació Astronòmica -->
    <div id="simControls" class="card disabled">
        <h3>Viatge en el Temps</h3>
        <p style="font-size: 13px; color: #aaa; margin-top: 0;">Tria una data i hora per renderitzar l'estat exacte d'aquell moment.</p>
        <label>Data i Hora Local:</label>
        <input type="datetime-local" id="simDate">
        <button class="btn-send" onclick="sendSimulation()">Simular</button>
    </div>

    <!-- Panell de Control Manual de LEDs -->
    <div id="manualControls" class="card disabled">
        
        <h3>💾 Configuracions Guardades</h3>
        <div style="display: flex; gap: 10px; margin-bottom: 15px;">
            <input type="text" id="presetName" maxlength="20" placeholder="Nom per guardar (ex: Posta de Sol)" style="margin: 0; flex: 1;">
            <button class="btn-send" style="margin: 0; width: auto; padding: 10px 15px; background: #0d6efd;" onclick="savePreset()">Guardar</button>
        </div>
        <div id="presetsList" style="display: flex; flex-direction: column; gap: 10px;">
            <!-- La llista es generarà des de JavaScript -->
        </div>

        <hr style="border-color: #333; margin: 25px 0;">

        <h3>Selecció de LEDs</h3>
        <label>
            <input type="radio" name="ring" value="0" checked onchange="updateMaxId()"> Corona Exterior (0-70)
        </label>
        <label style="margin-top: 10px;">
            <input type="radio" name="ring" value="1" onchange="updateMaxId()"> Corona Interior (0-71)
        </label>
        
        <hr style="border-color: #333; margin: 15px 0;">
        
        <label>
            <input type="checkbox" id="allLeds" checked onchange="toggleSlider()"> Aplicar a tota la corona
        </label>

        <div id="fireControl" class="fire-box">
            <label style="margin: 0; color: #ffa07a; font-weight: bold;">
                <input type="checkbox" id="fireEffect"> 🔥 Activar Efecte Foc (Crepitació)
            </label>
            <p style="font-size: 12px; margin: 5px 0 0 0; color: #ddd; font-weight: normal;">Pampallugueja modulant la intensitat dels colors manuals actuals. S'aplicarà quan cliquis el botó "Aplicar Color".</p>
        </div>
        
        <div id="colorControlsWrapper">
            <div style="margin-top: 15px;">
                <label>ID del LED: <span id="ledIdVal" class="value-badge">Tots</span></label>
                <div class="slider-group">
                    <button class="btn-step" onclick="stepSlider('ledId', -1)">-</button>
                    <input type="range" id="ledId" min="0" max="70" value="0" disabled oninput="updateLEDUI()">
                    <button class="btn-step" onclick="stepSlider('ledId', 1)">+</button>
                </div>
            </div>
            
            <h3 style="margin-top: 20px;">Control RGBW</h3>
            
            <label>Vermell (R): <span id="rVal" class="value-badge">0</span></label>
            <div class="slider-group">
                <button class="btn-step" onclick="stepSlider('colorR', -1)">-</button>
                <input type="range" id="colorR" min="0" max="255" value="0" oninput="updateColorUI()">
                <button class="btn-step" onclick="stepSlider('colorR', 1)">+</button>
            </div>
            
            <label>Verd (G): <span id="gVal" class="value-badge">0</span></label>
            <div class="slider-group">
                <button class="btn-step" onclick="stepSlider('colorG', -1)">-</button>
                <input type="range" id="colorG" min="0" max="255" value="0" oninput="updateColorUI()">
                <button class="btn-step" onclick="stepSlider('colorG', 1)">+</button>
            </div>
            
            <label>Blau (B): <span id="bVal" class="value-badge">0</span></label>
            <div class="slider-group">
                <button class="btn-step" onclick="stepSlider('colorB', -1)">-</button>
                <input type="range" id="colorB" min="0" max="255" value="0" oninput="updateColorUI()">
                <button class="btn-step" onclick="stepSlider('colorB', 1)">+</button>
            </div>
            
            <label>Blanc Pur (W): <span id="wVal" class="value-badge">0</span></label>
            <div class="slider-group">
                <button class="btn-step" onclick="stepSlider('colorW', -1)">-</button>
                <input type="range" id="colorW" min="0" max="255" value="0" oninput="updateColorUI()">
                <button class="btn-step" onclick="stepSlider('colorW', 1)">+</button>
            </div>
            
            <button class="btn-send" onclick="sendColorData()">Aplicar Color</button>
        </div>
    </div>

    <script>
        // --- MEMÒRIA D'ESTAT (FRONTEND) ---
        let fireExtOn = false;
        let fireIntOn = false;

        // Aquesta és la clau de la coherència visual.
        // Aquí s'hi guardarà la última configuració que hagis ENVIAT al maquinari per a cada anell.
        let ringState = {
            "0": { r: 0, g: 0, b: 0, w: 0, id: 0, all: true },
            "1": { r: 0, g: 0, b: 0, w: 0, id: 0, all: true }
        };

        function updateStatusUI() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    let lbl = document.getElementById('astroStatusLabel');
                    if (data.mode === 'manual') {
                        lbl.innerHTML = '✋ Control Manual Actiu';
                        lbl.style.borderColor = '#0d6efd';
                    } else {
                        let timeStr = data.is_daytime ? '☀️ Dia' : '🌙 Nit';
                        let pct = (data.fraction * 100).toFixed(1);
                        lbl.innerHTML = `${timeStr} &nbsp;|&nbsp; Lluna: ${pct}%`;
                        lbl.style.borderColor = data.is_daytime ? '#f39c12' : '#8e44ad';
                    }

                    fireExtOn = data.fire_ext_on;
                    fireIntOn = data.fire_int_on;
                    
                    let currentRing = document.querySelector('input[name="ring"]:checked').value;
                    document.getElementById('fireEffect').checked = (currentRing === "0") ? fireExtOn : fireIntOn;

                    if(data.mode === "manual") changeModeTabs('manual');
                    else if(data.mode === "simulacio") {
                        changeModeTabs('simulacio');
                        if(data.sim_ts > 0 && !document.getElementById('simDate').value) {
                            let simDate = new Date(data.sim_ts * 1000);
                            simDate.setMinutes(simDate.getMinutes() - simDate.getTimezoneOffset());
                            document.getElementById('simDate').value = simDate.toISOString().slice(0, 16);
                        }
                    } else changeModeTabs('astro');
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
                loadPresets(); 
            }
            if(m === 'simulacio') {
                document.getElementById('simControls').classList.remove('disabled');
                if(!document.getElementById('simDate').value) {
                    let now = new Date();
                    now.setMinutes(now.getMinutes() - now.getTimezoneOffset());
                    document.getElementById('simDate').value = now.toISOString().slice(0, 16);
                }
            }
        }

        function setMode(m) {
            fetch('/api/mode?set=' + m).then(() => {
                changeModeTabs(m);
                updateStatusUI(); 
            });
        }

        function loadPresets() {
            fetch('/api/presets/list')
                .then(res => res.json())
                .then(names => {
                    let html = '';
                    if(names.length === 0) {
                        html = '<p style="color: #aaa; font-size: 13px; text-align: center;">No hi ha cap configuració guardada encara.</p>';
                    } else {
                        names.forEach(name => {
                            html += `
                            <div class="preset-item">
                                <span style="font-weight: bold;">${name}</span>
                                <div style="display: flex; gap: 8px;">
                                    <button style="background: #198754; padding: 6px 12px; margin: 0; font-size: 13px;" onclick="loadPreset('${name}')">▶️ Carregar</button>
                                    <button style="background: #dc3545; padding: 6px 10px; margin: 0; font-size: 13px;" onclick="deletePreset('${name}')">🗑️</button>
                                </div>
                            </div>`;
                        });
                    }
                    document.getElementById('presetsList').innerHTML = html;
                });
        }

        function savePreset() {
            let name = document.getElementById('presetName').value.trim();
            if(!name) { alert('Introdueix un nom per a la configuració.'); return; }
            
            fetch('/api/presets/save?name=' + encodeURIComponent(name), { method: 'POST' })
                .then(res => {
                    if(res.ok) {
                        document.getElementById('presetName').value = '';
                        loadPresets(); 
                    } else {
                        alert('Error al guardar a la base de dades interna.');
                    }
                });
        }

        function loadPreset(name) {
            fetch('/api/presets/load?name=' + encodeURIComponent(name), { method: 'POST' })
                .then(res => {
                    if(res.ok) {
                        setMode('manual'); 
                        updateStatusUI();
                        
                        // En carregar un preset complet, la memòria dels lliscadors 
                        // s'ha de reiniciar per evitar que tinguis valors vells.
                        ringState = {
                            "0": { r: 0, g: 0, b: 0, w: 0, id: 0, all: true },
                            "1": { r: 0, g: 0, b: 0, w: 0, id: 0, all: true }
                        };
                        updateMaxId(); // Força la UI a reiniciar els lliscadors
                    } else {
                        alert('Error al llegir el fitxer guardat.');
                    }
                });
        }

        function deletePreset(name) {
            if(!confirm('Segur que vols esborrar la configuració "' + name + '"?')) return;
            fetch('/api/presets/delete?name=' + encodeURIComponent(name), { method: 'POST' })
                .then(res => {
                    if(res.ok) loadPresets();
                });
        }

        function sendSimulation() {
            let dateVal = document.getElementById('simDate').value;
            if(!dateVal) return;
            let unixTimestamp = Math.floor(new Date(dateVal).getTime() / 1000);
            fetch('/api/simulate?ts=' + unixTimestamp).then(res => { if(res.ok) updateStatusUI(); });
        }

        function stepSlider(id, step) {
            let el = document.getElementById(id);
            if (el.disabled) return;
            let val = parseInt(el.value) + step;
            if (val < parseInt(el.min)) val = parseInt(el.min);
            if (val > parseInt(el.max)) val = parseInt(el.max);
            el.value = val;
            if (id === 'ledId') updateLEDUI();
            else updateColorUI();
        }

        function updateMaxId() {
            let ring = document.querySelector('input[name="ring"]:checked').value;
            document.getElementById('ledId').max = (ring === "0") ? 70 : 71;
            
            // Aquí està la clau: CARREGAR L'ESTAT de la corona pertinent
            document.getElementById('colorR').value = ringState[ring].r;
            document.getElementById('colorG').value = ringState[ring].g;
            document.getElementById('colorB').value = ringState[ring].b;
            document.getElementById('colorW').value = ringState[ring].w;
            document.getElementById('ledId').value = ringState[ring].id;
            
            let applyAll = ringState[ring].all;
            document.getElementById('allLeds').checked = applyAll;
            document.getElementById('ledId').disabled = applyAll;

            document.getElementById('fireEffect').checked = (ring === "0") ? fireExtOn : fireIntOn;
            
            updateLEDUI();
            updateColorUI();
        }

        function toggleSlider() {
            let isAll = document.getElementById('allLeds').checked;
            document.getElementById('ledId').disabled = isAll;
            updateLEDUI();
        }

        function updateLEDUI() {
            document.getElementById('ledIdVal').innerText = document.getElementById('allLeds').checked ? 'Tots' : document.getElementById('ledId').value;
        }

        function updateColorUI() {
            document.getElementById('rVal').innerText = document.getElementById('colorR').value;
            document.getElementById('gVal').innerText = document.getElementById('colorG').value;
            document.getElementById('bVal').innerText = document.getElementById('colorB').value;
            document.getElementById('wVal').innerText = document.getElementById('colorW').value;
        }

        function sendColorData() {
            let ring = document.querySelector('input[name="ring"]:checked').value;
            let isAll = document.getElementById('allLeds').checked;
            let idVal = document.getElementById('ledId').value;
            let id = isAll ? -1 : idVal;
            
            let r = document.getElementById('colorR').value;
            let g = document.getElementById('colorG').value;
            let b = document.getElementById('colorB').value;
            let w = document.getElementById('colorW').value;
            let f = document.getElementById('fireEffect').checked ? 1 : 0;
            
            // GUARDAR L'ESTAT a la memòria de Javascript just abans d'enviar
            ringState[ring] = { r: r, g: g, b: b, w: w, id: idVal, all: isAll };
            
            fetch(`/api/led?ring=${ring}&id=${id}&r=${r}&g=${g}&b=${b}&w=${w}&f=${f}`)
                .then(() => {
                    if (ring === "0") fireExtOn = (f === 1);
                    else fireIntOn = (f === 1);
                });
        }

        window.onload = function() {
            updateStatusUI();
            loadPresets(); 
        };
    </script>
</body>
</html>
)=====";

#endif