#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <math.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "webpage.h"
#include "types.h"

// ==========================================
// CONFIGURACIÓ GLOBAL
// ==========================================
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
const char* nom_dispositiu = SECRET_DEVICE_NAME;

const double LATITUD = SECRET_LATITUDE;
const double LONGITUD = SECRET_LONGITUDE;

const int PIN_DADES = 27;
const int NUM_LEDS = 144;         
const int NUM_OUTER_LEDS = 72;    
const int NUM_INNER_LEDS = 72;    

Adafruit_NeoPixel tira = Adafruit_NeoPixel(NUM_LEDS, PIN_DADES, NEO_GRBW + NEO_KHZ800);
bool output_enabled = true;
void showOutput() {
    if (!output_enabled) tira.clear();
    tira.show();
}
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

// ==========================================
// ESTRUCTURA DE COLOR (Ara inclou Foc Individual)
// ==========================================


const ColorRGBW PALETA_COLORS[] = {
    {10, 0, 0, 0, false},  // 0: VERMELL
    {0, 0, 10, 0, false},  // 1: BLAU
    {0, 0, 0, 10, false},  // 2: BLANC
    {6, 3, 0, 1, false},   // 3: GROC
    {7, 0, 3, 0, false},   // 4: ROSA
    {3, 0, 7, 0, false},   // 5: LILA
    {0, 5, 5, 0, false},   // 6: TURQUESA
    {0, 3, 3, 4, false},   // 7: BLANC_FRED
    {8, 2, 0, 0, false}    // 8: TARONJA
};
const int NUM_PALETA_COLORS = sizeof(PALETA_COLORS) / sizeof(PALETA_COLORS[0]);


// ==========================================
// CONFIGURACIO DE DISSENY I COLORS DIURNS
// ==========================================
// El mode manual treballa amb una escala propia i conservadora. El valor es
// el pressupost maxim total (R+G+B+W) per pixel, no el maxim d'un uint8_t.
// Així el 100% de la UI te un significat controlat i cap client HTTP pot
// enviar accidentalment un pixel a 255+255+255+255.
const uint16_t MANUAL_MAX_TOTAL = 48;



uint32_t day_override_key = 0;
uint16_t day_override_variant = 0;
const char* DAY_STATE_FILE = "/day_state.json";
const int NUM_DAY_PAIRS = 8;
const uint8_t DAY_PAIR_INDICES[NUM_DAY_PAIRS][2] = {
    {6,4}, {1,8}, {6,5}, {8,6}, {3,5}, {6,8}, {5,4}, {4,7}
};
ColorRGBW calibrated_day_pairs[NUM_DAY_PAIRS][2];
String daily_palette_signature;

uint32_t mix32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

uint32_t dayKeyFromJulian(double jd) {
    // Conserva el mateix criteri de canvi de dia que tenia el projecte.
    return (uint32_t)floor(jd + 0.5 + (LONGITUD / 360.0));
}

uint16_t dayVariantForKey(uint32_t dayKey) {
    return (dayKey == day_override_key) ? day_override_variant : 0;
}

DayColorSelection getDayColors(double jd) {
    DayColorSelection result;
    result.dayKey = dayKeyFromJulian(jd);
    result.variant = dayVariantForKey(result.dayKey);

    // Tria una harmonia completa, mai dos colors independents. La seleccio
    // estable per dia conserva la previsualitzacio i no altera random() del foc.
    uint32_t seed = mix32(result.dayKey);
    // La paritat evita repetir la parella en dos dies consecutius (variant 0).
    int pair = ((seed % 4U) * 2U + (result.dayKey & 1U) + result.variant) % NUM_DAY_PAIRS;
    result.outerIndex = DAY_PAIR_INDICES[pair][0];
    result.innerIndex = DAY_PAIR_INDICES[pair][1];
    if (daily_palette_signature.length()) {
        result.outer = calibrated_day_pairs[pair][0];
        result.inner = calibrated_day_pairs[pair][1];
    } else {
        result.outer = PALETA_COLORS[result.outerIndex];
        result.inner = PALETA_COLORS[result.innerIndex];
    }
    return result;
}

void loadDayOverride() {
    File file = LittleFS.open(DAY_STATE_FILE, "r");
    if (!file) return;

    StaticJsonDocument<128> doc;
    if (!deserializeJson(doc, file)) {
        day_override_key = doc["dayKey"] | 0;
        day_override_variant = doc["variant"] | 0;
    }
    file.close();
}

void saveDayOverride() {
    StaticJsonDocument<128> doc;
    doc["dayKey"] = day_override_key;
    doc["variant"] = day_override_variant;

    File file = LittleFS.open(DAY_STATE_FILE, "w");
    if (!file) return;
    serializeJson(doc, file);
    file.close();
}

ColorRGBW clampManualColor(ColorRGBW color) {
    uint16_t total = (uint16_t)color.r + color.g + color.b + color.w;
    if (total <= MANUAL_MAX_TOTAL || total == 0) return color;

    // Escalat enter amb truncament: garanteix que la suma resultant no
    // superi mai el pressupost per efectes d'arrodoniment.
    color.r = (uint8_t)(((uint32_t)color.r * MANUAL_MAX_TOTAL) / total);
    color.g = (uint8_t)(((uint32_t)color.g * MANUAL_MAX_TOTAL) / total);
    color.b = (uint8_t)(((uint32_t)color.b * MANUAL_MAX_TOTAL) / total);
    color.w = (uint8_t)(((uint32_t)color.w * MANUAL_MAX_TOTAL) / total);
    return color;
}

void clampManualArray(ColorRGBW* arr, int size) {
    for (int i = 0; i < size; i++) arr[i] = clampManualColor(arr[i]);
}

String colorToJson(const ColorRGBW& color) {
    String json = "{\"r\":" + String(color.r);
    json += ",\"g\":" + String(color.g);
    json += ",\"b\":" + String(color.b);
    json += ",\"w\":" + String(color.w);
    json += "}";
    return json;
}

String presetFilename(const String& rawName) {
    String name = rawName;name.trim();
    uint8_t digest[32];
    mbedtls_sha256((const unsigned char*)name.c_str(),name.length(),digest,0);
    String path="/p_";path.reserve(45);
    char byte[3];
    for(int i=0;i<16;++i){snprintf(byte,sizeof(byte),"%02x",digest[i]);path+=byte;}
    return path+".json";
}

bool presetBelongsToName(const String& path,const String& name) {
    File file=LittleFS.open(path,"r");if(!file)return false;
    DynamicJsonDocument doc(4096);auto error=deserializeJson(doc,file);file.close();
    return !error && doc["name"].as<String>()==name;
}

String legacyPresetFilename(const String& rawName) {
    String name = rawName;
    name.trim();
    if (name.length() > 20) name = name.substring(0, 20);
    String filename = "/p_" + name + ".json";
    filename.replace(" ", "_");
    return filename;
}

String existingPresetFilename(const String& rawName) {
    String name=rawName;name.trim();
    String current=presetFilename(name);if(presetBelongsToName(current,name))return current;
    String safe="/p_";
    for(size_t i=0;i<name.length()&&i<20;++i){char c=name[i];safe+=(isalnum((unsigned char)c)||c=='-'||c=='_')?c:'_';}
    safe+=".json";
    if(presetBelongsToName(safe,name))return safe;
    String legacy=legacyPresetFilename(name);if(presetBelongsToName(legacy,name))return legacy;
    return current;
}

// ==========================================
// ESTRUCTURES I MOTOR ASTRONÒMIC
// ==========================================


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

        double L0 = normalize(280.46646 + 36000.76983 * T);
        double M = normalize(357.52911 + 35999.05029 * T);
        double lam_sun = normalize(L0 + 1.914602 * sin(toRad(M)) + 0.019993 * sin(toRad(2*M)));
        
        double LP = normalize(218.31645 + 481267.8813 * T);
        double D = normalize(297.85020 + 445267.1115 * T);
        double MP = normalize(134.96341 + 477198.8676 * T);
        double F = normalize(93.27209 + 483202.0175 * T);

        double lam_moon = normalize(LP + 6.289 * sin(toRad(MP)) + 1.274 * sin(toRad(2*D - MP)) + 0.658 * sin(toRad(2*D)) + 0.214 * sin(toRad(2*MP)) - 0.186 * sin(toRad(M)) - 0.114 * sin(toRad(2*F)));
        double bet_moon = 5.128 * sin(toRad(F)) + 0.280 * sin(toRad(MP + F)) + 0.277 * sin(toRad(MP - F)) + 0.173 * sin(toRad(2*D - F));

        double eps = 23.439291 - 0.0130042 * T;

        double alpha_sun = normalize(toDeg(atan2(cos(toRad(eps)) * sin(toRad(lam_sun)), cos(toRad(lam_sun)))));
        double delta_sun = toDeg(asin(sin(toRad(eps)) * sin(toRad(lam_sun))));

        double alpha_moon = normalize(toDeg(atan2(sin(toRad(lam_moon))*cos(toRad(eps)) - tan(toRad(bet_moon))*sin(toRad(eps)), cos(toRad(lam_moon)))));
        double delta_moon = toDeg(asin(sin(toRad(bet_moon))*cos(toRad(eps)) + cos(toRad(bet_moon))*sin(toRad(eps))*sin(toRad(lam_moon))));

        double y_chi = cos(toRad(delta_sun)) * sin(toRad(alpha_sun - alpha_moon));
        double x_chi = cos(toRad(delta_moon)) * sin(toRad(delta_sun)) - sin(toRad(delta_moon)) * cos(toRad(delta_sun)) * cos(toRad(alpha_sun - alpha_moon));
        double chi = normalize(toDeg(atan2(y_chi, x_chi)));

        double theta0 = 280.46061837 + 360.98564736629 * (jd - 2451545.0);
        double theta = normalize(theta0 + LONGITUD); 
        
        double H_moon = normalize(theta - alpha_moon); 
        double y_q = sin(toRad(H_moon));
        double x_q = tan(toRad(LATITUD)) * cos(toRad(delta_moon)) - sin(toRad(delta_moon)) * cos(toRad(H_moon));
        double q = toDeg(atan2(y_q, x_q));
        
        double H_sun = normalize(theta - alpha_sun); 
        double altitude_sun = toDeg(asin(sin(toRad(LATITUD))*sin(toRad(delta_sun)) + cos(toRad(LATITUD))*cos(toRad(delta_sun))*cos(toRad(H_sun))));

        MoonData data;
        data.jd = jd;
        data.is_daytime = (altitude_sun > -0.83); 

        // Fracció il·luminada a partir de l'elongació aparent real Sol-Lluna.
        // Fem clamp per evitar errors numèrics puntuals fora del rang [-1, 1].
        double cos_elongation =
            sin(toRad(delta_sun)) * sin(toRad(delta_moon)) +
            cos(toRad(delta_sun)) * cos(toRad(delta_moon)) *
            cos(toRad(alpha_sun - alpha_moon));
        cos_elongation = fmax(-1.0, fmin(1.0, cos_elongation));
        double elongation = acos(cos_elongation);
        data.fraction = (1.0 - cos(elongation)) / 2.0;

        // Posició dins del cicle sinòdic (0° nova, 180° plena, 360° nova).
        double phase_cycle = normalize(lam_moon - lam_sun);
        data.phaseAge = (phase_cycle / 360.0) * 29.530588853;
        data.isWaxing = (phase_cycle < 180.0);
        data.phaseAngle = phase_cycle;
        
        data.tiltAngle = normalize(chi - q);
        data.altitude = toDeg(asin(sin(toRad(LATITUD))*sin(toRad(delta_moon)) + cos(toRad(LATITUD))*cos(toRad(delta_moon))*cos(toRad(H_moon))));

        return data;
    }
};

LunarCalculator moonCalc;
MoonData dades_actuals;
unsigned long ultim_calcul = 0;
const unsigned long INTERVAL_CALCUL = 60000; 

// ==========================================
// MÀQUINA D'ESTATS I UI
// ==========================================

SystemMode mode_actual = MODE_ASTRO;
String calibration_owner;
time_t simulacio_timestamp = 0;

// Variables per a l'animació del foc (Ara vinculat a LEDs concrets)
bool has_fire_ext = false;
bool has_fire_int = false;

unsigned long ultim_frame_foc_ext = 0;
unsigned long ultim_frame_foc_int = 0;

uint8_t calor_exterior[NUM_OUTER_LEDS];
uint8_t calor_interior[NUM_INNER_LEDS];

ColorRGBW color_base_exterior[NUM_OUTER_LEDS];
ColorRGBW color_base_interior[NUM_INNER_LEDS];

// ==========================================
// CALIBRACIO COLORIMETRICA (UPRtek)
// ==========================================
// La versio antiga d'Estudi permetia un canal a 255. Aquesta rutina NO
// aplica MANUAL_MAX_TOTAL=48: es un mode de mesura separat, d'alta intensitat.
// Només s'encen un dels dos anells i la suma RGBW per LED mai supera 255.
// Nivells de primaris: 40% / 70% / 100% de 255 (102 / 179 / 255).


const CalibrationSample CALIBRATION_SAMPLES[] = {
    {"FONS",   0,   0,   0,   0},
    {"R40",  102,   0,   0,   0},
    {"R70",  179,   0,   0,   0},
    {"R100", 255,   0,   0,   0},
    {"G40",    0, 102,   0,   0},
    {"G70",    0, 179,   0,   0},
    {"G100",   0, 255,   0,   0},
    {"B40",    0,   0, 102,   0},
    {"B70",    0,   0, 179,   0},
    {"B100",   0,   0, 255,   0},
    {"W40",    0,   0,   0, 102},
    {"W70",    0,   0,   0, 179},
    {"W100",   0,   0,   0, 255},
    {"RG",   128, 127,   0,   0},
    {"RB",   128,   0, 127,   0},
    {"RW",   128,   0,   0, 127},
    {"GB",     0, 128, 127,   0},
    {"GW",     0, 128,   0, 127},
    {"BW",     0,   0, 128, 127},
    {"RGB",   85,  85,  85,   0},
    {"RGW",   85,  85,   0,  85},
    {"RBW",   85,   0,  85,  85},
    {"GBW",    0,  85,  85,  85},
    {"RGBW",  64,  64,  64,  63},
    {"VAL-A",114,  40,   0,  25},
    {"VAL-B", 28,   0, 120,  31},
    {"VAL-C",  0,  55,  96,  28}
};
const int NUM_CALIBRATION_SAMPLES = sizeof(CALIBRATION_SAMPLES) / sizeof(CALIBRATION_SAMPLES[0]);
const unsigned long CALIBRATION_TIMEOUT_MS = 30UL * 60UL * 1000UL;

SystemMode mode_abans_calibracio = MODE_ASTRO;
uint32_t pixels_abans_calibracio[NUM_LEDS];
bool calibration_inner = true;
int calibration_step = 0;
unsigned long calibration_last_action = 0;

String calibrationSampleJson(int index) {
    const CalibrationSample& c = CALIBRATION_SAMPLES[index];
    String json = "{\"id\":\"" + String(c.id) + "\"";
    json += ",\"r\":" + String(c.r);
    json += ",\"g\":" + String(c.g);
    json += ",\"b\":" + String(c.b);
    json += ",\"w\":" + String(c.w);
    json += "}";
    return json;
}

String calibrationStatusJson() {
    if (mode_actual != MODE_CALIBRATION) {
        return "{\"active\":false,\"count\":" + String(NUM_CALIBRATION_SAMPLES) + "}";
    }
    String json = "{\"active\":true,\"ring\":\"";
    json += calibration_inner ? "inner" : "outer";
    json += "\",\"index\":" + String(calibration_step);
    json += ",\"count\":" + String(NUM_CALIBRATION_SAMPLES);
    json += ",\"sample\":" + calibrationSampleJson(calibration_step) + "}";
    return json;
}

void renderCalibrationSample() {
    const CalibrationSample& c = CALIBRATION_SAMPLES[calibration_step];
    // Proteccio independent de la UI. Evitem 255 simultanis en RGBW.
    const uint16_t total = (uint16_t)c.r + c.g + c.b + c.w;
    tira.clear();
    if (total <= 255) {
        uint32_t color = tira.Color(c.r, c.g, c.b, c.w);
        const int start = calibration_inner ? NUM_OUTER_LEDS : 0;
        const int count = calibration_inner ? NUM_INNER_LEDS : NUM_OUTER_LEDS;
        for (int i = 0; i < count; i++) tira.setPixelColor(start + i, color);
    }
    showOutput();
    calibration_last_action = millis();
}

void startCalibration(bool inner) {
    // Guardem l'estat nomes al primer inici. Canviar d'anell no el sobreescriu.
    if (mode_actual != MODE_CALIBRATION) {
        mode_abans_calibracio = mode_actual;
        calibration_owner = server.header("X-Lloona-Session");
        for (int i = 0; i < NUM_LEDS; i++) {
            pixels_abans_calibracio[i] = tira.getPixelColor(i);
        }
    }
    mode_actual = MODE_CALIBRATION;
    calibration_inner = inner;
    calibration_step = 0;
    renderCalibrationSample();
}

void stopCalibration() {
    if (mode_actual != MODE_CALIBRATION) return;
    mode_actual = mode_abans_calibracio;
    calibration_owner = "";
    for (int i = 0; i < NUM_LEDS; i++) {
        tira.setPixelColor(i, pixels_abans_calibracio[i]);
    }
    showOutput();
    // En tornar al mode astronòmic, forcem una actualització real immediata.
    if (mode_actual == MODE_ASTRO) ultim_calcul = 0;
}

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
// FUNCIONS DE PERSISTÈNCIA (HEXADECIMAL)
// ==========================================
// Hem actualitzat l'esquema. Cada LED són 9 caràcters: RRGGBBWWF (F=0 o 1)
String serializeColors(ColorRGBW* arr, int size) {
    String out = "";
    out.reserve(size * 9);
    char buf[10];
    for (int i = 0; i < size; i++) {
        sprintf(buf, "%02X%02X%02X%02X%1d", arr[i].r, arr[i].g, arr[i].b, arr[i].w, arr[i].f ? 1 : 0);
        out += buf;
    }
    return out;
}

String serializeUiColors(ColorRGBW* arr, int size) {
    String out = "";
    out.reserve(size * 9);
    char buf[10];
    for (int ui_i = 0; ui_i < size; ui_i++) {
        int phys_i = (size - ui_i) % size;
        sprintf(buf, "%02X%02X%02X%02X%1d",
                arr[phys_i].r, arr[phys_i].g, arr[phys_i].b, arr[phys_i].w,
                arr[phys_i].f ? 1 : 0);
        out += buf;
    }
    return out;
}

void deserializeColors(String hex, ColorRGBW* arr, int size) {
    for (int i = 0; i < size; i++) {
        if (i * 9 + 8 >= hex.length()) break;
        char rBuf[3] = {hex[i * 9],     hex[i * 9 + 1], '\0'};
        char gBuf[3] = {hex[i * 9 + 2], hex[i * 9 + 3], '\0'};
        char bBuf[3] = {hex[i * 9 + 4], hex[i * 9 + 5], '\0'};
        char wBuf[3] = {hex[i * 9 + 6], hex[i * 9 + 7], '\0'};
        char fBuf[2] = {hex[i * 9 + 8], '\0'};
        
        arr[i].r = strtoul(rBuf, NULL, 16);
        arr[i].g = strtoul(gBuf, NULL, 16);
        arr[i].b = strtoul(bBuf, NULL, 16);
        arr[i].w = strtoul(wBuf, NULL, 16);
        arr[i].f = (fBuf[0] == '1');
    }
}

uint8_t hexToByte(String hex) {
    return (uint8_t) strtoul(hex.c_str(), NULL, 16);
}

// ==========================================
// FUNCIONS D'INICIALITZACIÓ I API
// ==========================================

void render_astro_state();
#include "api_support.h"
#include "day_palette.h"

void setup_wifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(nom_dispositiu);
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);
    int intents = 0;
    while (WiFi.status() != WL_CONNECTED && intents < 20) {
        delay(500);
        intents++;
    }
    if (WiFi.status() == WL_CONNECTED) {
        MDNS.begin(nom_dispositiu);
        MDNS.addService("http", "tcp", 80);
    }
}

void setup_ota() {
    ArduinoOTA.setHostname(nom_dispositiu);
    ArduinoOTA.onStart([]() { tira.clear(); showOutput(); });
    if (SECRET_ENABLE_OTA) {
        ArduinoOTA.setPassword(SECRET_OTA_PASSWORD);
        ArduinoOTA.begin();
    }
}

void render_astro_state();

String buildPreviewJson(time_t timestamp) {
    MoonData data = moonCalc.calculate(timestamp);
    DayColorSelection day = getDayColors(data.jd);

    String json = "{";
    json += "\"timestamp\":" + String((unsigned long)timestamp) + ",";
    json += "\"fraction\":" + String(data.fraction, 5) + ",";
    json += "\"phase_age\":" + String(data.phaseAge, 3) + ",";
    json += "\"is_waxing\":" + String(data.isWaxing ? "true" : "false") + ",";
    json += "\"tilt\":" + String(data.tiltAngle, 3) + ",";
    json += "\"altitude\":" + String(data.altitude, 3) + ",";
    json += "\"is_daytime\":" + String(data.is_daytime ? "true" : "false") + ",";
    json += "\"day\":{";
    json += "\"key\":" + String(day.dayKey) + ",";
    json += "\"variant\":" + String(day.variant) + ",";
    json += "\"outer\":" + colorToJson(day.outer) + ",";
    json += "\"inner\":" + colorToJson(day.inner);
    json += "}}";
    return json;
}

void setup_web() {
    setupVersionedApi();
    setupDayPaletteApi();
    server.on("/", HTTP_GET, []() {
        // The maintenance page is served directly from flash.
        server.send_P(200, "text/html", INDEX_HTML);
    });

    onApi("/api/status", HTTP_GET, []() {
        time_t now = timeClient.getEpochTime();
        MoonData liveData = dades_actuals;
        if (mode_actual == MODE_SIMULACIO) liveData = moonCalc.calculate(simulacio_timestamp);
        else if (now >= 946684800) liveData = moonCalc.calculate(now);
        DayColorSelection day = getDayColors(liveData.jd);

        String modeString = "astro";
        if (mode_actual == MODE_MANUAL) modeString = "manual";
        else if (mode_actual == MODE_SIMULACIO) modeString = "simulacio";
        else if (mode_actual == MODE_CALIBRATION) modeString = "calibration";

        String json = "{";
        json += "\"api_version\":1,\"device_id\":\"" + WiFi.macAddress() + "\",\"boot_id\":\"" + boot_id + "\",\"revision\":" + String(state_revision) + ",";
        json += "\"power\":" + String(output_enabled ? "true" : "false") + ",\"clock_synced\":" + String(now >= 946684800 ? "true" : "false") + ",";
        json += "\"effective_timestamp\":" + String((unsigned long)(mode_actual == MODE_SIMULACIO ? simulacio_timestamp : now)) + ",";
        json += "\"ext\":\"" + serializeOutputRing(0, NUM_OUTER_LEDS) + "\",\"int\":\"" + serializeOutputRing(NUM_OUTER_LEDS, NUM_INNER_LEDS) + "\",";
        json += "\"mode\":\"" + modeString + "\",";
        json += "\"sim_ts\":" + String((unsigned long)simulacio_timestamp) + ",";
        json += "\"timestamp\":" + String((unsigned long)now) + ",";
        json += "\"fraction\":" + String(liveData.fraction, 5) + ",";
        json += "\"phase_age\":" + String(liveData.phaseAge, 3) + ",";
        json += "\"is_waxing\":" + String(liveData.isWaxing ? "true" : "false") + ",";
        json += "\"tilt\":" + String(liveData.tiltAngle, 3) + ",";
        json += "\"altitude\":" + String(liveData.altitude, 3) + ",";
        json += "\"is_daytime\":" + String(liveData.is_daytime ? "true" : "false") + ",";
        json += "\"manual_max_total\":" + String(MANUAL_MAX_TOTAL) + ",";
        json += "\"day\":{";
        json += "\"key\":" + String(day.dayKey) + ",";
        json += "\"variant\":" + String(day.variant) + ",";
        json += "\"outer\":" + colorToJson(day.outer) + ",";
        json += "\"inner\":" + colorToJson(day.inner);
        json += "}}";
        server.send(200, "application/json", json);
    });

    // Previsualitzacio de lectura: no canvia el mode ni els LEDs.
    onApi("/api/preview", HTTP_GET, []() {
        if (!server.hasArg("ts") || !validTimestamp(server.arg("ts"))) return server.send(400, "text/plain", "Data invalida");
        time_t timestamp = (time_t)strtoul(server.arg("ts").c_str(), nullptr, 10);
        if (timestamp < 100000) return server.send(400, "text/plain", "ts invalid");
        server.send(200, "application/json", buildPreviewJson(timestamp));
    });

    onApi("/api/mode", HTTP_ANY, []() {
        if (server.hasArg("set")) {
            String nou_mode = server.arg("set");
            if (nou_mode != "manual" && nou_mode != "astro") return server.send(400,"text/plain","Mode invalid");
            if (mode_actual == MODE_CALIBRATION) return server.send(409,"text/plain","Finalitza la calibracio");
            // Calibration can only end through its explicit stop action or power off.
            if (nou_mode == "manual" || nou_mode == "simulacio" || nou_mode == "astro") {
                stopCalibration();
            }
            if (nou_mode == "manual") {
                mode_actual = MODE_MANUAL;
                repaintCurrentMode();

            } else if (nou_mode == "astro") {
                mode_actual = MODE_ASTRO;
                has_fire_ext = false;
                has_fire_int = false;
                ultim_calcul = 0;
            }
        }
        server.send(200, "text/plain", "OK");
    });

    // Llista immutable de mostres: firmware com a unica font de valors RGBW.
    onApi("/api/calibration/samples", HTTP_GET, []() {
        String json = "[";
        json.reserve(1600);
        for (int i = 0; i < NUM_CALIBRATION_SAMPLES; i++) {
            if (i) json += ",";
            json += calibrationSampleJson(i);
        }
        json += "]";
        server.send(200, "application/json", json);
    });

    onApi("/api/calibration/status", HTTP_GET, []() {
        server.send(200, "application/json", calibrationStatusJson());
    });

    onApi("/api/calibration/start", HTTP_POST, []() {
        if (!server.hasArg("ring")) return server.send(400, "text/plain", "Manca ring");
        String ring = server.arg("ring");
        if (ring != "inner" && ring != "outer") {
            return server.send(400, "text/plain", "Anell invalid");
        }
        if (!output_enabled) return server.send(409,"text/plain","Encen el llum abans de calibrar");
        startCalibration(ring == "inner");
        server.send(200, "application/json", calibrationStatusJson());
    });

    onApi("/api/calibration/step", HTTP_POST, []() {
        if (mode_actual != MODE_CALIBRATION) {
            return server.send(409, "text/plain", "Calibracio inactiva");
        }
        if (!server.hasArg("index")) return server.send(400, "text/plain", "Manca index");
        String rawIndex = server.arg("index");
        if (rawIndex.length() == 0) return server.send(400, "text/plain", "Index buit");
        for (size_t i = 0; i < rawIndex.length(); i++) {
            if (rawIndex[i] < '0' || rawIndex[i] > '9') {
                return server.send(400, "text/plain", "Index invalid");
            }
        }
        int index = rawIndex.toInt();
        if (index < 0 || index >= NUM_CALIBRATION_SAMPLES) {
            return server.send(400, "text/plain", "Fora de rang");
        }
        calibration_step = index;
        renderCalibrationSample();
        server.send(200, "application/json", calibrationStatusJson());
    });

    onApi("/api/calibration/stop", HTTP_POST, []() {
        stopCalibration();
        server.send(200, "application/json", calibrationStatusJson());
    });

    onApi("/api/day/new", HTTP_POST, []() {
        if (mode_actual == MODE_CALIBRATION) return server.send(409,"text/plain","Finalitza la calibracio");
        time_t now = timeClient.getEpochTime();
        if (now < 946684800) return server.send(503, "text/plain", "Hora no sincronitzada");

        MoonData liveData = moonCalc.calculate(now);
        uint32_t todayKey = dayKeyFromJulian(liveData.jd);
        if (day_override_key != todayKey) {
            day_override_key = todayKey;
            day_override_variant = 0;
        }
        mode_actual = MODE_ASTRO;
        day_override_variant++;
        saveDayOverride();

        // Si el sistema esta en temps real i es de dia, el canvi es veu al moment.
        dades_actuals = liveData;
        if (mode_actual == MODE_ASTRO && liveData.is_daytime) render_astro_state();

        server.send(200, "application/json", buildPreviewJson(now));
    });

    onApi("/api/simulate", HTTP_ANY, []() {
        if (!server.hasArg("ts") || !validTimestamp(server.arg("ts"))) return server.send(400, "text/plain", "Data invalida");
        if (mode_actual == MODE_CALIBRATION) return server.send(409,"text/plain","Finalitza la calibracio");

        simulacio_timestamp = (time_t)strtoul(server.arg("ts").c_str(), nullptr, 10);
        if (simulacio_timestamp < 100000) return server.send(400, "text/plain", "ts invalid");

        stopCalibration();
        mode_actual = MODE_SIMULACIO;
        has_fire_ext = false;
        has_fire_int = false;
        dades_actuals = moonCalc.calculate(simulacio_timestamp);
        render_astro_state();
        server.send(200, "text/plain", "OK");
    });

    // Estat manual compacte: RRGGBBWWF per LED. El backend aplica sempre el
    // limit de potencia, independentment del client que faci la peticio.
    onApi("/api/led", HTTP_POST, []() {
        if (mode_actual == MODE_CALIBRATION) return server.send(409,"text/plain","Finalitza la calibracio");
        if (!server.hasArg("plain")) return server.send(400, "text/plain", "Manca el payload");

        DynamicJsonDocument doc(4096);
        DeserializationError err = deserializeJson(doc, server.arg("plain"));
        if (err) return server.send(400, "text/plain", "JSON invalid");

        String ext = doc["ext"].as<String>();
        String intString = doc["int"].as<String>();
        if (!validFrame(ext, NUM_OUTER_LEDS) || !validFrame(intString, NUM_INNER_LEDS)) {
            return server.send(400, "text/plain", "Estat incomplet");
        }

        mode_actual = MODE_MANUAL;
        has_fire_ext = false;
        has_fire_int = false;

        for (int ui_i = 0; ui_i < NUM_OUTER_LEDS; ui_i++) {
            int phys_i = (NUM_OUTER_LEDS - ui_i) % NUM_OUTER_LEDS;
            ColorRGBW color;
            color.r = hexToByte(ext.substring(ui_i * 9, ui_i * 9 + 2));
            color.g = hexToByte(ext.substring(ui_i * 9 + 2, ui_i * 9 + 4));
            color.b = hexToByte(ext.substring(ui_i * 9 + 4, ui_i * 9 + 6));
            color.w = hexToByte(ext.substring(ui_i * 9 + 6, ui_i * 9 + 8));
            color.f = (ext[ui_i * 9 + 8] == '1');
            color = clampManualColor(color);
            color_base_exterior[phys_i] = color;

            if (color.f) has_fire_ext = true;
            else tira.setPixelColor(phys_i, tira.Color(color.r, color.g, color.b, color.w));
        }

        for (int ui_i = 0; ui_i < NUM_INNER_LEDS; ui_i++) {
            int physRel = (NUM_INNER_LEDS - ui_i) % NUM_INNER_LEDS;
            int physId = NUM_OUTER_LEDS + physRel;
            ColorRGBW color;
            color.r = hexToByte(intString.substring(ui_i * 9, ui_i * 9 + 2));
            color.g = hexToByte(intString.substring(ui_i * 9 + 2, ui_i * 9 + 4));
            color.b = hexToByte(intString.substring(ui_i * 9 + 4, ui_i * 9 + 6));
            color.w = hexToByte(intString.substring(ui_i * 9 + 6, ui_i * 9 + 8));
            color.f = (intString[ui_i * 9 + 8] == '1');
            color = clampManualColor(color);
            color_base_interior[physRel] = color;

            if (color.f) has_fire_int = true;
            else tira.setPixelColor(physId, tira.Color(color.r, color.g, color.b, color.w));
        }

        showOutput();
        server.send(200, "text/plain", "OK");
    });

    onApi("/api/colors", HTTP_GET, []() {
        String json = "{\"ext\":\"" + serializeUiColors(color_base_exterior, NUM_OUTER_LEDS) +
                      "\",\"int\":\"" + serializeUiColors(color_base_interior, NUM_INNER_LEDS) + "\"}";
        server.send(200, "application/json", json);
    });

    // --- Escenes guardades ---
    onApi("/api/presets/list", HTTP_GET, []() {
        String json = "[";
        bool first = true;
        File root = LittleFS.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String filename = String(file.name());
                if ((filename.startsWith("p_") || filename.startsWith("/p_")) && filename.endsWith(".json")) {
                    DynamicJsonDocument doc(4096);
                    if (!deserializeJson(doc, file) && doc.containsKey("name")) {
                        if (!first) json += ",";
                        String name = doc["name"].as<String>();
                        name.replace("\\", "\\\\");
                        name.replace("\"", "\\\"");
                        json += "\"" + name + "\"";
                        first = false;
                    }
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }
        json += "]";
        server.send(200, "application/json", json);
    });

    // Llegeix una escena sense aplicar-la. S'utilitza per miniatures i edicio.
    onApi("/api/presets/get", HTTP_GET, []() {
        if (!server.hasArg("name") || !validPresetName(server.arg("name"))) return server.send(400, "text/plain", "Nom invalid");
        String filename = existingPresetFilename(server.arg("name"));
        File file = LittleFS.open(filename, "r");
        if (!file) return server.send(404, "text/plain", "No trobat");

        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, file)) {
            file.close();
            return server.send(500, "text/plain", "Fitxer corrupte");
        }
        file.close();

        ColorRGBW extColors[NUM_OUTER_LEDS];
        ColorRGBW intColors[NUM_INNER_LEDS];
        deserializeColors(doc["ext"].as<String>(), extColors, NUM_OUTER_LEDS);
        deserializeColors(doc["int"].as<String>(), intColors, NUM_INNER_LEDS);
        clampManualArray(extColors, NUM_OUTER_LEDS);
        clampManualArray(intColors, NUM_INNER_LEDS);

        String json = "{\"ext\":\"" + serializeUiColors(extColors, NUM_OUTER_LEDS) +
                      "\",\"int\":\"" + serializeUiColors(intColors, NUM_INNER_LEDS) + "\"}";
        server.send(200, "application/json", json);
    });

    onApi("/api/presets/save", HTTP_POST, []() {
        if (!server.hasArg("name") || !validPresetName(server.arg("name"))) return server.send(400, "text/plain", "Nom invalid");

        String name = server.arg("name");
        name.trim();
        if (!validPresetName(name)) return server.send(400,"text/plain","Nom invalid");

        DynamicJsonDocument doc(4096);
        doc["name"] = name;
        if (server.hasArg("plain")) {
            DynamicJsonDocument body(4096);
            if (deserializeJson(body, server.arg("plain"))) return server.send(400,"text/plain","JSON invalid");
            String ext = body["ext"].as<String>(), inner = body["int"].as<String>();
            if (!validFrame(ext,NUM_OUTER_LEDS) || !validFrame(inner,NUM_INNER_LEDS)) return server.send(400,"text/plain","Disseny invalid");
            ColorRGBW extColors[NUM_OUTER_LEDS], intColors[NUM_INNER_LEDS];
            deserializeColors(ext,extColors,NUM_OUTER_LEDS); deserializeColors(inner,intColors,NUM_INNER_LEDS);
            clampManualArray(extColors,NUM_OUTER_LEDS); clampManualArray(intColors,NUM_INNER_LEDS);
            // UI-to-physical reversal is involutive and must happen exactly once.
            doc["ext"] = serializeUiColors(extColors,NUM_OUTER_LEDS);
            doc["int"] = serializeUiColors(intColors,NUM_INNER_LEDS);
        } else {
            doc["ext"] = serializeColors(color_base_exterior, NUM_OUTER_LEDS);
            doc["int"] = serializeColors(color_base_interior, NUM_INNER_LEDS);
        }

        String target=existingPresetFilename(name);
        if (LittleFS.exists(target) && !presetBelongsToName(target,name)) return server.send(409,"text/plain","Fitxer d'escena no verificable");
        File file = LittleFS.open(target+".tmp", "w");
        if (!file) return server.send(500, "text/plain", "Error FS");
        size_t written=serializeJson(doc,file);file.close();
        if (!written || !LittleFS.rename(target+".tmp",target)) return server.send(500,"text/plain","Error desant escena");
        server.send(200, "text/plain", "OK");
    });

    onApi("/api/presets/load", HTTP_POST, []() {
        if (mode_actual == MODE_CALIBRATION) {
            return server.send(409, "text/plain", "Finalitza la calibracio abans d'aplicar escenes");
        }
        if (!server.hasArg("name") || !validPresetName(server.arg("name"))) return server.send(400, "text/plain", "Nom invalid");
        File file = LittleFS.open(existingPresetFilename(server.arg("name")), "r");
        if (!file) return server.send(404, "text/plain", "No trobat");

        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc, file)) {
            file.close();
            return server.send(500, "text/plain", "Fitxer corrupte");
        }
        file.close();

        deserializeColors(doc["ext"].as<String>(), color_base_exterior, NUM_OUTER_LEDS);
        deserializeColors(doc["int"].as<String>(), color_base_interior, NUM_INNER_LEDS);
        clampManualArray(color_base_exterior, NUM_OUTER_LEDS);
        clampManualArray(color_base_interior, NUM_INNER_LEDS);

        mode_actual = MODE_MANUAL;
        has_fire_ext = false;
        has_fire_int = false;

        for (int i = 0; i < NUM_OUTER_LEDS; i++) {
            if (color_base_exterior[i].f) has_fire_ext = true;
            else tira.setPixelColor(i, tira.Color(color_base_exterior[i].r, color_base_exterior[i].g,
                                                  color_base_exterior[i].b, color_base_exterior[i].w));
        }
        for (int i = 0; i < NUM_INNER_LEDS; i++) {
            if (color_base_interior[i].f) has_fire_int = true;
            else tira.setPixelColor(NUM_OUTER_LEDS + i,
                                    tira.Color(color_base_interior[i].r, color_base_interior[i].g,
                                               color_base_interior[i].b, color_base_interior[i].w));
        }
        showOutput();
        server.send(200, "text/plain", "OK");
    });

    onApi("/api/presets/delete", HTTP_POST, []() {
        if (!server.hasArg("name") || !validPresetName(server.arg("name"))) return server.send(400, "text/plain", "Nom invalid");
        if (LittleFS.remove(existingPresetFilename(server.arg("name")))) server.send(200, "text/plain", "OK");
        else server.send(500, "text/plain", "Error a l'esborrar");
    });

    server.begin();
}


// ==========================================
// RENDERITZAT: DIA (COLORS) I NIT (LLUNA)
// ==========================================
void render_astro_state() {
    tira.clear();

    if (dades_actuals.is_daytime) {
        DayColorSelection day = getDayColors(dades_actuals.jd);
        uint32_t color_outer = tira.Color(day.outer.r, day.outer.g, day.outer.b, day.outer.w);
        uint32_t color_inner = tira.Color(day.inner.r, day.inner.g, day.inner.b, day.inner.w);

        for (int i = 0; i < NUM_OUTER_LEDS; i++) tira.setPixelColor(i, color_outer);
        for (int i = 0; i < NUM_INNER_LEDS; i++) tira.setPixelColor(NUM_OUTER_LEDS + i, color_inner);

    } else {
        const uint8_t MAX_MOON_BRIGHTNESS = 15;
        const double BASE_SMOOTHING_BAND = 0.15;
        const double CRESCENT_DIM_END = 0.25;
        const double CRESCENT_MIN_BRIGHTNESS = 0.35;
        const double NEW_MOON_FADE_RANGE = 0.08;
        const double PHASE_EPSILON = 0.001;

        double fraction = fmax(0.0, fmin(1.0, dades_actuals.fraction));

        // La lluna nova queda realment apagada: el suavitzat original deixava
        // alguns LEDs encesos fins i tot amb una fracció pràcticament nul·la.
        if (fraction <= PHASE_EPSILON) {
            showOutput();
            return;
        }

        // Les fases molt pròximes a lluna nova necessiten menys llum perquè la
        // difusió física no ompli visualment el centre del disc. A partir del quart,
        // mantenim exactament la brillantor original.
        double brightness_scale = 1.0;
        if (fraction < CRESCENT_DIM_END) {
            double t = fraction / CRESCENT_DIM_END;
            brightness_scale = CRESCENT_MIN_BRIGHTNESS +
                               (1.0 - CRESCENT_MIN_BRIGHTNESS) * t;
        }
        uint8_t max_brightness = (uint8_t)round(MAX_MOON_BRIGHTNESS * brightness_scale);

        // A prop de lluna nova reduïm el suavitzat per evitar llum residual.
        // A la resta de fases conservem el render original, que ja funciona bé.
        double smoothing_scale = fmin(1.0, fraction / NEW_MOON_FADE_RANGE);
        double smoothing_band = BASE_SMOOTHING_BAND * smoothing_scale;

        double limit = 1.0 - (2.0 * fraction);
        double angle_per_led = 360.0 / NUM_INNER_LEDS;
        
        for (int i = 0; i < NUM_INNER_LEDS; i++) {
            double angle_logic = i * angle_per_led;
            
            double angle_celestial = 360.0 - angle_logic;
            if (angle_celestial >= 360.0) angle_celestial -= 360.0;
            
            double rel_angle = angle_celestial - dades_actuals.tiltAngle;
            double projection = cos(rel_angle * M_PI / 180.0);
            
            double intensity_factor = 0.0;
            if (smoothing_band <= 0.000001) {
                intensity_factor = (projection >= limit) ? 1.0 : 0.0;
            } else if (projection >= limit + smoothing_band) {
                intensity_factor = 1.0;
            } else if (projection > limit - smoothing_band) {
                intensity_factor = (projection - (limit - smoothing_band)) / (2.0 * smoothing_band);
            }
            
            uint8_t w_val = (uint8_t)round(intensity_factor * max_brightness);
            if (w_val > 0) set_inner_pixel(angle_logic, tira.Color(0, 0, 0, w_val));
        }
    }
    showOutput();
}

// ==========================================
// RENDERITZAT: ANIMACIÓ DE FOC MANUAL INDIVIDUAL
// ==========================================
void animar_foc_exterior() {
    int fire_leds[NUM_OUTER_LEDS];
    int count = 0;
    
    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        if(color_base_exterior[i].f) {
            fire_leds[count++] = i;
            int cooldown = random(0, 6);
            if(calor_exterior[i] <= cooldown + 40) calor_exterior[i] = 40; 
            else calor_exterior[i] -= cooldown;
        }
    }

    if(count > 0) {
        if(random(255) < 200) calor_exterior[fire_leds[random(count)]] = random(180, 255); 
        if(random(255) < 120) calor_exterior[fire_leds[random(count)]] = random(180, 255); 
        if(random(255) < 60)  calor_exterior[fire_leds[random(count)]] = random(200, 255); 
    }

    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        if(color_base_exterior[i].f) {
            uint8_t heat = calor_exterior[i]; 
            uint8_t r_mod = (color_base_exterior[i].r * heat) / 255;
            uint8_t g_mod = (color_base_exterior[i].g * heat) / 255;
            uint8_t b_mod = (color_base_exterior[i].b * heat) / 255;
            uint8_t w_mod = (color_base_exterior[i].w * heat) / 255;
            tira.setPixelColor(i, tira.Color(r_mod, g_mod, b_mod, w_mod));
        }
    }
}

void animar_foc_interior() {
    int fire_leds[NUM_INNER_LEDS];
    int count = 0;

    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        if(color_base_interior[i].f) {
            fire_leds[count++] = i;
            int cooldown = random(0, 6);  
            if(calor_interior[i] <= cooldown + 40) calor_interior[i] = 40;
            else calor_interior[i] -= cooldown;
        }
    }

    if(count > 0) {
        if(random(255) < 200) calor_interior[fire_leds[random(count)]] = random(180, 255); 
        if(random(255) < 120) calor_interior[fire_leds[random(count)]] = random(180, 255); 
        if(random(255) < 60)  calor_interior[fire_leds[random(count)]] = random(200, 255); 
    }

    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        if(color_base_interior[i].f) {
            uint8_t heat = calor_interior[i]; 
            uint8_t r_mod = (color_base_interior[i].r * heat) / 255;
            uint8_t g_mod = (color_base_interior[i].g * heat) / 255;
            uint8_t b_mod = (color_base_interior[i].b * heat) / 255;
            uint8_t w_mod = (color_base_interior[i].w * heat) / 255;
            tira.setPixelColor(NUM_OUTER_LEDS + i, tira.Color(r_mod, g_mod, b_mod, w_mod));
        }
    }
}

// ==========================================
// BUCLE PRINCIPAL (MAIN LOOP)
// ==========================================
void setup() {
    Serial.begin(115200);

    if (!LittleFS.begin(false)) {
        Serial.println("Error muntant el sistema d'arxius LittleFS!");
    } else {
        filesystem_ready = true;
        loadDayOverride();
        loadDayPalette();
    }

    // L'aleatorietat del foc queda separada de la seleccio determinista del dia.
    randomSeed(esp_random());

    tira.begin();
    tira.clear();
    showOutput();

    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        calor_exterior[i] = 40; 
        color_base_exterior[i] = {8, 2, 0, 0, false};
    }
    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        calor_interior[i] = 40;
        color_base_interior[i] = {8, 2, 0, 0, false};
    }

    loadRuntimeState();
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

    unsigned long temps_actual = millis();
    if (runtime_dirty && temps_actual - runtime_dirty_at >= 2000 && mode_actual != MODE_CALIBRATION) saveRuntimeState();

    // Fail-safe: un navegador tancat no deixa la font en alta intensitat per sempre.
    if (mode_actual == MODE_CALIBRATION &&
        temps_actual - calibration_last_action >= CALIBRATION_TIMEOUT_MS) {
        stopCalibration();
        markRuntimeDirty();
    }

    if (mode_actual == MODE_ASTRO) {
        if (ultim_calcul == 0 || temps_actual - ultim_calcul >= INTERVAL_CALCUL) {
            ultim_calcul = temps_actual;
            time_t raw_time = timeClient.getEpochTime();
            if (raw_time >= 946684800) { 
                dades_actuals = moonCalc.calculate(raw_time);
                render_astro_state();
            }
        }
    } 
    else if (mode_actual == MODE_MANUAL) {
        bool show_needed = false;
        
        if (has_fire_ext && (temps_actual - ultim_frame_foc_ext >= 30)) {
            ultim_frame_foc_ext = temps_actual;
            animar_foc_exterior();
            show_needed = true;
        }
        
        if (has_fire_int && (temps_actual - ultim_frame_foc_int >= 30)) {
            ultim_frame_foc_int = temps_actual;
            animar_foc_interior();
            show_needed = true;
        }

        if (show_needed) showOutput();
    }
}
