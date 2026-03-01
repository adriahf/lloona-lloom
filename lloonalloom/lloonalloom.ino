#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <math.h>
#include "secrets.h"

// ==========================================
// CONFIGURACIÓ GLOBAL
// ==========================================
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
const char* nom_dispositiu = SECRET_DEVICE_NAME;

// Coordenades carregades des de secrets.h
const double LATITUD = SECRET_LATITUDE;
const double LONGITUD = SECRET_LONGITUDE;

const int PIN_DADES = 27;
const int NUM_LEDS = 143;         // 71 + 72
const int NUM_OUTER_LEDS = 71;    // Índexs 0 a 70
const int NUM_INNER_LEDS = 72;    // Índexs 71 a 142

Adafruit_NeoPixel tira = Adafruit_NeoPixel(NUM_LEDS, PIN_DADES, NEO_GRBW + NEO_KHZ800);
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0); // Offset 0 per treballar en UTC pur

// ==========================================
// PALETA DE COLORS DIÜRNS
// ==========================================
struct ColorRGBW {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
};

const ColorRGBW PALETA_COLORS[10] = {
    {10, 0, 0, 0},  // 0: VERMELL
    {0, 10, 0, 0},  // 1: VERD
    {0, 0, 10, 0},  // 2: BLAU
    {0, 0, 0, 10},  // 3: BLANC
    {6, 3, 0, 1},   // 4: GROC
    {7, 0, 3, 0},   // 5: ROSA
    {3, 0, 7, 0},   // 6: LILA
    {0, 5, 5, 0},   // 7: TURQUESA
    {0, 3, 3, 4},   // 8: BLANC_FRED
    {8, 2, 0, 0}    // 9: TARONJA
};

// ==========================================
// ESTRUCTURES I MOTOR ASTRONÒMIC
// ==========================================
struct MoonData {
    double fraction;    // 0.0 a 1.0 (Percentatge il·luminat)
    double phaseAge;    // Dies des de la lluna nova
    bool isWaxing;      // true = Creixent, false = Minvant
    double phaseAngle;  // Angle d'elongació
    double tiltAngle;   // Angle d'inclinació paral·làctica (respecte al Zenit)
    double altitude;    // Elevació sobre l'horitzó (graus)
    double jd;          // Dia Julià absolut d'aquest càlcul
    bool is_daytime;    // True si el sol està per sobre de l'horitzó
};

class LunarCalculator {
private:
    double normalize(double v) {
        v = v - floor(v / 360.0) * 360.0;
        if (v < 0) v += 360.0;
        return v;
    }
    double toRad(double deg) { return deg * (M_PI / 180.0); }
    double toDeg(double rad) { return rad * (180.0 / M_PI); }

public:
    double computeJulianDay(time_t unixTime) {
        return (unixTime / 86400.0) + 2440587.5;
    }

    MoonData calculate(time_t unixTime) {
        double jd = computeJulianDay(unixTime);
        double T = (jd - 2451545.0) / 36525.0;

        // 1. Elements del Sol
        double L0 = normalize(280.46646 + 36000.76983 * T);
        double M = normalize(357.52911 + 35999.05029 * T);
        double lam_sun = normalize(L0 + 1.914602 * sin(toRad(M)) + 0.019993 * sin(toRad(2*M)));
        
        // 2. Elements de la Lluna
        double LP = normalize(218.31645 + 481267.8813 * T);
        double D = normalize(297.85020 + 445267.1115 * T);
        double MP = normalize(134.96341 + 477198.8676 * T);
        double F = normalize(93.27209 + 483202.0175 * T);

        double lam_moon = normalize(LP + 6.289 * sin(toRad(MP)) + 1.274 * sin(toRad(2*D - MP)) + 0.658 * sin(toRad(2*D)) + 0.214 * sin(toRad(2*MP)) - 0.186 * sin(toRad(M)) - 0.114 * sin(toRad(2*F)));
        double bet_moon = 5.128 * sin(toRad(F)) + 0.280 * sin(toRad(MP + F)) + 0.277 * sin(toRad(MP - F)) + 0.173 * sin(toRad(2*D - F));

        // 3. Obliqüitat de l'eclíptica
        double eps = 23.439291 - 0.0130042 * T;

        // 4. Coordenades Equatorials (Ascensió Recta i Declinació)
        double alpha_sun = normalize(toDeg(atan2(cos(toRad(eps)) * sin(toRad(lam_sun)), cos(toRad(lam_sun)))));
        double delta_sun = toDeg(asin(sin(toRad(eps)) * sin(toRad(lam_sun))));

        double alpha_moon = normalize(toDeg(atan2(sin(toRad(lam_moon))*cos(toRad(eps)) - tan(toRad(bet_moon))*sin(toRad(eps)), cos(toRad(lam_moon)))));
        double delta_moon = toDeg(asin(sin(toRad(bet_moon))*cos(toRad(eps)) + cos(toRad(bet_moon))*sin(toRad(eps))*sin(toRad(lam_moon))));

        // 5. Angle de Posició del Limb Il·luminat (chi)
        double y_chi = cos(toRad(delta_sun)) * sin(toRad(alpha_sun - alpha_moon));
        double x_chi = cos(toRad(delta_moon)) * sin(toRad(delta_sun)) - sin(toRad(delta_moon)) * cos(toRad(delta_sun)) * cos(toRad(alpha_sun - alpha_moon));
        double chi = normalize(toDeg(atan2(y_chi, x_chi)));

        // 6. Angles Locals i Altituds
        double theta0 = 280.46061837 + 360.98564736629 * (jd - 2451545.0);
        double theta = normalize(theta0 + LONGITUD); // Temps Sideral Local
        
        // Lluna
        double H_moon = normalize(theta - alpha_moon); // Angle Horari Lluna
        double y_q = sin(toRad(H_moon));
        double x_q = tan(toRad(LATITUD)) * cos(toRad(delta_moon)) - sin(toRad(delta_moon)) * cos(toRad(H_moon));
        double q = toDeg(atan2(y_q, x_q));
        
        // Sol
        double H_sun = normalize(theta - alpha_sun); // Angle Horari Sol
        double altitude_sun = toDeg(asin(sin(toRad(LATITUD))*sin(toRad(delta_sun)) + cos(toRad(LATITUD))*cos(toRad(delta_sun))*cos(toRad(H_sun))));

        // 7. Dades de sortida
        MoonData data;
        data.jd = jd;
        data.is_daytime = (altitude_sun > -0.83); // El sol es considera post quan baixa de -0.83º per refracció
        
        double i = 180.0 - D; 
        data.fraction = (1.0 + cos(toRad(i))) / 2.0;
        data.phaseAge = (D / 360.0) * 29.530588853;
        data.isWaxing = (D < 180.0); 
        data.phaseAngle = D;
        
        data.tiltAngle = normalize(chi - q);
        data.altitude = toDeg(asin(sin(toRad(LATITUD))*sin(toRad(delta_moon)) + cos(toRad(LATITUD))*cos(toRad(delta_moon))*cos(toRad(H_moon))));

        return data;
    }
};

LunarCalculator moonCalc;
MoonData dades_actuals;
unsigned long ultim_calcul = 0;
const unsigned long INTERVAL_CALCUL = 60000; // Recalcular cada 60 segons

// ==========================================
// MÀQUINA D'ESTATS I UI
// ==========================================
enum SystemMode { MODE_ASTRO, MODE_SIMULACIO, MODE_MANUAL };
SystemMode mode_actual = MODE_ASTRO;
time_t simulacio_timestamp = 0;

// Codi HTML i JS de la Interfície Web
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
        input[type=datetime-local] { width: 100%; padding: 12px; border-radius: 8px; border: 1px solid #444; background: #222; color: #fff; font-size: 16px; box-sizing: border-box; margin-bottom: 15px;}
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
        <p style="font-size: 13px; color: #aaa; margin-top: 0;">Tria una data i hora per renderitzar l'estat exacte d'aquell moment. S'aplicarà la teva zona horària local automàticament.</p>
        <label>Data i Hora Local:</label>
        <input type="datetime-local" id="simDate">
        <button class="btn-send" onclick="sendSimulation()">Simular</button>
    </div>

    <!-- Panell de Control Manual de LEDs -->
    <div id="manualControls" class="card disabled">
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

    <script>
        function updateStatusUI() {
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    // Actualitzar la barra d'estat
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

                    // Actualitzar estats de la UI interna si no hi hem interactuat
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
            
            if(m === 'manual') document.getElementById('manualControls').classList.remove('disabled');
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
                updateStatusUI(); // Refrescar JSON i labels de dalt
            });
        }

        function sendSimulation() {
            let dateVal = document.getElementById('simDate').value;
            if(!dateVal) return;
            let unixTimestamp = Math.floor(new Date(dateVal).getTime() / 1000);
            fetch('/api/simulate?ts=' + unixTimestamp)
                .then(response => {
                    if(response.ok) updateStatusUI();
                });
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
            updateLEDUI();
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
            let id = document.getElementById('allLeds').checked ? -1 : document.getElementById('ledId').value;
            let r = document.getElementById('colorR').value;
            let g = document.getElementById('colorG').value;
            let b = document.getElementById('colorB').value;
            let w = document.getElementById('colorW').value;
            fetch(`/api/led?ring=${ring}&id=${id}&r=${r}&g=${g}&b=${b}&w=${w}`);
        }

        window.onload = updateStatusUI;
    </script>
</body>
</html>
)=====";

// ==========================================
// CAPA D'ABSTRACCIÓ DE MAQUINARI
// ==========================================

void set_outer_pixel(float logical_angle, uint32_t color) {
    logical_angle = fmod(logical_angle, 360.0);
    if (logical_angle < 0) logical_angle += 360.0;
    
    int index = (int)((logical_angle / 360.0) * NUM_OUTER_LEDS);
    if (index >= NUM_OUTER_LEDS) index = NUM_OUTER_LEDS - 1;
    tira.setPixelColor(index, color);
}

void set_inner_pixel(float logical_angle, uint32_t color) {
    logical_angle = fmod(logical_angle, 360.0);
    if (logical_angle < 0) logical_angle += 360.0;
    
    float physical_angle = 360.0 - logical_angle;
    if (physical_angle >= 360.0) physical_angle -= 360.0;
    
    int index = NUM_OUTER_LEDS + (int)((physical_angle / 360.0) * NUM_INNER_LEDS);
    if (index >= NUM_LEDS) index = NUM_LEDS - 1; 
    tira.setPixelColor(index, color);
}

// ==========================================
// FUNCIONS D'INICIALITZACIÓ I API
// ==========================================

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    int intents = 0;
    while (WiFi.status() != WL_CONNECTED && intents < 20) {
        delay(500);
        intents++;
    }
}

void setup_ota() {
    ArduinoOTA.setHostname(nom_dispositiu);
    ArduinoOTA.onStart([]() { tira.clear(); tira.show(); });
    ArduinoOTA.begin();
}

void setup_web() {
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", INDEX_HTML);
    });

    server.on("/api/status", HTTP_GET, []() {
        String json = "{";
        String m_str = "astro";
        if (mode_actual == MODE_MANUAL) m_str = "manual";
        else if (mode_actual == MODE_SIMULACIO) m_str = "simulacio";
        
        json += "\"mode\":\"" + m_str + "\",";
        json += "\"sim_ts\":" + String(simulacio_timestamp) + ",";
        json += "\"fraction\":" + String(dades_actuals.fraction, 4) + ",";
        json += "\"tilt\":" + String(dades_actuals.tiltAngle, 2) + ",";
        json += "\"is_daytime\":" + String(dades_actuals.is_daytime ? "true" : "false") + ",";
        json += "\"timestamp\":" + String(timeClient.getEpochTime());
        json += "}";
        server.send(200, "application/json", json);
    });

    server.on("/api/mode", []() {
        if (server.hasArg("set")) {
            String nou_mode = server.arg("set");
            if (nou_mode == "manual") {
                mode_actual = MODE_MANUAL;
            } else if (nou_mode == "simulacio") {
                mode_actual = MODE_SIMULACIO;
            } else if (nou_mode == "astro") {
                mode_actual = MODE_ASTRO;
                ultim_calcul = 0; 
            }
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/simulate", []() {
        if (server.hasArg("ts")) {
            simulacio_timestamp = server.arg("ts").toInt();
            mode_actual = MODE_SIMULACIO;
            
            dades_actuals = moonCalc.calculate(simulacio_timestamp);
            extern void render_astro_state();
            render_astro_state();
            
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Manca el paràmetre ts");
        }
    });

    server.on("/api/led", []() {
        if (mode_actual != MODE_MANUAL) {
            server.send(403, "text/plain", "Error: No estas en mode manual");
            return;
        }

        int ring = server.arg("ring").toInt(); 
        int id = server.arg("id").toInt();     
        int r = server.arg("r").toInt();
        int g = server.arg("g").toInt();
        int b = server.arg("b").toInt();
        int w = server.arg("w").toInt();

        uint32_t color = tira.Color(r, g, b, w);

        if (ring == 0) {
            if (id == -1) {
                for (int i = 0; i < NUM_OUTER_LEDS; i++) tira.setPixelColor(i, color);
            } else if (id >= 0 && id < NUM_OUTER_LEDS) {
                tira.setPixelColor(id, color);
            }
        } else if (ring == 1) {
            if (id == -1) {
                for (int i = 0; i < NUM_INNER_LEDS; i++) tira.setPixelColor(NUM_OUTER_LEDS + i, color);
            } else if (id >= 0 && id < NUM_INNER_LEDS) {
                tira.setPixelColor(NUM_OUTER_LEDS + id, color);
            }
        }

        tira.show();
        server.send(200, "text/plain", "OK");
    });

    server.begin();
}

// ==========================================
// RENDERITZAT: DIA (COLORS) I NIT (LLUNA)
// ==========================================
void render_astro_state() {
    tira.clear();

    if (dades_actuals.is_daytime) {
        // --- MODE DIÜRN ---
        // Generació determinista de colors usant la part sencera del Dia Julià com a llavor.
        // En sumar (LONGITUD/360), centrem el canvi de dia a la mitjanit local aproximada.
        uint32_t local_day_seed = (uint32_t)(dades_actuals.jd + 0.5 + (LONGITUD / 360.0));
        
        randomSeed(local_day_seed);
        
        int idx_outer = random(0, 10);
        int idx_inner = random(0, 10);
        
        // Evitem que les dues corones escullin exactament el mateix color
        if (idx_inner == idx_outer) {
            idx_inner = (idx_inner + 1) % 10;
        }

        uint32_t color_outer = tira.Color(PALETA_COLORS[idx_outer].r, PALETA_COLORS[idx_outer].g, PALETA_COLORS[idx_outer].b, PALETA_COLORS[idx_outer].w);
        uint32_t color_inner = tira.Color(PALETA_COLORS[idx_inner].r, PALETA_COLORS[idx_inner].g, PALETA_COLORS[idx_inner].b, PALETA_COLORS[idx_inner].w);

        for (int i = 0; i < NUM_OUTER_LEDS; i++) {
            tira.setPixelColor(i, color_outer);
        }
        for (int i = 0; i < NUM_INNER_LEDS; i++) {
            tira.setPixelColor(NUM_OUTER_LEDS + i, color_inner);
        }

    } else {
        // --- MODE NOCTURN (Fase Lunar) ---
        uint8_t max_brightness = 15; 
        double smoothing_band = 0.15; 
        
        double limit = 1.0 - (2.0 * dades_actuals.fraction);
        double angle_per_led = 360.0 / NUM_INNER_LEDS;
        
        for (int i = 0; i < NUM_INNER_LEDS; i++) {
            double angle_logic = i * angle_per_led;
            
            double angle_celestial = 360.0 - angle_logic;
            if (angle_celestial >= 360.0) angle_celestial -= 360.0;
            
            double rel_angle = angle_celestial - dades_actuals.tiltAngle;
            double projection = cos(rel_angle * M_PI / 180.0);
            
            double intensity_factor = 0.0; 
            
            if (projection >= limit + smoothing_band) {
                intensity_factor = 1.0;
            } else if (projection > limit - smoothing_band) {
                intensity_factor = (projection - (limit - smoothing_band)) / (2.0 * smoothing_band);
            }
            
            uint8_t w_val = (uint8_t)(intensity_factor * max_brightness);
            
            if (w_val > 0) {
                set_inner_pixel(angle_logic, tira.Color(0, 0, 0, w_val));
            }
        }
        
        // Opcional: Podries posar un fons fosc al cel (corona exterior) aquí
        // for (int i = 0; i < NUM_OUTER_LEDS; i++) tira.setPixelColor(i, tira.Color(0, 0, 1, 0));
    }
    
    tira.show();
}

// ==========================================
// BUCLE PRINCIPAL (MAIN LOOP)
// ==========================================
void setup() {
    tira.begin();
    tira.clear();
    tira.show();

    setup_wifi();
    setup_ota();
    timeClient.begin();
    setup_web();
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    
    if (WiFi.status() == WL_CONNECTED) {
        timeClient.update();
    }

    if (mode_actual == MODE_ASTRO) {
        unsigned long temps_actual = millis();
        if (ultim_calcul == 0 || temps_actual - ultim_calcul >= INTERVAL_CALCUL) {
            ultim_calcul = temps_actual;
            
            time_t raw_time = timeClient.getEpochTime();
            
            if (raw_time > 100000) { 
                dades_actuals = moonCalc.calculate(raw_time);
                render_astro_state();
            }
        }
    }
}