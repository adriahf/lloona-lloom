#ifndef LLOONA_WEB_API_H
#define LLOONA_WEB_API_H

// UI indices run clockwise from 12 o'clock. Existing scene files keep their
// physical order; reversing is its own inverse, so old scenes need no migration.
String reverseRingHex(const String& hex, int count) {
    String result;
    result.reserve(count * 9);
    for (int i = 0; i < count; ++i) {
        int offset = ((count - i) % count) * 9;
        result += hex.substring(offset, offset + 9);
    }
    return result;
}

bool validRingHex(const String& hex, int count) {
    if (hex.length() != count * 9) return false;
    for (int i = 0; i < count * 9; ++i) {
        char c = hex[i];
        if (i % 9 == 8) {
            if (c != '0' && c != '1') return false;
        } else if (!isxdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

bool validScene(JsonDocument& doc) {
    return doc["ext"].is<String>() && doc["int"].is<String>() &&
        validRingHex(doc["ext"].as<String>(), NUM_OUTER_LEDS) &&
        validRingHex(doc["int"].as<String>(), NUM_INNER_LEDS);
}

void sendJson(JsonDocument& doc) {
    String json;
    serializeJson(doc, json);
    server.sendHeader("Cache-Control", "no-store");
    server.send(200, "application/json; charset=utf-8", json);
}

String liveRingHex(int start, int count) {
    String result;
    result.reserve(count * 9);
    char pixel[10];
    for (int i = 0; i < count; ++i) {
        int physical = (count - i) % count;
        uint32_t color = tira.getPixelColor(start + physical);
        bool fire = mode_actual == MODE_MANUAL &&
            (start == 0 ? color_base_exterior[physical].f : color_base_interior[physical].f);
        snprintf(pixel, sizeof(pixel), "%02X%02X%02X%02X%d",
            (unsigned)((color >> 16) & 255), (unsigned)((color >> 8) & 255),
            (unsigned)(color & 255), (unsigned)((color >> 24) & 255), fire ? 1 : 0);
        result += pixel;
    }
    return result;
}

void applyManualScene() {
    mode_actual = MODE_MANUAL;
    has_fire_ext = false;
    has_fire_int = false;
    for (int i = 0; i < NUM_OUTER_LEDS; ++i) {
        const ColorRGBW& c = color_base_exterior[i];
        has_fire_ext |= c.f;
        tira.setPixelColor(i, tira.Color(c.r, c.g, c.b, c.w));
    }
    for (int i = 0; i < NUM_INNER_LEDS; ++i) {
        const ColorRGBW& c = color_base_interior[i];
        has_fire_int |= c.f;
        tira.setPixelColor(NUM_OUTER_LEDS + i, tira.Color(c.r, c.g, c.b, c.w));
    }
    tira.show();
}

bool presetPath(String& name, String& path) {
    if (!server.hasArg("name")) {
        server.send(400, "text/plain", "Falta el nom de l'escena.");
        return false;
    }
    name = server.arg("name");
    name.trim();
    if (name.isEmpty() || name.length() > 80) {
        server.send(400, "text/plain", "El nom ha de tenir entre 1 i 20 caràcters.");
        return false;
    }
    unsigned characters = 0;
    for (unsigned i = 0; i < name.length(); ++i) {
        unsigned char c = name[i];
        if ((c & 0xC0) != 0x80) ++characters;
        if (c < 32 || c == 127 || c == '/' || c == '\\') {
            server.send(400, "text/plain", "El nom conté caràcters no permesos.");
            return false;
        }
    }
    if (characters > 20) {
        server.send(400, "text/plain", "El nom ha de tenir com a màxim 20 caràcters.");
        return false;
    }
    path = "/p_" + name + ".json";
    path.replace(" ", "_");
    return true;
}

bool readPreset(const String& name, const String& path, JsonDocument& doc) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        server.send(404, "text/plain", "No s'ha trobat l'escena.");
        return false;
    }
    DeserializationError error = deserializeJson(doc, file);
    file.close();
    if (error || !validScene(doc)) {
        server.send(422, "text/plain", "No es pot llegir aquesta escena.");
        return false;
    }
    if (doc["name"].as<String>() != name) {
        server.send(409, "text/plain", "Aquest nom coincideix amb un altre fitxer. Tria un altre nom.");
        return false;
    }
    return true;
}

void render_astro_state();

void setup_web() {
    server.on("/", HTTP_GET, []() {
        server.sendHeader("Cache-Control", "no-cache");
        server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
    });
    server.on("/api/status", HTTP_GET, []() {
        // The lamp can stay in manual mode for days. Keep the sky information
        // current without touching its LED output or the simulation timestamp.
        MoonData sky = mode_actual != MODE_SIMULACIO && timeClient.isTimeSet()
            ? moonCalc.calculate(timeClient.getEpochTime()) : dades_actuals;
        DynamicJsonDocument doc(4096);
        doc["mode"] = mode_actual == MODE_MANUAL ? "manual" :
            (mode_actual == MODE_SIMULACIO ? "simulacio" : "astro");
        doc["sim_ts"] = simulacio_timestamp;
        doc["fraction"] = sky.fraction;
        doc["phase_age"] = sky.phaseAge;
        doc["is_waxing"] = sky.isWaxing;
        doc["is_daytime"] = sky.is_daytime;
        doc["timestamp"] = timeClient.getEpochTime();
        doc["clock_synced"] = timeClient.isTimeSet();
        doc["ext"] = liveRingHex(0, NUM_OUTER_LEDS);
        doc["int"] = liveRingHex(NUM_OUTER_LEDS, NUM_INNER_LEDS);
        sendJson(doc);
    });
    server.on("/api/mode", []() {
        String mode = server.arg("set");
        if (mode == "manual") applyManualScene();
        else if (mode == "astro") {
            mode_actual = MODE_ASTRO;
            has_fire_ext = has_fire_int = false;
            ultim_calcul = 0;
            if (timeClient.isTimeSet()) {
                dades_actuals = moonCalc.calculate(timeClient.getEpochTime());
                render_astro_state();
            }
        } else {
            server.send(400, "text/plain", "Mode invàlid. Per simular, envia una data.");
            return;
        }
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/simulate", []() {
        String input = server.arg("ts");
        char* end;
        long long timestamp = strtoll(input.c_str(), &end, 10);
        // A finite, explicit range also works on ESP32 cores with 32-bit time_t.
        if (input.isEmpty() || *end || timestamp < 0 || timestamp > 4102444799LL ||
            (sizeof(time_t) < 8 && timestamp > 2147483647LL)) {
            server.send(400, "text/plain", "Data fora del rang admès pel dispositiu.");
            return;
        }
        simulacio_timestamp = static_cast<time_t>(timestamp);
        mode_actual = MODE_SIMULACIO;
        has_fire_ext = has_fire_int = false;
        dades_actuals = moonCalc.calculate(simulacio_timestamp);
        render_astro_state();
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/led", HTTP_POST, []() {
        DynamicJsonDocument doc(4096);
        if (!server.hasArg("plain") || deserializeJson(doc, server.arg("plain")) || !validScene(doc)) {
            server.send(400, "text/plain", "Dades RGBW invàlides. Calen dos anells complets.");
            return;
        }
        // Validate both rings before changing anything; applying is one action.
        deserializeColors(reverseRingHex(doc["ext"].as<String>(), NUM_OUTER_LEDS), color_base_exterior, NUM_OUTER_LEDS);
        deserializeColors(reverseRingHex(doc["int"].as<String>(), NUM_INNER_LEDS), color_base_interior, NUM_INNER_LEDS);
        applyManualScene();
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/colors", HTTP_GET, []() {
        DynamicJsonDocument doc(4096);
        doc["ext"] = reverseRingHex(serializeColors(color_base_exterior, NUM_OUTER_LEDS), NUM_OUTER_LEDS);
        doc["int"] = reverseRingHex(serializeColors(color_base_interior, NUM_INNER_LEDS), NUM_INNER_LEDS);
        sendJson(doc);
    });
    server.on("/api/presets/list", HTTP_GET, []() {
        // Stream names only. Previews are fetched individually to bound heap use.
        server.sendHeader("Cache-Control", "no-store");
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "application/json; charset=utf-8", "");
        server.sendContent("[");
        bool first = true;
        File root = LittleFS.open("/");
        if (root) {
            File file = root.openNextFile();
            while (file) {
                String filename = file.name();
                if (filename.startsWith("p_") || filename.startsWith("/p_")) {
                    DynamicJsonDocument doc(4096);
                    if (!deserializeJson(doc, file) && doc["name"].is<String>()) {
                        String escaped;
                        serializeJson(doc["name"], escaped);
                        if (!first) server.sendContent(",");
                        server.sendContent(escaped);
                        first = false;
                    }
                }
                file.close();
                file = root.openNextFile();
            }
            root.close();
        }
        server.sendContent("]");
        server.sendContent("");
    });
    server.on("/api/presets/get", HTTP_GET, []() {
        String name, path;
        if (!presetPath(name, path)) return;
        DynamicJsonDocument doc(4096);
        if (!readPreset(name, path, doc)) return;
        doc["ext"] = reverseRingHex(doc["ext"].as<String>(), NUM_OUTER_LEDS);
        doc["int"] = reverseRingHex(doc["int"].as<String>(), NUM_INNER_LEDS);
        sendJson(doc);
    });
    server.on("/api/presets/save", HTTP_POST, []() {
        String name, path;
        if (!presetPath(name, path)) return;
        DynamicJsonDocument doc(4096);
        if (LittleFS.exists(path)) {
            if (!readPreset(name, path, doc)) return;
            doc.clear();
        }
        if (server.hasArg("plain") && !server.arg("plain").isEmpty()) {
            if (deserializeJson(doc, server.arg("plain")) || !validScene(doc)) {
                server.send(400, "text/plain", "No s'ha desat: dades RGBW invàlides.");
                return;
            }
            doc["ext"] = reverseRingHex(doc["ext"].as<String>(), NUM_OUTER_LEDS);
            doc["int"] = reverseRingHex(doc["int"].as<String>(), NUM_INNER_LEDS);
        } else {
            // Preserve compatibility with the original save endpoint.
            doc["ext"] = serializeColors(color_base_exterior, NUM_OUTER_LEDS);
            doc["int"] = serializeColors(color_base_interior, NUM_INNER_LEDS);
        }
        doc["name"] = name;
        const char* temporary = "/scene.tmp";
        File file = LittleFS.open(temporary, "w");
        if (!file) return server.send(507, "text/plain", "No hi ha espai per desar l'escena.");
        size_t expected = measureJson(doc);
        size_t written = serializeJson(doc, file);
        file.flush();
        file.close();
        if (written != expected || !LittleFS.rename(temporary, path)) {
            LittleFS.remove(temporary);
            server.send(500, "text/plain", "No s'ha pogut desar l'escena.");
            return;
        }
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/presets/load", HTTP_POST, []() {
        String name, path;
        if (!presetPath(name, path)) return;
        DynamicJsonDocument doc(4096);
        if (!readPreset(name, path, doc)) return;
        deserializeColors(doc["ext"].as<String>(), color_base_exterior, NUM_OUTER_LEDS);
        deserializeColors(doc["int"].as<String>(), color_base_interior, NUM_INNER_LEDS);
        applyManualScene();
        server.send(200, "text/plain", "OK");
    });
    server.on("/api/presets/delete", HTTP_POST, []() {
        String name, path;
        if (!presetPath(name, path)) return;
        DynamicJsonDocument doc(4096);
        if (!readPreset(name, path, doc)) return;
        if (LittleFS.remove(path)) server.send(200, "text/plain", "OK");
        else server.send(500, "text/plain", "No s'ha pogut eliminar l'escena.");
    });
    server.begin();
}
#endif
