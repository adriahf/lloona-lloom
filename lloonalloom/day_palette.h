#pragma once
// A small calibrated table, persisted once and used locally by the astro engine.
const char* DAY_PALETTE_FILE = "/day-palette-v1.json";

bool parseDayPalette(JsonVariantConst root, ColorRGBW (&pairs)[NUM_DAY_PAIRS][2], String& signature) {
    signature = root["signature"].as<String>();
    JsonArrayConst rows = root["pairs"].as<JsonArrayConst>();
    if (signature.length() != 64 || rows.size() != NUM_DAY_PAIRS) return false;
    for (char c : signature) if (!isxdigit((unsigned char)c)) return false;
    const char* rings[] = {"outer", "inner"};
    const char* channels[] = {"r", "g", "b", "w"};
    for (int i = 0; i < NUM_DAY_PAIRS; ++i) {
        for (int ring = 0; ring < 2; ++ring) {
            uint8_t values[4]; int total = 0;
            for (int j = 0; j < 4; ++j) {
                auto value = rows[i][rings[ring]][channels[j]];
                if (!value.is<int>() || value.as<int>() < 0 || value.as<int>() > 10) return false;
                values[j] = value.as<int>(); total += values[j];
            }
            // Preserve the conservative original daily level, not the manual maximum.
            if (total == 0 || total > 10) return false;
            pairs[i][ring] = {values[0], values[1], values[2], values[3], false};
        }
    }
    return true;
}

void adoptDayPalette(const ColorRGBW (&pairs)[NUM_DAY_PAIRS][2], const String& signature) {
    for (int i = 0; i < NUM_DAY_PAIRS; ++i)
        for (int ring = 0; ring < 2; ++ring) calibrated_day_pairs[i][ring] = pairs[i][ring];
    daily_palette_signature = signature;
}

void loadDayPalette() {
    File file = LittleFS.open(DAY_PALETTE_FILE, "r");
    if (!file) return;
    DynamicJsonDocument doc(4096);
    auto error = deserializeJson(doc, file); file.close();
    ColorRGBW pairs[NUM_DAY_PAIRS][2]; String signature;
    if (!error && parseDayPalette(doc.as<JsonVariantConst>(), pairs, signature)) adoptDayPalette(pairs, signature);
}

void setupDayPaletteApi() {
    onApi("/api/day/palette", HTTP_GET, []() {
        String json = "{\"signature\":\"" + daily_palette_signature + "\",\"count\":8}";
        server.send(200, "application/json", json);
    });
    onApi("/api/day/palette", HTTP_POST, []() {
        if (mode_actual == MODE_CALIBRATION) return server.send(409, "text/plain", "Finalitza la calibracio");
        if (!filesystem_ready) return server.send(503, "text/plain", "LittleFS no disponible");
        DynamicJsonDocument doc(4096);
        ColorRGBW pairs[NUM_DAY_PAIRS][2]; String signature;
        if (deserializeJson(doc, server.arg("plain")) || !parseDayPalette(doc.as<JsonVariantConst>(), pairs, signature))
            return server.send(400, "text/plain", "Paleta diaria invalida");
        File file = LittleFS.open("/day-palette-v1.tmp", "w");
        if (!file) return server.send(500, "text/plain", "No es pot desar la paleta");
        size_t written = serializeJson(doc, file); file.close();
        if (written != measureJson(doc) || !LittleFS.rename("/day-palette-v1.tmp", DAY_PALETTE_FILE)) {
            LittleFS.remove("/day-palette-v1.tmp");
            return server.send(500, "text/plain", "No s'ha pogut desar la paleta");
        }
        adoptDayPalette(pairs, signature);
        if (mode_actual == MODE_ASTRO || mode_actual == MODE_SIMULACIO) repaintCurrentMode();
        server.send(200, "application/json", "{\"ok\":true}");
    });
}
