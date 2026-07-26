#include "ir_chain.h"
#include "ir_resampler.h"
#include "fft.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <filesystem>

namespace {

const size_t kMaxChainFiles     = 16;
const size_t kMaxCombinedFrames = 4u * 1024u * 1024u; // matches wav_loader's per-file cap
const size_t kMaxRateAttempts   = 4;  // exact rate + up to 3 nearest-rate fallbacks

bool is_digit_w(wchar_t c) { return c >= L'0' && c <= L'9'; }

bool has_wav_extension(const std::filesystem::path & p) {
    const std::wstring e = p.extension().wstring();
    return e.size() == 4 && e[0] == L'.'
        && (e[1] == L'w' || e[1] == L'W')
        && (e[2] == L'a' || e[2] == L'A')
        && (e[3] == L'v' || e[3] == L'V');
}

// Full linear convolution via FFT. Load-time only - never in the audio path.
std::vector<double> fft_convolve(const std::vector<double> & a, const std::vector<double> & b) {
    const size_t out_len = a.size() + b.size() - 1;
    size_t n = 1;
    while (n < out_len) n <<= 1;
    SimpleFFT fft;
    fft.init(n);
    std::vector<std::complex<double>> A(n), B(n);
    for (size_t i = 0; i < a.size(); ++i) A[i] = std::complex<double>(a[i], 0.0);
    for (size_t i = 0; i < b.size(); ++i) B[i] = std::complex<double>(b[i], 0.0);
    fft.forward(A.data());
    fft.forward(B.data());
    for (size_t k = 0; k < n; ++k) A[k] *= B[k];
    fft.inverse(A.data());
    std::vector<double> out(out_len);
    for (size_t i = 0; i < out_len; ++i) out[i] = A[i].real();
    return out;
}

} // namespace

bool ir_name_matches_rate(const std::wstring & name, unsigned rate) {
    const std::wstring r = std::to_wstring(rate);
    size_t pos = 0;
    while ((pos = name.find(r, pos)) != std::wstring::npos) {
        const bool digit_before = pos > 0 && is_digit_w(name[pos - 1]);
        const size_t after = pos + r.size();
        const bool digit_after = after < name.size() && is_digit_w(name[after]);
        if (!digit_before && !digit_after) return true;
        ++pos;
    }
    return false;
}

std::vector<unsigned> ir_rates_in_name(const std::wstring & name) {
    std::vector<unsigned> out;
    size_t i = 0;
    while (i < name.size()) {
        if (!is_digit_w(name[i])) { ++i; continue; }
        size_t j = i;
        while (j < name.size() && is_digit_w(name[j])) ++j;
        if (j - i <= 7) { // longer runs cannot be a sane rate
            unsigned long long v = 0;
            for (size_t k = i; k < j; ++k) v = v * 10ull + (unsigned long long)(name[k] - L'0');
            if (v >= 8000ull && v <= 1000000ull) {
                const unsigned u = (unsigned)v;
                bool dup = false;
                for (unsigned e : out) if (e == u) { dup = true; break; }
                if (!dup) out.push_back(u);
            }
        }
        i = j;
    }
    return out;
}

int ir_pick_closest_rate(const std::vector<unsigned> & rates, unsigned target) {
    int best = -1;
    double bestd = 0.0;
    for (size_t i = 0; i < rates.size(); ++i) {
        const double d = std::fabs(std::log((double)rates[i] / (double)target));
        if (best < 0 || d < bestd - 1e-12 ||
            (std::fabs(d - bestd) <= 1e-12 && rates[i] > rates[(size_t)best])) {
            best = (int)i;
            bestd = d;
        }
    }
    return best;
}

bool build_ir_chain(const wchar_t * folder, unsigned sample_rate,
                    unsigned stream_channels, bool allow_resample,
                    chain_result & out, std::string & err) {
    out = chain_result();
    namespace fs = std::filesystem;

    std::error_code ec;
    const fs::path root(folder);
    if (!fs::is_directory(root, ec) || ec) {
        err = "folder does not exist or is not accessible";
        return false;
    }

    // --- one recursive scan for every *.wav --------------------------------
    std::vector<fs::path> wavs;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) { err = "cannot enumerate folder"; return false; }
    for (; it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break; // stop on a traversal error, keep what we already have
        std::error_code ec2;
        if (!it->is_regular_file(ec2) || ec2) continue;
        if (has_wav_extension(it->path())) wavs.push_back(it->path());
    }
    std::sort(wavs.begin(), wavs.end()); // deterministic order; the math is order-independent

    // --- rates to try: the stream rate first, then (when resampling is
    //     allowed) every other rate named by any file, closest first --------
    std::vector<unsigned> attempts;
    attempts.push_back(sample_rate);
    if (allow_resample) {
        std::vector<unsigned> named;
        for (const fs::path & p : wavs) {
            for (unsigned r : ir_rates_in_name(p.filename().wstring())) {
                if (r == sample_rate) continue;
                bool dup = false;
                for (unsigned e : named) if (e == r) { dup = true; break; }
                if (!dup) named.push_back(r);
            }
        }
        std::sort(named.begin(), named.end(), [sample_rate](unsigned a, unsigned b) {
            const double da = std::fabs(std::log((double)a / (double)sample_rate));
            const double db = std::fabs(std::log((double)b / (double)sample_rate));
            if (std::fabs(da - db) > 1e-12) return da < db;
            return a > b; // ties: prefer the higher rate
        });
        for (unsigned r : named) {
            if (attempts.size() >= kMaxRateAttempts) break;
            attempts.push_back(r);
        }
    }

    // --- load & validate; the first rate that yields a usable chain wins ---
    std::vector<ir_data> irs;
    size_t run_len = 0;
    for (unsigned rate : attempts) {
        irs.clear();
        run_len = 0;

        for (const fs::path & m : wavs) {
            if (!ir_name_matches_rate(m.filename().wstring(), rate)) continue;

            chain_file_info info;
            info.path = m.wstring();

            if (irs.size() >= kMaxChainFiles) {
                info.note = "skipped: chain limit of 16 files reached";
                out.files.push_back(std::move(info));
                continue;
            }

            ir_data ir;
            std::string ferr;
            if (!load_wav_ir(info.path.c_str(), ir, ferr)) {
                info.note = ferr;
                out.files.push_back(std::move(info));
                continue;
            }
            info.sample_rate = ir.sample_rate; // header rate, pre-resampling
            info.channels    = ir.channels;
            info.frames      = ir.frames();

            if (ir.sample_rate != sample_rate) {
                if (!allow_resample) {
                    info.note = "file is " + std::to_string(ir.sample_rate) + " Hz, stream is "
                              + std::to_string(sample_rate) + " Hz (resampling disabled)";
                    out.files.push_back(std::move(info));
                    continue;
                }
                const unsigned from = ir.sample_rate;
                std::string rerr;
                if (!resample_ir(ir, sample_rate, kMaxCombinedFrames, rerr)) {
                    info.note = "resampling failed: " + rerr;
                    out.files.push_back(std::move(info));
                    continue;
                }
                info.note = "resampled " + std::to_string(from) + " -> "
                          + std::to_string(sample_rate) + " Hz (r8brain)";
                info.frames = ir.frames();
            }

            const size_t new_len = (run_len == 0) ? ir.frames() : run_len + ir.frames() - 1;
            if (new_len > kMaxCombinedFrames) {
                info.note = "skipped: combined impulse response would exceed 4194304 frames";
                out.files.push_back(std::move(info));
                continue;
            }
            if (ir.channels != 1 && ir.channels != stream_channels) {
                if (!info.note.empty()) info.note += "; ";
                info.note += "channel count differs from stream - using channel 1";
            }

            info.loaded = true;
            out.files.push_back(std::move(info));
            irs.push_back(std::move(ir));
            run_len = new_len;
        }

        if (!irs.empty()) { out.matched_rate = rate; break; }
    }

    out.used_count = irs.size();
    if (irs.empty()) return true; // scan succeeded; nothing usable

    // --- combine in series --------------------------------------------------
    // Effective per-file layout: mono (or channel-mismatched) files contribute
    // channel 0 to every stream channel; files matching the stream layout
    // contribute per-channel. The chain becomes multichannel as soon as one
    // file is.
    unsigned target = 1;
    for (const ir_data & ir : irs)
        if (stream_channels > 1 && ir.channels == stream_channels) target = stream_channels;

    auto chan_of = [&](const ir_data & ir, unsigned c) -> const std::vector<double> & {
        return (ir.channels == stream_channels && c < ir.channels) ? ir.ch[c] : ir.ch[0];
    };

    ir_data comb;
    comb.sample_rate = sample_rate;
    comb.channels    = target;
    comb.ch.resize(target);
    for (unsigned c = 0; c < target; ++c) {
        std::vector<double> acc = chan_of(irs[0], c);
        for (size_t i = 1; i < irs.size(); ++i)
            acc = fft_convolve(acc, chan_of(irs[i], c));
        comb.ch[c] = std::move(acc);
    }
    out.combined = std::move(comb);
    return true;
}
