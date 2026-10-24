#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

struct Station {
    String name;
    String url;
    String genre;
    bool   fav = false;
};

static constexpr int MAX_STATIONS = 250;

extern std::vector<Station> stations;

bool loadStations();
bool saveStations();
void defaultStations();
void stationsToJson(JsonArray a);
bool stationsFromJson(JsonArrayConst a, std::vector<Station>& out, String& err);
