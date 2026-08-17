#include "flux1.hpp"
#include <cstdio>
#include <cstring>

namespace flux {

Flux::Flux() {
    std::memset(grid_, 0, sizeof(grid_));
}

int Flux::nx(int x, Dir d) const {
    switch (d) {
        case Dir::E: return x + 1;
        case Dir::W: return x - 1;
        default:     return x;
    }
}
int Flux::ny(int y, Dir d) const {
    switch (d) {
        case Dir::S: return y + 1;
        case Dir::N: return y - 1;
        default:     return y;
    }
}

void Flux::setRule(int x, int y, const Rule& r) {
    if (inbounds(x, y)) rules_[y * kGrid + x].push_back(r);
}

void Flux::seedBit(int x, int y, uint8_t v) {
    if (inbounds(x, y)) grid_[y * kGrid + x].bit = v & 1;
}
void Flux::seedAcc(int x, int y, uint8_t v) {
    if (inbounds(x, y)) grid_[y * kGrid + x].acc = v & 1;
}
void Flux::injectWave(int x, int y, Dir dir, uint8_t bit, uint8_t delay) {
    if (!inbounds(x, y)) return;
    grid_[y * kGrid + x].bit = bit & 1;
    grid_[y * kGrid + x].dir = dir;
    grid_[y * kGrid + x].delay = delay;
}

// ---- rule matching ----
static bool condOk(Cond c, uint8_t bit, uint8_t acc) {
    switch (c) {
        case Cond::Always: return true;
        case Cond::Bit1:   return bit == 1;
        case Cond::Bit0:   return bit == 0;
        case Cond::Acc1:   return acc == 1;
        case Cond::Acc0:   return acc == 0;
    }
    return false;
}

// advance sps_ audio samples across all channels (square waves + FM)
void Flux::renderAudio() {
    float dt = (float)sps_ / sampleRate_;
    float dtw = dt * 2.0f * 3.14159265f;
    for (int s = 0; s < sps_; ++s) {
        float out = 0.0f;
        for (int i = 0; i < kChans; ++i) {
            Chan& c = chans_[i];
            if (c.freq <= 0.0f) { c.last = 0.0f; continue; }
            // instantaneous frequency: carrier + FM depth * modulator last sample
            float inst = c.freq;
            if (c.modSrc >= 0 && c.modSrc < kChans) {
                inst += c.modDepth * chans_[c.modSrc].last;
            }
            c.phase += inst * dtw;
            c.last = c.amp * (std::sin(c.phase) >= 0.0f ? 1.0f : -1.0f);  // square
            out += c.last;
        }
        audio_.push_back(out);
    }
}

void Flux::step() {
    // Pending transport: bit about to land on (x,y) with a charge delay.
    struct Move { int x, y; uint8_t bit, delay; };
    std::vector<Move> moves;
    // Cells that keep their wave (didn't move / no consuming rule fired).
    // (index, remaining delay) pairs.
    std::vector<std::pair<int, uint8_t>> stay;

    // -------- phase A: evaluate every live cell --------
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            Cell& c = grid_[y * kGrid + x];
            bool hasRules = !rules_[y * kGrid + x].empty();
            if (!c.bit && !hasRules) continue;
            bool hadBit = c.bit;

            if (c.bit && c.delay > 0) {
                --c.delay;          // charging
                stay.emplace_back(y * kGrid + x, c.delay);
                continue;
            }

            // act now (emit works even with no local bit).
            bool consumed = false;
            for (const Rule& r : rules_[y * kGrid + x]) {
                if (!condOk(r.cond, c.bit, c.acc)) continue;
                switch (r.action) {
                    case Action::Send: {
                        if (c.bit) {
                            Dir d = (r.dir == Dir::Hold) ? c.dir : r.dir;
                            int tx = nx(x, d), ty = ny(y, d);
                            if (inbounds(tx, ty))
                                moves.push_back({tx, ty, c.bit, r.delay});
                            // else: wave leaves the grid, dies.
                        }
                        consumed = true;
                        break;
                    }
                    case Action::Emit: {
                        Dir d = (r.dir == Dir::Hold) ? c.dir : r.dir;
                        int tx = nx(x, d), ty = ny(y, d);
                        if (inbounds(tx, ty))
                            moves.push_back({tx, ty, (uint8_t)(r.val & 1), r.delay});
                        break;   // generator: own bit stays (not consumed)
                    }
                    case Action::Stop: {
                        consumed = true;   // absorb the wave
                        break;
                    }
                    case Action::SetAcc: {
                        c.acc = r.val & 1;
                        break;
                    }
                    case Action::ClearAcc: {
                        c.acc = 0;
                        break;
                    }
                    case Action::ToggleAcc: {
                        c.acc ^= 1;
                        break;
                    }
                    case Action::ChanFreq: {
                        if (r.chan < kChans) chans_[r.chan].freq = r.fval;
                        break;
                    }
                    case Action::ChanAmp: {
                        if (r.chan < kChans) chans_[r.chan].amp = r.fval;
                        break;
                    }
                    case Action::ChanPhase: {
                        if (r.chan < kChans) chans_[r.chan].phase += r.fval;
                        break;
                    }
                    case Action::ChanMod: {
                        if (r.chan < kChans) {
                            chans_[r.chan].modSrc = (r.src < kChans) ? r.src : -1;
                            chans_[r.chan].modDepth = r.fval;
                        }
                        break;
                    }
                }
                if (consumed) break;
            }
            if (!consumed && hadBit) {
                // acc-only ops or no match: wave lingers
                stay.emplace_back(y * kGrid + x, 0);
            }
        }
    }

    // -------- phase B: synchronous commit --------
    uint8_t accSnap[kGrid * kGrid];
    for (int i = 0; i < kGrid * kGrid; ++i) accSnap[i] = grid_[i].acc;
    std::memset(grid_, 0, sizeof(grid_));
    for (int i = 0; i < kGrid * kGrid; ++i) grid_[i].acc = accSnap[i];
    for (auto& [idx, d] : stay) { grid_[idx].bit = 1; grid_[idx].delay = d; }
    for (const Move& m : moves) {
        int idx = m.y * kGrid + m.x;
        if (grid_[idx].bit) continue;         // occupied: collision, keep first
        grid_[idx].bit = m.bit & 1;
        grid_[idx].delay = m.delay;
        grid_[idx].dir  = (m.bit == 1) ? Dir::Hold : Dir::Hold;
    }

    // rebuild wave list for inspection/rendering
    waves_.clear();
    for (int y = 0; y < kGrid; ++y)
        for (int x = 0; x < kGrid; ++x) {
            const Cell& c = grid_[y * kGrid + x];
            if (c.bit) {
                Wave w;
                w.x = x; w.y = y;
                w.bit = c.bit;
                w.delay = c.delay;
                w.dir = c.dir;
                w.age = tick_;
                w.dead = 0;
                waves_.push_back(w);
            }
        }
    // audio: render one sample per grid tick
    renderAudio();
    ++tick_;
}

void Flux::run(int ticks) {
    for (int i = 0; i < ticks; ++i) step();
}

std::string Flux::render(bool showAcc) const {
    std::string out;
    char buf[64];
    for (int y = 0; y < kGrid; ++y) {
        for (int x = 0; x < kGrid; ++x) {
            const Cell& c = grid_[y * kGrid + x];
            char ch = '.';
            if (c.bit)      ch = (c.delay > 0) ? 'o' : '#';
            else if (c.acc) ch = (showAcc) ? 'A' : '.';
            out += ch;
            out += ' ';
        }
        out += '\n';
    }
    std::snprintf(buf, sizeof(buf), "tick=%llu  waves=%zu\n",
                  (unsigned long long)tick_, waves_.size());
    out += buf;
    return out;
}

} // namespace flux