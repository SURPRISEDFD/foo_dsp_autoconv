#pragma once
#include <vector>
#include <string>
#include <cstdint>

struct ir_data {
    unsigned sample_rate = 0;
    unsigned channels    = 0;
    std::vector<std::vector<double>> ch; // ch[channel][frame]
    size_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

// Loads a RIFF/WAVE impulse response from disk.
// Supported: PCM 16/24/32-bit and IEEE float 32/64-bit, plain fmt or
// WAVE_FORMAT_EXTENSIBLE, any channel count up to 32.
// Returns false with a short error message on failure; never throws.
bool load_wav_ir(const wchar_t * path, ir_data & out, std::string & err);
