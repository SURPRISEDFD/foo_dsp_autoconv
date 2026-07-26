#include "ir_resampler.h"
#include "CDSPResampler.h" // r8brain-free-src (third_party/r8brain, see tools/get_r8brain.ps1)
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// Mirrors the official r8brain-free-src example.cpp:
//   CDSPResampler24 rs(srcRate, dstRate, bufCapacity);
//   loop: rd = read up to bufCapacity input samples (zeros once input ends);
//         wr = rs.process(buf, rd, outPtr);  // wr may be 0 at the start
//         append outPtr[0..wr) until inLen * dst / src samples are collected.
// No other r8brain API is relied upon.
bool resample_ir(ir_data & ir, unsigned target_rate, size_t max_frames,
                 std::string & err) {
    if (ir.sample_rate == target_rate) return true;
    if (ir.sample_rate == 0 || target_rate == 0) { err = "bad sample rate"; return false; }

    const size_t in_len = ir.frames();
    if (in_len == 0 || ir.ch.empty()) { err = "empty impulse response"; return false; }

    const double ratio   = (double)target_rate / (double)ir.sample_rate;
    const size_t out_len = (size_t)std::llround((double)in_len * ratio);
    if (out_len == 0) { err = "resampled impulse response would be empty"; return false; }
    if (out_len > max_frames) {
        err = "resampled impulse response would exceed the frame limit";
        return false;
    }

    try {
        const int kBuf = 1024; // per-call block size, as in the official example
        std::vector<double> inbuf((size_t)kBuf, 0.0);

        for (auto & ch : ir.ch) {
            r8b::CDSPResampler24 rs((double)ir.sample_rate, (double)target_rate, kBuf);

            std::vector<double> out;
            out.reserve(out_len);

            size_t pos         = 0;
            size_t remaining   = out_len;
            size_t zero_blocks = 0;
            const size_t max_zero_blocks = out_len / (size_t)kBuf + 64; // hang guard

            while (remaining > 0) {
                int rd;
                if (pos < in_len) {
                    const size_t left = in_len - pos;
                    rd = (int)(left < (size_t)kBuf ? left : (size_t)kBuf);
                    memcpy(inbuf.data(), ch.data() + pos, (size_t)rd * sizeof(double));
                    pos += (size_t)rd;
                } else {
                    if (++zero_blocks > max_zero_blocks) {
                        err = "resampler produced less output than expected";
                        return false;
                    }
                    rd = kBuf;
                    std::fill(inbuf.begin(), inbuf.end(), 0.0);
                }

                double * op = nullptr;
                const int wr = rs.process(inbuf.data(), rd, op);
                if (wr > 0) {
                    const size_t take = (size_t)wr < remaining ? (size_t)wr : remaining;
                    out.insert(out.end(), op, op + take);
                    remaining -= take;
                }
            }
            ch.swap(out);
        }
    } catch (...) {
        err = "r8brain resampler failure";
        return false;
    }

    ir.sample_rate = target_rate;
    return true;
}
