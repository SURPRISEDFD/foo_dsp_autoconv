#pragma once
#include "wav_loader.h"
#include <string>

// Offline resampling of a loaded impulse response to `target_rate` using
// r8brain-free-src (MIT, (c) Aleksey Vaneev / Voxengo), which is fetched into
// third_party/r8brain by tools/get_r8brain.ps1.
//
// The implementation follows the official r8brain example verbatim:
// one CDSPResampler24 per channel, streaming process() calls, zeros fed after
// the input ends until inLen * target / src output samples are collected
// (the resampler discards its own latency internally, so output sample 0 is
// aligned with input sample 0).
//
// Load-time only - never called from the audio path. Fails (returns false
// with `err` set) instead of throwing; the caller then skips the file.
bool resample_ir(ir_data & ir, unsigned target_rate, size_t max_frames,
                 std::string & err);
