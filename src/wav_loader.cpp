#include "wav_loader.h"
#include <cstdio>
#include <cstring>

namespace {

const size_t   kMaxFileBytes = 512u * 1024u * 1024u;   // sanity cap
const uint32_t kMaxFrames    = 4u * 1024u * 1024u;     // ~43 s @ 96 kHz, ample for calibration IRs
const unsigned kMaxChannels  = 32;

uint16_t rd16(const uint8_t * p) { uint16_t v; memcpy(&v, p, 2); return v; }
uint32_t rd32(const uint8_t * p) { uint32_t v; memcpy(&v, p, 4); return v; }

bool read_file(const wchar_t * path, std::vector<uint8_t> & buf, std::string & err) {
    FILE * f = nullptr;
    if (_wfopen_s(&f, path, L"rb") != 0 || !f) { err = "cannot open file"; return false; }
    _fseeki64(f, 0, SEEK_END);
    const long long sz = _ftelli64(f);
    if (sz <= 0 || (unsigned long long)sz > kMaxFileBytes) {
        fclose(f); err = "file empty or too large"; return false;
    }
    _fseeki64(f, 0, SEEK_SET);
    buf.resize((size_t)sz);
    const size_t got = fread(buf.data(), 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { err = "read error"; return false; }
    return true;
}

} // namespace

bool load_wav_ir(const wchar_t * path, ir_data & out, std::string & err) {
    out = ir_data();

    std::vector<uint8_t> buf;
    if (!read_file(path, buf, err)) return false;

    if (buf.size() < 12 ||
        memcmp(buf.data(), "RIFF", 4) != 0 ||
        memcmp(buf.data() + 8, "WAVE", 4) != 0) {
        err = "not a RIFF/WAVE file";
        return false;
    }

    uint16_t tag = 0, nch = 0, bits = 0, block = 0;
    uint32_t rate = 0;
    bool have_fmt = false;
    const uint8_t * data = nullptr;
    size_t data_size = 0;

    // Chunk walk; tolerant of broken chunk sizes near EOF.
    size_t pos = 12;
    while (pos + 8 <= buf.size()) {
        const uint8_t * id = &buf[pos];
        uint32_t sz = rd32(&buf[pos + 4]);
        const size_t body = pos + 8;
        if (sz > buf.size() - body) sz = (uint32_t)(buf.size() - body);

        if (memcmp(id, "fmt ", 4) == 0 && sz >= 16) {
            tag   = rd16(&buf[body + 0]);
            nch   = rd16(&buf[body + 2]);
            rate  = rd32(&buf[body + 4]);
            block = rd16(&buf[body + 12]);
            bits  = rd16(&buf[body + 14]);
            // WAVE_FORMAT_EXTENSIBLE: real format = first DWORD of SubFormat GUID
            if (tag == 0xFFFE && sz >= 40) tag = (uint16_t)rd32(&buf[body + 24]);
            have_fmt = true;
        } else if (memcmp(id, "data", 4) == 0) {
            data = &buf[body];
            data_size = sz;
        }
        pos = body + sz + (sz & 1); // chunks are word-aligned
    }

    if (!have_fmt || !data)            { err = "missing fmt/data chunk"; return false; }
    if (nch < 1 || nch > kMaxChannels) { err = "unsupported channel count"; return false; }
    if (rate < 8000 || rate > 1000000) { err = "implausible sample rate"; return false; }

    const bool is_float = (tag == 3);
    if (tag != 1 && tag != 3) { err = "unsupported WAV format (need PCM or IEEE float)"; return false; }
    if (is_float ? (bits != 32 && bits != 64)
                 : (bits != 16 && bits != 24 && bits != 32)) {
        err = "unsupported bit depth";
        return false;
    }

    const unsigned bytes_per    = bits / 8;
    const unsigned expect_block = bytes_per * nch;
    if (block != expect_block) block = (uint16_t)expect_block; // some writers lie

    const size_t frames = data_size / expect_block;
    if (frames == 0)          { err = "empty data chunk"; return false; }
    if (frames > kMaxFrames)  { err = "impulse response too long (limit 4194304 frames)"; return false; }

    out.sample_rate = rate;
    out.channels    = nch;
    out.ch.assign(nch, std::vector<double>(frames, 0.0));

    for (size_t f = 0; f < frames; ++f) {
        const uint8_t * fr = data + f * expect_block;
        for (unsigned c = 0; c < nch; ++c) {
            const uint8_t * s = fr + c * bytes_per;
            double v = 0.0;
            if (is_float) {
                if (bits == 32) { float  x; memcpy(&x, s, 4); v = x; }
                else            { double x; memcpy(&x, s, 8); v = x; }
            } else if (bits == 16) {
                int16_t x; memcpy(&x, s, 2);
                v = x / 32768.0;
            } else if (bits == 24) {
                int32_t x = (int32_t)s[0] | ((int32_t)s[1] << 8) | ((int32_t)s[2] << 16);
                if (x & 0x800000) x |= ~0xFFFFFF; // sign-extend
                v = x / 8388608.0;
            } else { // 32-bit int PCM
                int32_t x; memcpy(&x, s, 4);
                v = x / 2147483648.0;
            }
            out.ch[c][f] = v;
        }
    }
    return true;
}
