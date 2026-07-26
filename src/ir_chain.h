#pragma once
#include "wav_loader.h"
#include <string>
#include <vector>

// One entry per calibration file that was considered for the chain.
struct chain_file_info {
    std::wstring path;
    bool loaded = false;        // true when the file became part of the chain
    unsigned sample_rate = 0;   // rate from the file header (0 if unreadable)
    unsigned channels = 0;
    size_t frames = 0;          // frames actually used (post-resampling)
    std::string note;           // skip reason, or a non-fatal remark when loaded
};

struct chain_result {
    ir_data combined;                    // series convolution of every loaded file
    std::vector<chain_file_info> files;  // every considered file, in processing order
    size_t used_count = 0;               // how many files ended up in the chain
    unsigned matched_rate = 0;           // rate the file names were matched against
                                         // (== stream rate unless the nearest-rate
                                         // fallback kicked in)
};

// --- exposed for unit tests (pure string/number helpers) --------------------

// Digit-boundary-safe "file name contains this decimal rate" check:
// "EQ_44100.wav" matches 44100; "Cal_48000.wav" does NOT match 8000.
bool ir_name_matches_rate(const std::wstring & name, unsigned rate);

// Every plausible sample rate (8000..1000000) named in the file name, i.e.
// every maximal digit run within that range, deduplicated, in order.
std::vector<unsigned> ir_rates_in_name(const std::wstring & name);

// Index of the rate closest to `target` by |ln(rate/target)| (so 2x up and
// 2x down count as equally far); ties prefer the higher rate; -1 if empty.
int ir_pick_closest_rate(const std::vector<unsigned> & rates, unsigned target);

// ----------------------------------------------------------------------------

// Recursively scans `folder` for *.wav files whose FILE NAME contains the
// stream sample rate (digit-boundary-safe), loads every match, verifies the
// actual header rate, and convolves all usable files in series into one
// combined impulse response (equivalent to chaining them; the result does
// not depend on file order).
//
// When no file name contains the stream rate and `allow_resample` is true,
// the rates named by the other files are tried instead, closest first (up
// to 3 fallback rates). Every loaded file whose header rate differs from
// the stream rate is then resampled to the stream rate with r8brain (see
// ir_resampler.h) before being chained, so `combined` is always at the
// stream rate. With `allow_resample` false, rate-mismatched files are
// skipped exactly as before.
//
// Returns false only when the folder itself cannot be enumerated; zero
// matches / zero usable files still return true with used_count == 0.
bool build_ir_chain(const wchar_t * folder, unsigned sample_rate,
                    unsigned stream_channels, bool allow_resample,
                    chain_result & out, std::string & err);
