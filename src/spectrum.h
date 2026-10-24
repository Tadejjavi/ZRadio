#pragma once
#include <Arduino.h>

// Spektralni prikaz: knjižnica za zvok pošilja PCM (zr_pcm_tap) v krožni medpomnilnik,
// prikaz pa po potrebi izračuna FFT (1024 točk, Hannovo okno) in logaritemske pasove.
void spectrumEnable(bool on);
// bands: n vrednosti 0..1 (zglajeno). Vrne false, če ni podatkov.
bool spectrumCompute(float* bands, int n, uint32_t sampleRate);
// osciloskop: n vzorcev (mono), poravnano na prehod skozi nič navzgor (stabilna slika); vrne false brez podatkov
bool spectrumScope(int16_t* out, int n, int decim);
