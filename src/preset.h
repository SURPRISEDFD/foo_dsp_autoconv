#pragma once
#include "fb2k_sdk.h"

// All user-facing configuration of the DSP. Serialized into the dsp_preset
// blob managed by foobar2000 (per DSP-chain entry).
struct autoconv_preset {
    bool         enabled   = true;
    bool         auto_gain = true;   // normalize the combined IR to unity RMS gain
    float        gain_db   = 0.0f;   // extra manual gain, dB (clamped to +/-24 in UI)
    bool         fft_adaptive     = true; // FFT block follows the combined IR length
                                          // (always a power of two, 512..32768)
    pfc::string8 folder;             // calibration folder, UTF-8; scanned recursively

    static const GUID guid;

    void to_preset(dsp_preset & out) const;
    // Tolerant: falls back to defaults on any parse failure. Understands the
    // current 'ACV3' layout plus the legacy 'ACV2' (no FFT flag) and 'ACV1'
    // (carried a filename template) layouts.
    void from_preset(const dsp_preset & in);
};
