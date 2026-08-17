// Flux-1 emulator CLI
// Compile: g++ -O2 -std=c++17 main.cpp flux1.cpp -o flux1
#include "flux1.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <fstream>
#include <thread>
#include <chrono>
#include <cctype>

using namespace flux;

// ----------------------------------------------------------------------
// .flux parser: "wave map" program
//   lines:
//     cell X Y : cond action args...      (rules per node)
//     cond: always | bit1 | bit0 | acc1 | acc0
//     action: send DIR [delay] | emit DIR [delay] | stop
//             | setacc V | clracc | toggleacc
//     seed X Y          (set data bit)
//     seedacc X Y       (set accumulator)
//     inject X Y DIR VAL DELAY   (inject a wave)
//     steps N
//     pause MS           (animation pause per tick)
//     rate HZ            (audio sample rate, default 44100)
//     sps N              (audio samples rendered per grid tick, default 735)
//     chan I freq F amp A [mod J D]   (configure square channel I)
//   Actions (rules/cells) additionally:
//     freq I F | amp I A | phase I R | mod I J D
//   DIR: N | E | S | W | H
//   '#' starts a comment. ';' separates statements on a line.
// ----------------------------------------------------------------------

struct ChanCfg {
    float freq = 0, amp = 0, modDepth = 0;
    int   modSrc = -1;
    bool  set = false;
};

static bool dirFrom(const std::string& s, Dir& out) {
    if (s == "N") { out = Dir::N; return true; }
    if (s == "E") { out = Dir::E; return true; }
    if (s == "S") { out = Dir::S; return true; }
    if (s == "W") { out = Dir::W; return true; }
    if (s == "H") { out = Dir::Hold; return true; }
    return false;
}
static bool condFrom(const std::string& s, Cond& out) {
    if (s == "always") { out = Cond::Always; return true; }
    if (s == "bit1")   { out = Cond::Bit1; return true; }
    if (s == "bit0")   { out = Cond::Bit0; return true; }
    if (s == "acc1")   { out = Cond::Acc1; return true; }
    if (s == "acc0")   { out = Cond::Acc0; return true; }
    return false;
}
static bool isInt(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-') i = 1;
    for (; i < s.size(); ++i) if (!isdigit((unsigned char)s[i])) return false;
    return true;
}
static bool isNum(const std::string& s) {
    if (s.empty()) return false;
    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;
    bool dot = false, digit = false;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (c == '.') { if (dot) return false; dot = true; }
        else if (isdigit((unsigned char)c)) digit = true;
        else return false;
    }
    return digit;
}

struct Program {
    std::vector<Rule> rules[kGrid * kGrid];
    std::vector<Rule> globalRules;
    std::vector<std::tuple<int,int,uint8_t>> seeds;
    std::vector<std::tuple<int,int,uint8_t>> accSeeds;
    std::vector<std::tuple<int,int,Dir,uint8_t,uint8_t>> injects;
    ChanCfg chans[kChans];
    int steps = 100;
    int pauseMs = 60;
    float rate = 44100.0f;
    int sps = 44100 / 60;
};

static bool parseFile(const std::string& path, Program& prg) {
    std::ifstream f(path);
    if (!f) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
    std::string line;
    int ln = 0;
    while (std::getline(f, line)) {
        ++ln;
        // strip comment
        size_t hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        // tokenize on whitespace and ';'
        for (auto& ch : line) if (ch == ';' || ch == ':' || ch == ',' || ch == '=') ch = ' ';
        std::istringstream iss(line);
        std::vector<std::string> tok;
        std::string t;
        while (iss >> t) tok.push_back(t);
        if (tok.empty()) continue;

        if (tok[0] == "steps") {
            if (tok.size() > 1 && isInt(tok[1])) prg.steps = atoi(tok[1].c_str());
        } else if (tok[0] == "pause") {
            if (tok.size() > 1 && isInt(tok[1])) prg.pauseMs = atoi(tok[1].c_str());
        } else if (tok[0] == "rate") {
            if (tok.size() > 1 && isNum(tok[1])) prg.rate = (float)atof(tok[1].c_str());
        } else if (tok[0] == "sps") {
            if (tok.size() > 1 && isInt(tok[1])) prg.sps = atoi(tok[1].c_str());
        } else if (tok[0] == "chan") {
            if (tok.size() >= 4 && isInt(tok[1])) {
                int i = atoi(tok[1].c_str());
                if (i >= 0 && i < kChans) {
                    ChanCfg& cc = prg.chans[i];
                    // chan I freq F amp A [mod J D]
                    for (size_t k = 2; k + 1 < tok.size(); ++k) {
                        if (tok[k] == "freq" && isNum(tok[k+1])) { cc.freq = (float)atof(tok[k+1].c_str()); cc.set = true; }
                        else if (tok[k] == "amp" && isNum(tok[k+1])) { cc.amp = (float)atof(tok[k+1].c_str()); cc.set = true; }
                        else if (tok[k] == "mod" && k + 2 < tok.size() && isInt(tok[k+1]) && isNum(tok[k+2])) {
                            cc.modSrc = atoi(tok[k+1].c_str());
                            cc.modDepth = (float)atof(tok[k+2].c_str());
                            cc.set = true;
                        }
                    }
                }
            }
        } else if (tok[0] == "seed") {
            if (tok.size() >= 3 && isInt(tok[1]) && isInt(tok[2]))
                prg.seeds.emplace_back(atoi(tok[1].c_str()), atoi(tok[2].c_str()), 1);
        } else if (tok[0] == "seedacc") {
            if (tok.size() >= 3 && isInt(tok[1]) && isInt(tok[2]))
                prg.accSeeds.emplace_back(atoi(tok[1].c_str()), atoi(tok[2].c_str()), 1);
        } else if (tok[0] == "inject") {
            if (tok.size() >= 6 && isInt(tok[1]) && isInt(tok[2]) && isInt(tok[4]) && isInt(tok[5])) {
                Dir d;
                if (dirFrom(tok[3], d))
                    prg.injects.emplace_back(atoi(tok[1].c_str()), atoi(tok[2].c_str()), d,
                                             (uint8_t)atoi(tok[4].c_str()), (uint8_t)atoi(tok[5].c_str()));
            }
        } else if (tok[0] == "cell") {
            if (tok.size() < 5) { fprintf(stderr, "%s:%d bad cell\n", path.c_str(), ln); continue; }
            int x = atoi(tok[1].c_str());
            int y = atoi(tok[2].c_str());
            Cond cond;
            if (!condFrom(tok[3], cond)) { fprintf(stderr, "%s:%d bad cond\n", path.c_str(), ln); continue; }
            Rule r; r.cond = cond; r.action = Action::Stop; r.dir = Dir::Hold; r.delay = 0; r.val = 0;
            r.chan = 0; r.src = 0; r.fval = 0.0f;
            const std::string& act = tok[4];
            if (act == "send") {
                r.action = Action::Send;
                if (tok.size() >= 6) dirFrom(tok[5], r.dir);
                if (tok.size() >= 7 && isInt(tok[6])) r.delay = (uint8_t)atoi(tok[6].c_str());
            } else if (act == "emit") {
                r.action = Action::Emit;
                if (tok.size() >= 6) dirFrom(tok[5], r.dir);
                if (tok.size() >= 7 && isInt(tok[6])) r.delay = (uint8_t)atoi(tok[6].c_str());
                r.val = 1;
            } else if (act == "stop") {
                r.action = Action::Stop;
            } else if (act == "setacc") {
                r.action = Action::SetAcc;
                if (tok.size() >= 6) r.val = (uint8_t)atoi(tok[5].c_str());
            } else if (act == "clracc") {
                r.action = Action::ClearAcc;
            } else if (act == "toggleacc") {
                r.action = Action::ToggleAcc;
            } else if (act == "freq") {
                r.action = Action::ChanFreq;
                if (tok.size() >= 6 && isInt(tok[5])) r.chan = (uint8_t)atoi(tok[5].c_str());
                if (tok.size() >= 7 && isNum(tok[6])) r.fval = (float)atof(tok[6].c_str());
            } else if (act == "amp") {
                r.action = Action::ChanAmp;
                if (tok.size() >= 6 && isInt(tok[5])) r.chan = (uint8_t)atoi(tok[5].c_str());
                if (tok.size() >= 7 && isNum(tok[6])) r.fval = (float)atof(tok[6].c_str());
            } else if (act == "phase") {
                r.action = Action::ChanPhase;
                if (tok.size() >= 6 && isInt(tok[5])) r.chan = (uint8_t)atoi(tok[5].c_str());
                if (tok.size() >= 7 && isNum(tok[6])) r.fval = (float)atof(tok[6].c_str());
            } else if (act == "mod") {
                r.action = Action::ChanMod;
                if (tok.size() >= 6 && isInt(tok[5])) r.chan = (uint8_t)atoi(tok[5].c_str());
                if (tok.size() >= 7 && isInt(tok[6])) r.src = (uint8_t)atoi(tok[6].c_str());
                if (tok.size() >= 8 && isNum(tok[7])) r.fval = (float)atof(tok[7].c_str());
            } else {
                fprintf(stderr, "%s:%d bad action '%s'\n", path.c_str(), ln, act.c_str());
                continue;
            }
            if (x >= 0 && x < kGrid && y >= 0 && y < kGrid)
                prg.rules[y * kGrid + x].push_back(r);
        } else if (tok[0] == "rule") {
            // rule applied to every cell that has no specific program
            if (tok.size() < 3) continue;
            Cond cond;
            if (!condFrom(tok[1], cond)) continue;
            Rule r; r.cond = cond; r.action = Action::Stop; r.dir = Dir::Hold; r.delay = 0; r.val = 0;
            r.chan = 0; r.src = 0; r.fval = 0.0f;
            const std::string& act = tok[2];
            if (act == "send") {
                r.action = Action::Send;
                if (tok.size() >= 4) dirFrom(tok[3], r.dir);
                if (tok.size() >= 5 && isInt(tok[4])) r.delay = (uint8_t)atoi(tok[4].c_str());
            } else if (act == "emit") {
                r.action = Action::Emit;
                if (tok.size() >= 4) dirFrom(tok[3], r.dir);
                if (tok.size() >= 5 && isInt(tok[4])) r.delay = (uint8_t)atoi(tok[4].c_str());
                r.val = 1;
            } else if (act == "stop") {
                r.action = Action::Stop;
            } else if (act == "setacc") {
                r.action = Action::SetAcc;
                if (tok.size() >= 4) r.val = (uint8_t)atoi(tok[3].c_str());
            } else if (act == "clracc") {
                r.action = Action::ClearAcc;
            } else if (act == "toggleacc") {
                r.action = Action::ToggleAcc;
            } else if (act == "freq") {
                r.action = Action::ChanFreq;
                if (tok.size() >= 4 && isInt(tok[3])) r.chan = (uint8_t)atoi(tok[3].c_str());
                if (tok.size() >= 5 && isNum(tok[4])) r.fval = (float)atof(tok[4].c_str());
            } else if (act == "amp") {
                r.action = Action::ChanAmp;
                if (tok.size() >= 4 && isInt(tok[3])) r.chan = (uint8_t)atoi(tok[3].c_str());
                if (tok.size() >= 5 && isNum(tok[4])) r.fval = (float)atof(tok[4].c_str());
            } else if (act == "phase") {
                r.action = Action::ChanPhase;
                if (tok.size() >= 4 && isInt(tok[3])) r.chan = (uint8_t)atoi(tok[3].c_str());
                if (tok.size() >= 5 && isNum(tok[4])) r.fval = (float)atof(tok[4].c_str());
            } else if (act == "mod") {
                r.action = Action::ChanMod;
                if (tok.size() >= 4 && isInt(tok[3])) r.chan = (uint8_t)atoi(tok[3].c_str());
                if (tok.size() >= 5 && isInt(tok[4])) r.src = (uint8_t)atoi(tok[4].c_str());
                if (tok.size() >= 6 && isNum(tok[5])) r.fval = (float)atof(tok[5].c_str());
            } else {
                fprintf(stderr, "%s:%d bad action '%s'\n", path.c_str(), ln, act.c_str());
                continue;
            }
            prg.globalRules.push_back(r);
        }
    }
    return true;
}

// ----------------------------------------------------------------------
static void renderFlux(const Flux& f, bool showAcc) {
    printf("%s", f.render(showAcc).c_str());
}

static bool writeWav(const std::string& path, const std::vector<float>& samples, float rate) {
    std::ofstream w(path, std::ios::binary);
    if (!w) { fprintf(stderr, "cannot open %s\n", path.c_str()); return false; }
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
    return true;
}

int main(int argc, char** argv) {
    std::string file, wavPath;
    bool showAcc = false;
    int animSteps = -1;
    int pauseMs = -1;
    bool quiet = false;
    int ticks = 0;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--acc") showAcc = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--ticks" && i + 1 < argc) ticks = atoi(argv[++i]);
        else if (a == "--steps" && i + 1 < argc) animSteps = atoi(argv[++i]);
        else if (a == "--pause" && i + 1 < argc) pauseMs = atoi(argv[++i]);
        else if (a == "--wav" && i + 1 < argc) wavPath = argv[++i];
        else if (a == "-h" || a == "--help") {
            printf("Flux-1 - 1-bit Turing storm (16x16) with 3 square FM channels\n"
                   "usage: flux1 program.flux [--acc] [--ticks N] [--steps N] [--pause MS]\n"
                   "                             [--quiet] [--wav out.wav]\n");
            return 0;
        }
        else file = a;
    }
    if (file.empty()) {
        fprintf(stderr, "usage: flux1 program.flux\n");
        return 1;
    }

    Program prg;
    if (!parseFile(file, prg)) return 1;

    Flux f;
    f.setSampleRate(prg.rate);
    f.setSamplesPerTick(prg.sps);
    for (int i = 0; i < kChans; ++i)
        if (prg.chans[i].set) {
            f.setChanFreq(i, prg.chans[i].freq);
            f.setChanAmp(i, prg.chans[i].amp);
            if (prg.chans[i].modSrc >= 0)
                f.setChanMod(i, prg.chans[i].modSrc, prg.chans[i].modDepth);
        }
    for (auto& [x, y, v] : prg.seeds)    f.seedBit(x, y, v);
    for (auto& [x, y, v] : prg.accSeeds) f.seedAcc(x, y, v);
    for (auto& [x, y, d, v, dl] : prg.injects) f.injectWave(x, y, d, v, dl);
    for (int i = 0; i < kGrid * kGrid; ++i)
        for (const Rule& r : prg.rules[i])
            f.setRule(i % kGrid, i / kGrid, r);
    for (int i = 0; i < kGrid * kGrid; ++i)
        if (prg.rules[i].empty())
            for (const Rule& r : prg.globalRules)
                f.setRule(i % kGrid, i / kGrid, r);

    int total = (ticks > 0) ? ticks : prg.steps;
    int stepEvery = (animSteps > 0) ? animSteps : 1;
    int ms = (pauseMs >= 0) ? pauseMs : prg.pauseMs;

    for (int t = 0; t <= total; ++t) {
        if (t % stepEvery == 0) {
            if (!quiet) {
                printf("\x1b[H\x1b[2J");   // clear screen
                printf("Flux-1  16x16  1-bit Turing storm\n");
                printf("==================================\n");
                renderFlux(f, showAcc);
            }
            if (ms > 0 && t < total)
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
        }
        f.step();
    }
    if (quiet) renderFlux(f, showAcc);
    if (!wavPath.empty()) {
        if (writeWav(wavPath, f.audio(), prg.rate))
            printf("wrote %s (%zu samples)\n", wavPath.c_str(), f.audio().size());
    }
    return 0;
}