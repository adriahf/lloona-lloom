#pragma once
#include <Arduino.h>

struct ColorRGBW {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t w;
    bool f; // Flag de Foc/Crepitació
};

struct DayColorSelection {
    uint32_t dayKey;
    uint16_t variant;
    int outerIndex;
    int innerIndex;
    ColorRGBW outer;
    ColorRGBW inner;
};

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

struct CalibrationSample {
    const char* id;
    uint8_t r, g, b, w;
};

enum SystemMode { MODE_ASTRO, MODE_SIMULACIO, MODE_MANUAL, MODE_CALIBRATION };
