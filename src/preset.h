#pragma once
#include "fb2k_sdk.h"

// All user-facing configuration of the DSP. Serialized into the dsp_preset
// blob managed by foobar2000 (per DSP-chain entry).
struct autoconv_preset {
    bool         enabled   = true;
    bool         auto_gain = true;   // normalize IR to unity RMS (white-noise) gain
    float        gain_db   = 0.0f;   // extra manual gain, dB (clamped to +/-24 in UI)
    pfc::string8 folder;             // calibration folder, UTF-8
    pfc::string8 name_template;      // e.g. "Calibration_{samplerate}.wav"

    autoconv_preset() { name_template = "Calibration_{samplerate}.wav"; }

    static const GUID guid;

    void to_preset(dsp_preset & out) const;
    // Tolerant: falls back to defaults on any parse failure.
    void from_preset(const dsp_preset & in);

    // "{samplerate}" in the template is replaced by the decimal rate.
    pfc::string8 build_filename(unsigned sample_rate) const;
    pfc::string8 build_full_path(unsigned sample_rate) const;
};
