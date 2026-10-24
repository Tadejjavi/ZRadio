#include <chrono>
#include <cstdio>
#include <cstdlib>
void hostDraw(int layout, int theme, const char* toast);
extern "C++" void hostBenchSetup();
int benchMain(int argc, char** argv) {
    for (int l = 0; l < 12; l++) {
        for (int i = 0; i < 5; i++) hostDraw(l, 0, nullptr);
        auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; i++) hostDraw(l, 0, nullptr);
        double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 200;
        printf("postavitev %2d: %7.0f us/sliko\n", l, us);
    }
    return 0;
}
