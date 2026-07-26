#include "preset.h"
#include <cstring>

// {7A52E8B1-4C1F-4D0A-9B2D-6E1AD08F3C55}
const GUID autoconv_preset::guid =
{ 0x7a52e8b1, 0x4c1f, 0x4d0a, { 0x9b, 0x2d, 0x6e, 0x1a, 0xd0, 0x8f, 0x3c, 0x55 } };

namespace {

const t_uint32 kMagicV3      = 0x33564341; // "ACV3"
const t_uint32 kMagicV2      = 0x32564341; // "ACV2" (no resample/FFT flags yet)
const t_uint32 kMagicV1      = 0x31564341; // "ACV1" (legacy: trailed by a filename-template string)
const t_uint32 kFlagEnabled  = 1u << 0;
const t_uint32 kFlagAutoGain = 1u << 1;
const t_uint32 kFlagResample = 1u << 2;    // ACV3+
const t_uint32 kFlagAdaptive = 1u << 3;    // ACV3+
const t_uint32 kMaxStr       = 4096;

void put_u32(pfc::array_t<t_uint8> & a, t_uint32 v) {
    a.append_fromptr(reinterpret_cast<const t_uint8*>(&v), 4);
}
void put_f32(pfc::array_t<t_uint8> & a, float v) {
    a.append_fromptr(reinterpret_cast<const t_uint8*>(&v), 4);
}
void put_str(pfc::array_t<t_uint8> & a, const char * s) {
    t_uint32 len = s ? (t_uint32)strlen(s) : 0;
    if (len > kMaxStr) len = kMaxStr;
    put_u32(a, len);
    if (len) a.append_fromptr(reinterpret_cast<const t_uint8*>(s), len);
}

struct reader {
    const t_uint8 * p;
    t_size n;
    t_size pos = 0;
    bool u32(t_uint32 & v) {
        if (pos + 4 > n) return false;
        memcpy(&v, p + pos, 4); pos += 4; return true;
    }
    bool f32(float & v) {
        if (pos + 4 > n) return false;
        memcpy(&v, p + pos, 4); pos += 4; return true;
    }
    bool str(pfc::string8 & out) {
        t_uint32 len;
        if (!u32(len) || len > kMaxStr || pos + len > n) return false;
        out.set_string(reinterpret_cast<const char*>(p + pos), len);
        pos += len; return true;
    }
};

} // namespace

void autoconv_preset::to_preset(dsp_preset & out) const {
    pfc::array_t<t_uint8> a;
    put_u32(a, kMagicV3);
    t_uint32 flags = 0;
    if (enabled)          flags |= kFlagEnabled;
    if (auto_gain)        flags |= kFlagAutoGain;
    if (resample_enabled) flags |= kFlagResample;
    if (fft_adaptive)     flags |= kFlagAdaptive;
    put_u32(a, flags);
    put_f32(a, gain_db);
    put_str(a, folder);
    out.set_owner(guid);
    out.set_data(a.get_ptr(), a.get_size());
}

void autoconv_preset::from_preset(const dsp_preset & in) {
    *this = autoconv_preset(); // defaults first; keep them on any failure below

    reader r{ static_cast<const t_uint8*>(in.get_data()), in.get_data_size() };
    t_uint32 magic = 0, flags = 0;
    float g = 0.f;
    pfc::string8 f;
    if (!r.u32(magic)) return;
    if (magic != kMagicV1 && magic != kMagicV2 && magic != kMagicV3) return;
    if (!r.u32(flags) || !r.f32(g) || !r.str(f)) return;
    if (magic == kMagicV1) {
        pfc::string8 legacy_template;
        r.str(legacy_template); // read and discard; matching is rate-in-filename now
    }

    enabled   = (flags & kFlagEnabled)  != 0;
    auto_gain = (flags & kFlagAutoGain) != 0;
    if (magic == kMagicV3) {
        resample_enabled = (flags & kFlagResample) != 0;
        fft_adaptive     = (flags & kFlagAdaptive) != 0;
    } // V1/V2 presets keep the defaults (both enabled)
    if (g >= -60.f && g <= 60.f) gain_db = g;
    folder = f;
}
