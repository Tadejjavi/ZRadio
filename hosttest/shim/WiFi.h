#pragma once
#define WL_CONNECTED 3
struct WiFiC { int status() { return 3; } int RSSI() { return -60; } };
extern WiFiC WiFi;
