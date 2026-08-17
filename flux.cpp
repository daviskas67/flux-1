// flux - Flux-1 emulator front-end
// Loads a .flux ROM image and runs it with ASCII 16x16 visualization,
// audio (3 channels, FM/modulation) and WAV export.
//
// Usage:
//   flux rom.flux [options]
//     --tps N        CPU ticks per second for animation (default 60)
//     --frames N     redraw every N ticks (default 1)
//     --wav out.wav  render audio to a WAV file and exit
//     --wav-rate HZ  sample rate for WAV (default 44100, 22050 for 8086)
//     --secs S       number of seconds to render (default 5)
//     --headless     no animation; just run --secs worth of ticks
//     --acc          show register/counters line
//     --steps N      run N CPU instructions and exit (for testing)
//     -h, --help
//
// Compile: g++ -O2 -std=c++17 flux.cpp flux_core.cpp -o flux
#include "flux_core.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <vector>
#include <thread>
#include <chrono>

using namespace flux;

static std::string loadFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::string data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return data;
}

static void writeWav(const std::string& path, const std::vector<float>& samples, float rate) {
    std::ofstream w(path, std::ios::binary);
    if (!w) { fprintf(stderr, "cannot open %s\n", path.c_str()); return; }
    uint32_t n = (uint32_t)samples.size();
    uint32_t dataBytes = n * 2;
    auto wr = [&](const void* p, size_t sz) { w.write((const char*)p, sz); };
    char riff[4] = {'R','I','F','F'};
    char wave[4] = {'W','A','V','E'};
    char fmt_[4] = {'f','m','t',' '};
    char data[4] = {'d','a','t','a'};
    uint16_t audioFmt = 1, channels = 1, bits = 16;
    uint32_t byteRate = (uint32_t)rate * 2;
    uint16_t blockAlign = 2;
    uint32_t fmtSize = 16, riffSize = 36 + dataBytes;
    wr(riff, 4); wr(&riffSize, 4); wr(wave, 4);
    wr(fmt_, 4); wr(&fmtSize, 4);
    wr(&audioFmt, 2); wr(&channels, 2); wr(&rate, 4); wr(&byteRate, 4);
    wr(&blockAlign, 2); wr(&bits, 2);
    wr(data, 4); wr(&dataBytes, 4);
    for (float s : samples) {
        if (s > 1.0f) s = 1.0f; if (s < -1.0f) s = -1.0f;
        int16_t v = (int16_t)(s * 32767.0f);
        wr(&v, 2);
    }
}

static void renderFrame(const FluxCPU& cpu, bool showInfo, bool showGridHex = false) {
    printf("\x1b[H\x1b[2J");
    printf("FLUX-1  256-bit grid  3 square channels  PC=%04X ACC=%u CARRY=%d MODE=%u\n",
           cpu.pc, cpu.acc, (int)cpu.carry, cpu.mode);
    if (showInfo) {
        for (int c = 0; c < AUDIO_CHANNELS; ++c) {
            const Channel& chn = cpu.ch[c];
            printf("  ch%d freq=%u duty=%u vol=%u mod_src=%02X depth=%d dest=%u\n",
                   c, chn.freq, chn.duty, chn.volume, chn.mod_src, chn.mod_depth, chn.mod_dest);
        }
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool bit = (cpu.grid[y * 2 + (x / 8)] >> (x % 8)) & 1;
            printf("%s", bit ? "\x1b[32m" : "\x1b[90m");
            printf("%s", bit ? "\xE2\x96\x88" : "\xE2\x96\x91");
        }
        printf("\x1b[0m\n");
    }
    if (showGridHex) {
        printf("grid hex:");
        for (int i = 0; i < GRID_BYTES; ++i)
            printf(" %02X", cpu.grid[i]);
        printf("\n");
    }
    printf("cycles=%llu  pc=%04X\n",
           (unsigned long long)cpu.cycles(), cpu.pc);
    fflush(stdout);
}

int main(int argc, char** argv) {
    std::string file;
    double tps = 60.0;
    int frames = 1;
    std::string wavPath;
    float wavRate = DEF_SAMPLE_RATE;
    double secs = 5.0;
    bool headless = false, showInfo = false, showGridHex = false;
    long steps = -1;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--tps" && i + 1 < argc) tps = atof(argv[++i]);
        else if (a == "--frames" && i + 1 < argc) frames = atoi(argv[++i]);
        else if (a == "--wav" && i + 1 < argc) wavPath = argv[++i];
        else if (a == "--wav-rate" && i + 1 < argc) wavRate = (float)atof(argv[++i]);
        else if (a == "--secs" && i + 1 < argc) secs = atof(argv[++i]);
        else if (a == "--headless") headless = true;
        else if (a == "--acc") showInfo = true;
        else if (a == "--grid-hex") showGridHex = true;
        else if (a == "--steps" && i + 1 < argc) steps = atol(argv[++i]);
        else if (a == "-h" || a == "--help") {
            printf("Flux-1 emulator\n"
                   "usage: flux rom.flux [--tps N] [--frames N] [--wav out.wav]\n"
                   "                     [--wav-rate HZ] [--secs S] [--headless] [--acc]\n"
                   "                     [--grid-hex] [--steps N]\n");
            return 0;
        }
        else file = a;
    }
    if (file.empty()) {
        fprintf(stderr, "usage: flux rom.flux\n");
        return 1;
    }

    std::string rom = loadFile(file);
    if (rom.empty()) { fprintf(stderr, "cannot load %s\n", file.c_str()); return 1; }

    FluxCPU cpu;
    std::memcpy(cpu.rom.data(), rom.data(),
                rom.size() < ROM_SIZE ? rom.size() : ROM_SIZE);

    // ---- run mode: fixed instruction count ----
    if (steps >= 0) {
        cpu.run((int)steps);
        renderFrame(cpu, showInfo, showGridHex);
        return 0;
    }

    // ---- WAV mode ----
    if (!wavPath.empty()) {
        long totalTicks = (long)(secs * tps);
        long sps = (long)(wavRate / tps);
        if (sps < 1) sps = 1;
        std::vector<float> audio;
        audio.reserve((size_t)(secs * wavRate));
        for (long t = 0; t < totalTicks; ++t) {
            cpu.tick();
            float block[4096];
            for (long s = 0; s < sps; s += 4096) {
                long n = (sps - s < 4096) ? (sps - s) : 4096;
                cpu.renderBlock(block, (int)n);
                for (long k = 0; k < n; ++k) audio.push_back(block[k]);
            }
        }
        writeWav(wavPath, audio, wavRate);
        printf("wrote %s (%zu samples, %.2f s)\n", wavPath.c_str(), audio.size(), audio.size() / wavRate);
        return 0;
    }

    // ---- animation mode ----
    long sps = (long)(wavRate / tps);
    if (sps < 1) sps = 1;
    for (;;) {
        cpu.tick();
        if (cpu.cycles() % (unsigned long long)frames == 0) {
            if (!headless) renderFrame(cpu, showInfo);
            if (tps > 0)
                std::this_thread::sleep_for(std::chrono::duration<double>(1.0 / tps));
        }
    }
    return 0;
}