#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <Adafruit_NeoPixel.h>
#include <NTPClient.h>
#include <WebServer.h>
#include <math.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "webpage.h"

// ==========================================
// CONFIGURACIÓ GLOBAL
// ==========================================
const char* ssid = SECRET_SSID;
const char* password = SECRET_PASS;
const char* nom_dispositiu = SECRET_DEVICE_NAME;

const double LATITUD = SECRET_LATITUDE;
const double LONGITUD = SECRET_LONGITUDE;

const int PIN_DADES = 27;
const int NUM_LEDS = 143;         
const int NUM_OUTER_LEDS = 71;    
const int NUM_INNER_LEDS = 72;    

Adafruit_NeoPixel tira = Adafruit_NeoPixel(NUM_LEDS, PIN_DADES, NEO_GRBW + NEO_KHZ800);
WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0);

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
    double fraction;    
    double phaseAge;    
    bool isWaxing;      
    double phaseAngle;  
    double tiltAngle;   
    double altitude;    
    double jd;          
    bool is_daytime;    
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
const unsigned long INTERVAL_CALCUL = 60000; 

// ==========================================
// MÀQUINA D'ESTATS I UI
// ==========================================
enum SystemMode { MODE_ASTRO, MODE_SIMULACIO, MODE_MANUAL };
SystemMode mode_actual = MODE_ASTRO;
time_t simulacio_timestamp = 0;

// Variables per a l'animació del foc (Les dues corones)
bool exterior_crepitant = false;
bool interior_crepitant = false;

unsigned long ultim_frame_foc_ext = 0;
unsigned long ultim_frame_foc_int = 0;

uint8_t calor_exterior[NUM_OUTER_LEDS];
uint8_t calor_interior[NUM_INNER_LEDS];

ColorRGBW color_base_exterior[NUM_OUTER_LEDS];
ColorRGBW color_base_interior[NUM_INNER_LEDS];

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
// FUNCIONS DE PERSISTÈNCIA (COMPRESSIÓ)
// ==========================================
// Comprimeix tot l'array de colors a una única cadena Hexadecimal pura (sense espais) per estalviar memòria al JSON
String serializeColors(ColorRGBW* arr, int size) {
    String out = "";
    out.reserve(size * 8);
    char buf[9];
    for (int i = 0; i < size; i++) {
        sprintf(buf, "%02X%02X%02X%02X", arr[i].r, arr[i].g, arr[i].b, arr[i].w);
        out += buf;
    }
    return out;
}

// Descomprimeix la cadena Hexadecimal de tornada a l'array de la memòria RAM
void deserializeColors(String hex, ColorRGBW* arr, int size) {
    for (int i = 0; i < size; i++) {
        if (i * 8 + 7 >= hex.length()) break;
        char rBuf[3] = {hex[i * 8],     hex[i * 8 + 1], '\0'};
        char gBuf[3] = {hex[i * 8 + 2], hex[i * 8 + 3], '\0'};
        char bBuf[3] = {hex[i * 8 + 4], hex[i * 8 + 5], '\0'};
        char wBuf[3] = {hex[i * 8 + 6], hex[i * 8 + 7], '\0'};
        arr[i].r = strtoul(rBuf, NULL, 16);
        arr[i].g = strtoul(gBuf, NULL, 16);
        arr[i].b = strtoul(bBuf, NULL, 16);
        arr[i].w = strtoul(wBuf, NULL, 16);
    }
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
        json += "\"fire_ext_on\":" + String(exterior_crepitant ? "true" : "false") + ",";
        json += "\"fire_int_on\":" + String(interior_crepitant ? "true" : "false") + ",";
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
                exterior_crepitant = false; 
                interior_crepitant = false;
            } else if (nou_mode == "astro") {
                mode_actual = MODE_ASTRO;
                exterior_crepitant = false;
                interior_crepitant = false;
                ultim_calcul = 0; 
            }
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/api/simulate", []() {
        if (server.hasArg("ts")) {
            simulacio_timestamp = server.arg("ts").toInt();
            mode_actual = MODE_SIMULACIO;
            exterior_crepitant = false;
            interior_crepitant = false;
            
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
        uint8_t r = server.arg("r").toInt();
        uint8_t g = server.arg("g").toInt();
        uint8_t b = server.arg("b").toInt();
        uint8_t w = server.arg("w").toInt();
        int f = server.hasArg("f") ? server.arg("f").toInt() : 0; 

        uint32_t color = tira.Color(r, g, b, w);

        if (ring == 0) {
            exterior_crepitant = (f == 1); 
            if (id == -1) {
                for (int i = 0; i < NUM_OUTER_LEDS; i++) {
                    color_base_exterior[i] = {r, g, b, w};
                    if (!exterior_crepitant) tira.setPixelColor(i, color);
                }
            } else if (id >= 0 && id < NUM_OUTER_LEDS) {
                color_base_exterior[id] = {r, g, b, w}; 
                if (!exterior_crepitant) tira.setPixelColor(id, color);
            }
        } else if (ring == 1) {
            interior_crepitant = (f == 1);
            if (id == -1) {
                for (int i = 0; i < NUM_INNER_LEDS; i++) {
                    color_base_interior[i] = {r, g, b, w};
                    if (!interior_crepitant) tira.setPixelColor(NUM_OUTER_LEDS + i, color);
                }
            } else if (id >= 0 && id < NUM_INNER_LEDS) {
                color_base_interior[id] = {r, g, b, w};
                if (!interior_crepitant) tira.setPixelColor(NUM_OUTER_LEDS + id, color);
            }
        }

        if (!exterior_crepitant || !interior_crepitant) {
            tira.show();
        }
        
        server.send(200, "text/plain", "OK");
    });

    // --- RUTES API PER A GESTIÓ DE PRESETS ---

    server.on("/api/presets/list", HTTP_GET, []() {
        String json = "[";
        bool first = true;
        File root = LittleFS.open("/");
        if (root) {
            File file = root.openNextFile();
            while(file) {
                String fn = String(file.name());
                if(fn.startsWith("p_") || fn.startsWith("/p_")) {
                    DynamicJsonDocument doc(4096);
                    DeserializationError err = deserializeJson(doc, file);
                    if (!err && doc.containsKey("name")) {
                        if(!first) json += ",";
                        json += "\"" + doc["name"].as<String>() + "\"";
                        first = false;
                    }
                }
                file = root.openNextFile();
            }
        }
        json += "]";
        server.send(200, "application/json", json);
    });

    server.on("/api/presets/save", HTTP_POST, []() {
        if (!server.hasArg("name")) {
            server.send(400, "text/plain", "Falta el nom");
            return;
        }
        String name = server.arg("name");
        String filename = "/p_" + name + ".json";
        filename.replace(" ", "_"); // Sanitització bàsica per al nom del fitxer

        DynamicJsonDocument doc(4096);
        doc["name"] = name;
        doc["ext"] = serializeColors(color_base_exterior, NUM_OUTER_LEDS);
        doc["int"] = serializeColors(color_base_interior, NUM_INNER_LEDS);
        doc["f_ext"] = exterior_crepitant;
        doc["f_int"] = interior_crepitant;

        File file = LittleFS.open(filename, "w");
        if(file) {
            serializeJson(doc, file);
            file.close();
            server.send(200, "text/plain", "OK");
        } else {
            server.send(500, "text/plain", "Error a l'escriure al FS");
        }
    });

    server.on("/api/presets/load", HTTP_POST, []() {
        if (!server.hasArg("name")) return server.send(400, "text/plain", "Falta el nom");
        
        String name = server.arg("name");
        String filename = "/p_" + name + ".json";
        filename.replace(" ", "_");
        
        File file = LittleFS.open(filename, "r");
        if(file) {
            DynamicJsonDocument doc(4096);
            if(!deserializeJson(doc, file)) {
                mode_actual = MODE_MANUAL;
                
                // Bolquem el JSON sobre la RAM
                deserializeColors(doc["ext"].as<String>(), color_base_exterior, NUM_OUTER_LEDS);
                deserializeColors(doc["int"].as<String>(), color_base_interior, NUM_INNER_LEDS);
                exterior_crepitant = doc["f_ext"].as<bool>();
                interior_crepitant = doc["f_int"].as<bool>();
                
                // Actualitzem l'estat visual immediatament
                for(int i=0; i<NUM_OUTER_LEDS; i++) if(!exterior_crepitant) tira.setPixelColor(i, tira.Color(color_base_exterior[i].r, color_base_exterior[i].g, color_base_exterior[i].b, color_base_exterior[i].w));
                for(int i=0; i<NUM_INNER_LEDS; i++) if(!interior_crepitant) tira.setPixelColor(NUM_OUTER_LEDS+i, tira.Color(color_base_interior[i].r, color_base_interior[i].g, color_base_interior[i].b, color_base_interior[i].w));
                
                if (!exterior_crepitant || !interior_crepitant) tira.show();
                
                server.send(200, "text/plain", "OK");
            } else {
                server.send(500, "text/plain", "Fitxer corrupte");
            }
            file.close();
        } else {
            server.send(404, "text/plain", "No trobat");
        }
    });

    server.on("/api/presets/delete", HTTP_POST, []() {
        if (!server.hasArg("name")) return server.send(400, "text/plain", "Falta el nom");
        
        String name = server.arg("name");
        String filename = "/p_" + name + ".json";
        filename.replace(" ", "_");
        
        if(LittleFS.remove(filename)) {
            server.send(200, "text/plain", "OK");
        } else {
            server.send(500, "text/plain", "Error a l'esborrar");
        }
    });

    server.begin();
}

// ==========================================
// RENDERITZAT: DIA (COLORS) I NIT (LLUNA)
// ==========================================
void render_astro_state() {
    tira.clear();

    if (dades_actuals.is_daytime) {
        uint32_t local_day_seed = (uint32_t)(dades_actuals.jd + 0.5 + (LONGITUD / 360.0));
        randomSeed(local_day_seed);
        
        int idx_outer = random(0, 10);
        int idx_inner = random(0, 10);
        
        if (idx_inner == idx_outer) {
            idx_inner = (idx_inner + 1) % 10;
        }

        uint32_t color_outer = tira.Color(PALETA_COLORS[idx_outer].r, PALETA_COLORS[idx_outer].g, PALETA_COLORS[idx_outer].b, PALETA_COLORS[idx_outer].w);
        uint32_t color_inner = tira.Color(PALETA_COLORS[idx_inner].r, PALETA_COLORS[idx_inner].g, PALETA_COLORS[idx_inner].b, PALETA_COLORS[idx_inner].w);

        for (int i = 0; i < NUM_OUTER_LEDS; i++) tira.setPixelColor(i, color_outer);
        for (int i = 0; i < NUM_INNER_LEDS; i++) tira.setPixelColor(NUM_OUTER_LEDS + i, color_inner);

    } else {
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
    }
    tira.show();
}

// ==========================================
// RENDERITZAT: ANIMACIÓ DE FOC MANUAL (MODULAT)
// ==========================================
void animar_foc_exterior() {
    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        int cooldown = random(0, 6);  // Refredament encara més lent
        if(calor_exterior[i] <= cooldown + 40) calor_exterior[i] = 40; // Base "brasa" perquè no s'apagui del tot
        else calor_exterior[i] -= cooldown;
    }

    if(random(255) < 200) { 
        int p = random(NUM_OUTER_LEDS);
        calor_exterior[p] = random(180, 255); 
    }
    
    if(random(255) < 120) { 
        int p = random(NUM_OUTER_LEDS);
        calor_exterior[p] = random(180, 255); 
    }

    if(random(255) < 60) { 
        int p = random(NUM_OUTER_LEDS);
        calor_exterior[p] = random(200, 255); 
    }

    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        uint8_t heat = calor_exterior[i]; 
        
        uint8_t r_mod = (color_base_exterior[i].r * heat) / 255;
        uint8_t g_mod = (color_base_exterior[i].g * heat) / 255;
        uint8_t b_mod = (color_base_exterior[i].b * heat) / 255;
        uint8_t w_mod = (color_base_exterior[i].w * heat) / 255;

        tira.setPixelColor(i, tira.Color(r_mod, g_mod, b_mod, w_mod));
    }
}

void animar_foc_interior() {
    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        int cooldown = random(0, 6);  
        if(calor_interior[i] <= cooldown + 40) calor_interior[i] = 40; // Base "brasa"
        else calor_interior[i] -= cooldown;
    }

    if(random(255) < 200) { 
        int p = random(NUM_INNER_LEDS);
        calor_interior[p] = random(180, 255); 
    }
    
    if(random(255) < 120) { 
        int p = random(NUM_INNER_LEDS);
        calor_interior[p] = random(180, 255); 
    }

    if(random(255) < 60) { 
        int p = random(NUM_INNER_LEDS);
        calor_interior[p] = random(200, 255); 
    }

    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        uint8_t heat = calor_interior[i]; 
        
        uint8_t r_mod = (color_base_interior[i].r * heat) / 255;
        uint8_t g_mod = (color_base_interior[i].g * heat) / 255;
        uint8_t b_mod = (color_base_interior[i].b * heat) / 255;
        uint8_t w_mod = (color_base_interior[i].w * heat) / 255;

        tira.setPixelColor(NUM_OUTER_LEDS + i, tira.Color(r_mod, g_mod, b_mod, w_mod));
    }
}

// ==========================================
// BUCLE PRINCIPAL (MAIN LOOP)
// ==========================================
void setup() {
    Serial.begin(115200);

    // Inicialització del sistema de fitxers. Formata automàticament si és el primer cop
    if (!LittleFS.begin(true)) {
        Serial.println("Error muntant el sistema d'arxius LittleFS!");
    }

    tira.begin();
    tira.clear();
    tira.show();

    // Inicialitzem el mapa de calor i un color base per defecte (Taronja apagat) per a les dues corones
    for(int i = 0; i < NUM_OUTER_LEDS; i++) {
        calor_exterior[i] = 40; 
        color_base_exterior[i] = {8, 2, 0, 0};
    }
    for(int i = 0; i < NUM_INNER_LEDS; i++) {
        calor_interior[i] = 40;
        color_base_interior[i] = {8, 2, 0, 0};
    }

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

    if (mode_actual == MODE_ASTRO) {
        if (ultim_calcul == 0 || temps_actual - ultim_calcul >= INTERVAL_CALCUL) {
            ultim_calcul = temps_actual;
            time_t raw_time = timeClient.getEpochTime();
            if (raw_time > 100000) { 
                dades_actuals = moonCalc.calculate(raw_time);
                render_astro_state();
            }
        }
    } 
    else if (mode_actual == MODE_MANUAL) {
        bool show_needed = false;
        
        // Animació foc Exterior
        if (exterior_crepitant && (temps_actual - ultim_frame_foc_ext >= 30)) {
            ultim_frame_foc_ext = temps_actual;
            animar_foc_exterior();
            show_needed = true;
        }
        
        // Animació foc Interior
        if (interior_crepitant && (temps_actual - ultim_frame_foc_int >= 30)) {
            ultim_frame_foc_int = temps_actual;
            animar_foc_interior();
            show_needed = true;
        }

        if (show_needed) tira.show();
    }
}