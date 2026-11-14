#pragma once
// Included after the colour serializers: shared transport, power and persistence.
#include <functional>

#ifndef SECRET_API_TOKEN
#define SECRET_API_TOKEN ""
#endif
#ifndef SECRET_OTA_PASSWORD
#define SECRET_OTA_PASSWORD ""
#endif
#ifndef SECRET_ENABLE_OTA
#define SECRET_ENABLE_OTA true
#endif
static_assert(!SECRET_ENABLE_OTA || sizeof(SECRET_OTA_PASSWORD) > 1,
              "OTA is enabled: define a non-empty SECRET_OTA_PASSWORD in secrets.h. "
              "Set SECRET_ENABLE_OTA false only for an intentional USB-only build.");

uint32_t state_revision = 1;
String boot_id;
String control_session;
unsigned long control_session_until = 0;
bool runtime_dirty = false;
unsigned long runtime_dirty_at = 0;
bool filesystem_ready = false;
const char* RUNTIME_FILE = "/runtime-v1.json";

bool validFrame(const String& value, int count) {
    if (value.length() != (size_t)count * 9) return false;
    for (int i = 0; i < count; ++i) {
        for (int j = 0; j < 8; ++j) if (!isxdigit((unsigned char)value[i * 9 + j])) return false;
        if (value[i * 9 + 8] != '0' && value[i * 9 + 8] != '1') return false;
    }
    return true;
}

bool validPresetName(const String& name) {
    if(name.length()==0||name.length()>80)return false;
    unsigned int count=0;
    for(size_t i=0;i<name.length();++i){unsigned char c=name[i];if(c<32||c==127||c=='/'||c=='\\')return false;if((c&0xC0)!=0x80)++count;}
    return count<=20;
}

bool validTimestamp(const String& value) {
    if (value.length() == 0 || value.length() > 10) return false;
    for (size_t i = 0; i < value.length(); ++i) if (!isdigit((unsigned char)value[i])) return false;
    uint64_t timestamp = strtoull(value.c_str(), nullptr, 10);
    return timestamp >= 946684800ULL && timestamp <= 4102444800ULL;
}

bool authorized() {
    if (String(SECRET_API_TOKEN).length() == 0) return true;
    if (server.header("Authorization") == String("Bearer ") + SECRET_API_TOKEN) return true;
    server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
    return false;
}

bool sessionActive() {
    if (control_session.length() && (long)(millis() - control_session_until) >= 0) control_session = "";
    return control_session.length() > 0;
}

bool claimControl() {
    String id = server.header("X-Lloona-Session");
    if (id.length() == 0 || id.length() > 80) {
        server.send(400, "application/json", "{\"error\":\"session_required\"}");
        return false;
    }
    for (size_t i = 0; i < id.length(); ++i) if (!isalnum((unsigned char)id[i]) && id[i] != '-' && id[i] != '_') {
        server.send(400, "application/json", "{\"error\":\"invalid_session\"}");
        return false;
    }
    if (sessionActive() && control_session != id) {
        server.send(409, "application/json", "{\"error\":\"another_screen_is_controlling\"}");
        return false;
    }
    control_session = id;
    control_session_until = millis() + 120000UL;
    return true;
}

void markRuntimeDirty() {
    ++state_revision;
    runtime_dirty = true;
    runtime_dirty_at = millis();
}

void repaintCurrentMode() {
    if (!output_enabled) { showOutput(); return; }
    if (mode_actual == MODE_ASTRO) { ultim_calcul = 0; return; }
    if (mode_actual == MODE_SIMULACIO) {
        dades_actuals = moonCalc.calculate(simulacio_timestamp);
        render_astro_state();
        return;
    }
    if (mode_actual == MODE_CALIBRATION) { renderCalibrationSample(); return; }
    for (int i = 0; i < NUM_OUTER_LEDS; ++i) {
        auto c = color_base_exterior[i];
        tira.setPixelColor(i, tira.Color(c.r, c.g, c.b, c.w));
    }
    for (int i = 0; i < NUM_INNER_LEDS; ++i) {
        auto c = color_base_interior[i];
        tira.setPixelColor(NUM_OUTER_LEDS + i, tira.Color(c.r, c.g, c.b, c.w));
    }
    showOutput();
}

String serializeOutputRing(int offset, int count) {
    String result; result.reserve(count * 9);
    char buffer[10];
    for (int i = 0; i < count; ++i) {
        int physical = (count - i) % count;
        uint32_t c = tira.getPixelColor(offset + physical);
        bool fire = mode_actual == MODE_MANUAL && (offset == 0 ? color_base_exterior[physical].f : color_base_interior[physical].f);
        snprintf(buffer, sizeof(buffer), "%02X%02X%02X%02X%1d", (unsigned)((c >> 16) & 255), (unsigned)((c >> 8) & 255), (unsigned)(c & 255), (unsigned)((c >> 24) & 255), fire ? 1 : 0);
        result += buffer;
    }
    return result;
}

void saveRuntimeState() {
    if (!filesystem_ready || !runtime_dirty) return;
    DynamicJsonDocument doc(4096);
    doc["version"] = 1;
    doc["power"] = output_enabled;
    doc["mode"] = mode_actual == MODE_CALIBRATION ? (int)mode_abans_calibracio : (int)mode_actual;
    doc["simulation"] = (uint32_t)simulacio_timestamp;
    doc["ext"] = serializeColors(color_base_exterior, NUM_OUTER_LEDS);
    doc["int"] = serializeColors(color_base_interior, NUM_INNER_LEDS);
    File file = LittleFS.open("/runtime-v1.tmp", "w");
    if (!file) return;
    size_t written = serializeJson(doc, file);
    file.close();
    if (written && LittleFS.rename("/runtime-v1.tmp", RUNTIME_FILE)) runtime_dirty = false;
}

void loadRuntimeState() {
    if (!filesystem_ready) return;
    File file = LittleFS.open(RUNTIME_FILE, "r");
    if (!file) return;
    DynamicJsonDocument doc(4096);
    auto error = deserializeJson(doc, file);file.close();
    if (error || doc["version"] != 1) return;
    String ext = doc["ext"].as<String>(), inner = doc["int"].as<String>();
    if (!validFrame(ext, NUM_OUTER_LEDS) || !validFrame(inner, NUM_INNER_LEDS)) return;
    deserializeColors(ext, color_base_exterior, NUM_OUTER_LEDS);
    deserializeColors(inner, color_base_interior, NUM_INNER_LEDS);
    clampManualArray(color_base_exterior, NUM_OUTER_LEDS);
    clampManualArray(color_base_interior, NUM_INNER_LEDS);
    int mode = doc["mode"] | 0;
    mode_actual = mode == MODE_MANUAL ? MODE_MANUAL : mode == MODE_SIMULACIO ? MODE_SIMULACIO : MODE_ASTRO;
    simulacio_timestamp = doc["simulation"] | 0UL;
    if (mode_actual == MODE_SIMULACIO && simulacio_timestamp < 946684800) mode_actual = MODE_ASTRO;
    output_enabled = doc["power"] | true;
    has_fire_ext = has_fire_int = false;
    for (auto c : color_base_exterior) has_fire_ext |= c.f;
    for (auto c : color_base_interior) has_fire_int |= c.f;
    repaintCurrentMode();
}

void onApi(const char* path, HTTPMethod method, std::function<void()> handler) {
    auto wrapped = [path, method, handler]() {
        if (!authorized()) return;
        bool mutation = method != HTTP_GET || String(path) == "/api/mode" || String(path) == "/api/simulate";
        if (server.hasArg("plain") && server.arg("plain").length() > 4096) {
            server.send(413,"application/json","{\"error\":\"payload_too_large\"}");return;
        }
        bool emergency = String(path) == "/api/calibration/stop";
        if (String(path) == "/api/power" && server.hasArg("plain")) {
            StaticJsonDocument<128> power;
            if (!deserializeJson(power,server.arg("plain")) && power["on"].is<bool>() && !power["on"].as<bool>()) emergency = true;
        }
        if (mutation && !emergency && mode_actual == MODE_CALIBRATION && calibration_owner != server.header("X-Lloona-Session")) {
            server.send(409,"application/json","{\"error\":\"calibration_owned_by_another_screen\"}");return;
        }
        if (mutation && !emergency && !claimControl()) return;
        if (mutation && !emergency && server.hasHeader("If-Match") && server.header("If-Match") != String(state_revision)) {
            server.send(409,"application/json","{\"error\":\"revision_conflict\"}");return;
        }
        handler();
        if (mutation) markRuntimeDirty();
    };
    server.on(path, method, wrapped);
    server.on(String("/api/v1") + String(path).substring(4), method, wrapped);
}

void setupVersionedApi() {
    const char* headers[] = {"Authorization", "X-Lloona-Session", "If-Match"};
    server.collectHeaders(headers, 3);
    boot_id = String((uint32_t)esp_random(), HEX);
    auto info = []() {
        if (!authorized()) return;
        String json = "{\"api_version\":1,\"firmware_version\":\"1.1.1\",\"device_id\":\"" + WiFi.macAddress() + "\",\"capabilities\":{\"preview\":true,\"calibration\":true,\"day_colors\":true,\"daily_palette\":true,\"power\":true,\"scene_body\":true,\"frame\":true,\"sessions\":true}}";
        server.send(200,"application/json",json);
    };
    server.on("/api/info", HTTP_GET, info);
    server.on("/api/v1/info", HTTP_GET, info);
    auto session = []() {
        if (!authorized()) return;
        StaticJsonDocument<192> doc;
        if (deserializeJson(doc, server.arg("plain"))) {server.send(400,"application/json","{\"error\":\"invalid_json\"}");return;}
        if (doc["release"] | false) {
            if (control_session == server.header("X-Lloona-Session")) control_session = "";
        } else if (!claimControl()) return;
        server.send(200,"application/json","{\"ok\":true}");
    };
    server.on("/api/session",HTTP_POST,session);
    server.on("/api/v1/session",HTTP_POST,session);
    onApi("/api/power",HTTP_POST,[](){
        StaticJsonDocument<128> doc;
        if (deserializeJson(doc,server.arg("plain")) || !doc["on"].is<bool>()) {server.send(400,"application/json","{\"error\":\"invalid_power\"}");return;}
        bool on = doc["on"];
        if (!on) stopCalibration();
        output_enabled = on;
        repaintCurrentMode();
        server.send(200,"application/json","{\"ok\":true}");
    });
}
