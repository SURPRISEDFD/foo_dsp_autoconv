#pragma once
#include "wav_loader.h"
#include <string>
#include <vector>

// One entry per calibration file whose name matched the stream sample rate.
struct chain_file_info {
    std::wstring path;
    bool loaded = false;        // true when the file became part of the chain
    unsigned sample_rate = 0;   // rate from the file header (0 if unreadable)
    unsigned channels = 0;
    size_t frames = 0;
    std::string note;           // skip reason, or a non-fatal remark when loaded
};

struct chain_result {
    ir_data combined;                    // series convolution of every loaded file
    std::vector<chain_file_info> files;  // every matched file, in processing order
    size_t used_count = 0;               // how many files ended up in the chain
};

// Exposed for unit tests: digit-boundary-safe "file name contains this exact
// decimal rate" check: "EQ_44100.wav" matches 44100; "Cal_48000.wav" does
// NOT match 8000; "144100.wav" does NOT match 44100.
bool ir_name_matches_rate(const std::wstring & name, unsigned rate);

// Recursively scans `folder` for *.wav files whose FILE NAME contains the
// exact stream sample rate (digit-boundary-safe), loads every match,
// verifies that the actual header rate equals the stream rate (mismatches
// are skipped with a note - rate identification is strictly exact, nothing
// is ever resampled or guessed), and convolves all usable files in series
// into one combined impulse response (equivalent to chaining them; the
// result does not depend on file order).
//
// Returns false only when the folder itself cannot be enumerated; zero
// matches / zero usable files still return true with used_count == 0.
bool build_ir_chain(const wchar_t * folder, unsigned sample_rate,
                    unsigned stream_channels, chain_result & out,
                    std::string & err);
