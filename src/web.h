#pragma once
#include <Arduino.h>

void webBegin(bool apMode);
void webLoop();          // DNS (captive portal), odloženi ponovni zagon
