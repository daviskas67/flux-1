// asmflux - Flux-1 assembler
// Translates ASM-Flux source into a raw ROM image (.flux binary).
//
// Base instructions (SPEC.md 2.3), 8-bit:
//   NOP               00xxxxxx
//   SET addr          01xxxxxx
//   CLR addr          10xxxxxx
//   JMP addr          11xxxxxx   (PC = (PC & 0xFFC0) | addr)
//
// Pseudo-instructions (SPEC.md), two-byte extensions (0xFC prefix):
//   GET addr          ACC = grid[addr]
//   PUT addr          grid[addr] = ACC
//   XOR addr          ACC ^= grid[addr]
//   JZ addr           if ACC == 0 then jump within the 64-byte block
//
// Audio control (three channels, SPEC.md 3):
//   FREQ ch val16     set channel frequency (Hz)
//   DUTY ch val8      set pulse width (0..255)
//   VOL  ch val8      set volume (0..255)
//   MOD  ch src depth dest  set modulation:
//                         src: 0x00..0xFF grid bit | 0xF0..0xF2 channel | 0xFF off
//                         depth: 8.8 fixed point (256 = 1.0)
//                         dest: 0=freq 1=duty 2=volume
//
// Directives:
//   .org N            set current address
//   .word v1, v2,..   emit 16-bit words (little-endian)
//   .byte v1, v2,..   emit bytes
//   .incbin "file"    include raw bytes
//   LABEL:            define a label at current address
//   ; or #            comment to end of line
//
// Usage: asmflux in.asm [-o out.flux] [-l list.txt]
// Compile: g++ -O2 -std=c++17 asmflux.cpp -o asmflux
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <string>
#include <vector>
#include <map>
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstdint>

using std::string;
using std::vector;
using std::map;

static string trim(const string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}
static bool parseLong(const string& s, long& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtol(s.c_str(), &end, 0);
    return end && *end == '\0';
}
static bool parseDbl(const string& s, double& v) {
    if (s.empty()) return false;
    char* end = nullptr;
    v = strtod(s.c_str(), &end);
    return end && *end == '\0';
}
static vector<string> tokens(const string& line) {
    vector<string> out;
    std::istringstream iss(line);
    string t;
    while (iss >> t) {
        // allow comma-separated lists: "0x01, 0x02"
        if (!t.empty() && t.back() == ',') t.pop_back();
        if (!t.empty()) out.push_back(t);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr,
            "asmflux - Flux-1 assembler\n"
            "usage: asmflux in.asm [-o out.flux] [-l list.txt]\n");
        return 1;
    }
    string inFile = argv[1];
    string outFile = "out.flux";
    string listFile;
    for (int i = 2; i < argc; ++i) {
        if (string(argv[i]) == "-o" && i + 1 < argc) outFile = argv[++i];
        else if (string(argv[i]) == "-l" && i + 1 < argc) listFile = argv[++i];
    }

    std::ifstream fin(inFile);
    if (!fin) { fprintf(stderr, "asmflux: cannot open %s\n", inFile.c_str()); return 1; }

    // ---- pass 1: collect lines, strip comments, find labels ----
    struct Line { int num; string text; };
    vector<Line> lines;
    map<string, long> labels;
    long curAddr = 0;
    string line;
    while (std::getline(fin, line)) {
        // strip comment: ; or # (but not inside a quoted string)
        size_t i = 0;
        bool inq = false;
        for (; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') inq = !inq;
            else if (!inq && (c == ';' || c == '#')) break;
        }
        line = line.substr(0, i);
        string t = trim(line);
        if (t.empty()) continue;

        // label definition: "NAME:"
        size_t colon = t.find(':');
        if (colon != string::npos) {
            string name = trim(t.substr(0, colon));
            if (!name.empty()) labels[name] = curAddr;
            string rest = trim(t.substr(colon + 1));
            if (rest.empty()) continue;
            t = rest;
        }
        lines.push_back({ (int)lines.size() + 1, t });

        vector<string> tok = tokens(t);
        if (tok.empty()) continue;
        // estimate size for the next line's address
        string m = tok[0];
        if (m == ".org") { long v; if (tok.size() > 1 && parseLong(tok[1], v)) curAddr = v; }
        else if (m == ".word") curAddr += (long)(tok.size() - 1) * 2;
        else if (m == ".byte") curAddr += (long)(tok.size() - 1);
        else if (m == ".incbin") { /* handled in pass 2; count unknown size below */ }
        else if (m == "NOP" || m == "SET" || m == "CLR" || m == "JMP") curAddr += 1;
        else if (m == "GET" || m == "PUT" || m == "XOR" || m == "JZ") curAddr += 3;
        else if (m == "JMPF" || m == "JZF") curAddr += 4;
        else if (m == "COPY") curAddr += 5;
        else if (m == "FREQ") curAddr += 6;
        else if (m == "DUTY" || m == "VOL") curAddr += 6;
        else if (m == "MOD") curAddr += 18;
        else { fprintf(stderr, "asmflux:%d: unknown mnemonic '%s'\n", lines.size() + 1, m.c_str()); }
    }

    // ---- pass 2: emit bytes ----
    vector<uint8_t> out;
    curAddr = 0;
    auto ensure = [&](size_t n) { if (out.size() < n) out.resize(n); };
    auto emit = [&](uint8_t b) { if (out.size() <= (size_t)curAddr) out.resize(curAddr + 1); out[curAddr++] = b; };
    auto emit16 = [&](uint16_t v) { emit((uint8_t)(v & 0xFF)); emit((uint8_t)(v >> 8)); };
    auto resolveLabel = [&](const string& s, bool& ok) -> long {
        ok = true;
        long v;
        if (parseLong(s, v)) return v;
        // support "label+offset" / "label-offset"
        auto plus = s.find('+');
        auto minus = s.find('-');
        if (plus != string::npos || minus != string::npos) {
            string base = s.substr(0, (plus != string::npos) ? plus : minus);
            string off = s.substr((plus != string::npos) ? plus + 1 : minus + 1);
            auto it = labels.find(base);
            long delta = 0;
            if (it == labels.end() || !parseLong(off, delta)) { ok = false; return 0; }
            long sign = (plus != string::npos) ? 1 : -1;
            return it->second + sign * delta;
        }
        auto it = labels.find(s);
        if (it == labels.end()) { ok = false; return 0; }
        return it->second;
    };

    for (const Line& L : lines) {
        vector<string> tok = tokens(L.text);
        if (tok.empty()) continue;
        string m = tok[0];

        if (m == ".org") {
            long v; if (tok.size() > 1 && parseLong(tok[1], v)) curAddr = v;
        } else if (m == ".byte") {
            for (size_t k = 1; k < tok.size(); ++k) {
                bool ok; long v = resolveLabel(tok[k], ok);
                if (!ok) { fprintf(stderr, "asmflux:%d: bad value '%s'\n", L.num, tok[k].c_str()); return 1; }
                emit((uint8_t)(v & 0xFF));
            }
        } else if (m == ".word") {
            for (size_t k = 1; k < tok.size(); ++k) {
                bool ok; long v = resolveLabel(tok[k], ok);
                if (!ok) { fprintf(stderr, "asmflux:%d: bad value '%s'\n", L.num, tok[k].c_str()); return 1; }
                emit16((uint16_t)(v & 0xFFFF));
            }
        } else if (m == "NOP") {
            emit(0x00);
        } else if (m == "SET" || m == "CLR") {
            long v; bool ok = tok.size() > 1 && parseLong(tok[1], v);
            if (!ok) { fprintf(stderr, "asmflux:%d: %s needs an address\n", L.num, m.c_str()); return 1; }
            emit((uint8_t)(((m == "SET" ? 1 : 2) << 6) | (v & 0x3F)));
        } else if (m == "JMP") {
            bool ok; long v = resolveLabel(tok.size() > 1 ? tok[1] : "", ok);
            if (!ok) { fprintf(stderr, "asmflux:%d: JMP needs a label/address\n", L.num); return 1; }
            emit((uint8_t)((3 << 6) | (v & 0x3F)));
        } else if (m == "GET" || m == "PUT" || m == "XOR" || m == "JZ") {
            bool ok; long v = resolveLabel(tok.size() > 1 ? tok[1] : "", ok);
            if (!ok) { fprintf(stderr, "asmflux:%d: %s needs an address\n", L.num, m.c_str()); return 1; }
            uint8_t sub = (m == "GET") ? 0x00 : (m == "PUT") ? 0x01 : (m == "XOR") ? 0x02 : 0x03;
            emit(0xFC); emit(sub); emit((uint8_t)(v & 0xFF));
        } else if (m == "JMPF" || m == "JZF") {
            bool ok; long v = resolveLabel(tok.size() > 1 ? tok[1] : "", ok);
            if (!ok) { fprintf(stderr, "asmflux:%d: %s needs a label/address\n", L.num, m.c_str()); return 1; }
            emit(0xFC); emit((uint8_t)(m == "JMPF" ? 0x05 : 0x06));
            emit16((uint16_t)(v & 0xFFFF));
        } else if (m == "COPY") {
            long dst;
            if (tok.size() < 3 || !parseLong(tok[1], dst)) {
                fprintf(stderr, "asmflux:%d: COPY dst src\n", L.num); return 1;
            }
            bool ok; long src = resolveLabel(tok[2], ok);
            if (!ok) { fprintf(stderr, "asmflux:%d: bad COPY src\n", L.num); return 1; }
            emit(0xFC); emit(0x07); emit((uint8_t)(dst & 0xFF));
            emit16((uint16_t)(src & 0xFFFF));
        } else if (m == "FREQ") {
            long ch, v; bool ok = tok.size() > 2 && parseLong(tok[1], ch) && parseLong(tok[2], v);
            if (!ok) { fprintf(stderr, "asmflux:%d: FREQ ch val16\n", L.num); return 1; }
            emit(0xFC); emit(0x04); emit((uint8_t)ch); emit(0x00);
            emit16((uint16_t)(v & 0xFFFF));
        } else if (m == "DUTY" || m == "VOL") {
            long ch, v; bool ok = tok.size() > 2 && parseLong(tok[1], ch) && parseLong(tok[2], v);
            if (!ok) { fprintf(stderr, "asmflux:%d: %s ch val\n", L.num, m.c_str()); return 1; }
            emit(0xFC); emit(0x04); emit((uint8_t)ch); emit((uint8_t)(m == "DUTY" ? 1 : 2));
            emit16((uint16_t)(v & 0xFF));
        } else if (m == "MOD") {
            long ch, src, depth, dest;
            bool ok = tok.size() > 4 && parseLong(tok[1], ch) && parseLong(tok[2], src) &&
                      parseLong(tok[3], depth) && parseLong(tok[4], dest);
            if (!ok) { fprintf(stderr, "asmflux:%d: MOD ch src depth dest\n", L.num); return 1; }
            // mod_src (param 3), mod_depth (param 4), mod_dest (param 5)
            emit(0xFC); emit(0x04); emit((uint8_t)ch); emit(0x03);
            emit16((uint16_t)(src & 0xFF));
            emit(0xFC); emit(0x04); emit((uint8_t)ch); emit(0x04);
            emit16((uint16_t)(depth & 0xFFFF));
            emit(0xFC); emit(0x04); emit((uint8_t)ch); emit(0x05);
            emit16((uint16_t)(dest & 0xFF));
        } else {
            fprintf(stderr, "asmflux:%d: unknown mnemonic '%s'\n", L.num, m.c_str());
            return 1;
        }
    }

    // write output
    std::ofstream fout(outFile, std::ios::binary);
    if (!fout) { fprintf(stderr, "asmflux: cannot write %s\n", outFile.c_str()); return 1; }
    fout.write((const char*)out.data(), (std::streamsize)out.size());

    // listing
    if (!listFile.empty()) {
        std::ofstream fl(listFile);
        for (size_t i = 0; i < out.size(); ++i) {
            if (i % 16 == 0) fl << std::hex << i << ":\t";
            fl << std::hex << (int)out[i] << " ";
            if (i % 16 == 15) fl << "\n";
        }
        fl << "\n";
    }

    printf("asmflux: %s -> %s (%zu bytes)\n", inFile.c_str(), outFile.c_str(), out.size());
    return 0;
}