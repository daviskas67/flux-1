#pragma once
// Flux-1: massively-parallel 1-bit Turing storm
// 16x16 grid of 1-bit compute nodes ("cells").
// Each cell: 1 data bit, 1 accumulator bit, neighbor links (N/S/E/W),
// and a tiny rule-program. Programmed with "wave" flows.
#include <cstdint>
#include <string>
#include <vector>
#include <cmath>

namespace flux {

constexpr int kGrid = 16;         // 16x16 = 256 nodes
constexpr int kMaxDelay = 15;     // transport delay budget (ticks)
constexpr int kChans  = 3;        // square-wave audio channels

enum class Dir : uint8_t { N, E, S, W, Hold };

struct Cell {
    // node state
    uint8_t bit : 1;    // data bit (current wave presence)
    uint8_t acc : 1;    // accumulator bit (persistent flag)
    uint8_t delay;      // pending transport delay (ticks) for the current bit
    Dir     dir;        // direction this node currently pushes its bit

    Cell() : bit(0), acc(0), delay(0), dir(Dir::Hold) {}
};

// A wave: a bit in transit between cells. Travels on the grid with a delay.
struct Wave {
    int      x, y;      // current cell
    Dir      dir;       // movement direction
    uint8_t  bit;       // carried value
    uint8_t  delay;     // ticks until next hop
    uint8_t  age;       // hops taken
    uint8_t  dead;
};

// Program fragment: "WHEN <cond> <action>"
enum class Cond : uint8_t { Always, Bit1, Bit0, Acc1, Acc0 };
enum class Action : uint8_t {
    Send,      // push current bit to neighbor (dir), after delay ticks
    Emit,      // force-assert 1 to neighbor (a wave generator)
    Stop,      // absorb the wave (bit dies here)
    SetAcc,    // acc = val
    ClearAcc,  // acc = 0
    ToggleAcc, // acc = !acc
    // audio / modulation
    ChanFreq,  // chan = val   (Hz)
    ChanAmp,   // chan = val   (0..1)
    ChanPhase, // chan phase += val (rad)
    ChanMod,   // chan's freq is modulated by src with depth val
};

struct Rule {
    Cond    cond;
    Action  action;
    Dir     dir;
    uint8_t delay;   // hops / ticks
    uint8_t val;     // for SetAcc
    uint8_t chan;    // target channel (0..kChans-1)
    uint8_t src;     // modulator channel
    float   fval;    // float parameter (Hz / amp / phase / depth)
};

// A square-wave audio channel. Unrestricted: freq, amp, phase accumulator
// and an optional FM modulator can be set from the grid at any tick.
struct Chan {
    float freq   = 0.0f;   // Hz
    float amp    = 0.0f;   // 0..1
    float phase  = 0.0f;   // accumulator in radians
    int   modSrc = -1;     // -1 = none, else index of modulating channel
    float modDepth = 0.0f; // FM depth (Hz offset scale)
    float last   = 0.0f;   // last sample (-1..1), used as modulator signal
};

class Flux {
public:
    Flux();

    // program access
    void setRule(int x, int y, const Rule& r);
    void seedBit(int x, int y, uint8_t v);
    void seedAcc(int x, int y, uint8_t v);
    void injectWave(int x, int y, Dir dir, uint8_t bit, uint8_t delay);

    // simulation
    void step();                    // one synchronous tick
    void run(int ticks);            // run N ticks
    uint64_t tick() const { return tick_; }

    // audio channels (modulated by the grid)
    Chan& chan(int i) { return chans_[i]; }
    const Chan& chan(int i) const { return chans_[i]; }
    void setChanFreq(int i, float hz) { chans_[i].freq = hz; }
    void setChanAmp(int i, float a)   { chans_[i].amp = a; }
    void setChanMod(int i, int src, float depth) { chans_[i].modSrc = src; chans_[i].modDepth = depth; }

    // audio configuration
    void setSampleRate(float sr)      { sampleRate_ = sr; }
    void setSamplesPerTick(int n)     { sps_ = n; }
    void clearAudio()                 { audio_.clear(); }
    const std::vector<float>& audio() const { return audio_; }

    // inspection
    const Cell& cell(int x, int y) const { return grid_[y * kGrid + x]; }
    const std::vector<Wave>& waves() const { return waves_; }

    // dump current grid to ascii art
    std::string render(bool showAcc = false) const;

private:
    int nx(int x, Dir d) const;
    int ny(int y, Dir d) const;
    bool inbounds(int x, int y) const { return x >= 0 && x < kGrid && y >= 0 && y < kGrid; }
    void renderAudio();

    Cell grid_[kGrid * kGrid];
    std::vector<Rule> rules_[kGrid * kGrid];
    std::vector<Wave> waves_;
    Chan chans_[kChans];
    float sampleRate_ = 44100.0f;
    int   sps_ = 44100 / 60;      // samples rendered per grid tick (~60 ticks/s)
    std::vector<float> audio_;    // mono mix buffer
    uint64_t tick_ = 0;
};

} // namespace flux