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
    unsigned matched_rate = 0;           // rate the used files belong to (== stream
                                         // rate unless the nearest-rate fallback
                                         // kicked in and their IRs were resampled)
};

// --- exposed for unit tests (pure helpers) ----------------------------------

// Digit-boundary-safe "file name contains this exact decimal rate" check:
// "EQ_44100.wav" matches 44100; "Cal_48000.wav" does NOT match 8000.
bool ir_name_matches_rate(const std::wstring & name, unsigned rate);

// Rates named in the file name that are REAL standard audio sample rates
// (8000..768000 whitelist). Tap counts (65536, 131072), dates and other
// stray numbers are never treated as rates, so they can never create
// phantom fallback candidates.
std::vector<unsigned> ir_rates_in_name(const std::wstring & name);

// Index of the rate closest to `target` by |ln(rate/target)| (2x up and 2x
// down count as equally far); ties prefer the higher rate; -1 if empty.
int ir_pick_closest_rate(const std::vector<unsigned> & rates, unsigned target);

// Average power response |H(f)|^2 of one IR channel over [f_lo, f_hi] Hz
// (falls back to the full band if the range is degenerate). Used for
// sample-rate-independent auto level matching: unlike raw sum(h^2), the
// result does not change when the IR merely carries extra ultrasonic
// bandwidth at a higher sample rate.
double ir_band_avg_power(const std::vector<double> & h, unsigned sample_rate,
                         double f_lo, double f_hi);

// ----------------------------------------------------------------------------

// Recursively scans `folder` for *.wav files and builds the calibration chain
// for a stream at `sample_rate`. Identification is strictly exact: a file
// belongs to rate R only when its FILE NAME contains R (digit-boundary-safe)
// AND its header sample rate equals R. Files whose name and header disagree
// are always skipped with a note - they are ambiguous and are never used or
// resampled.
//
// Pass 1 uses R == sample_rate directly. When it yields nothing usable and
// `allow_resample` is true, the standard rates named by the remaining files
// are tried instead, closest first (up to 3 fallback rates); files belonging
// to the winning rate are resampled to the stream rate with r8brain (see
// ir_resampler.h) before being chained, so `combined` is always at the
// stream rate. With `allow_resample` false there is no fallback.
//
// All usable files are convolved in series into one combined impulse
// response (equivalent to chaining them; order-independent).
//
// Returns false only when the folder itself cannot be enumerated; zero
// matches / zero usable files still return true with used_count == 0.
bool build_ir_chain(const wchar_t * folder, unsigned sample_rate,
                    unsigned stream_channels, bool allow_resample,
                    chain_result & out, std::string & err);
