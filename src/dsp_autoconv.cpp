#include "fb2k_sdk.h"
#include "preset.h"
#include "convolver.h"
#include "ir_chain.h"
#include "config_dialog.h"
#include <cmath>
#include <cstring>
#include <vector>

namespace {

class dsp_autoconv : public dsp_impl_base {
public:
    dsp_autoconv(dsp_preset const & in) {
        m_cfg.from_preset(in);
    }

    // ---- dsp_factory_t requirements -------------------------------------

    static GUID g_get_guid() { return autoconv_preset::guid; }

    static void g_get_name(pfc::string_base & out) {
        out = "Auto Calibration Convolver";
    }

    static bool g_get_default_preset(dsp_preset & out) {
        autoconv_preset def;
        def.to_preset(out);
        return true;
    }

    static bool g_have_config_popup() { return true; }

    static void g_show_config_popup(const dsp_preset & p_data, HWND p_parent,
                                    dsp_preset_edit_callback & p_callback) {
        autoconv_preset cfg;
        cfg.from_preset(p_data);
        if (run_config_dialog(p_parent, cfg)) {
            dsp_preset_impl out;
            cfg.to_preset(out);
            p_callback.on_preset_changed(out);
        }
    }

    // ---- streaming ------------------------------------------------------

    bool on_chunk(audio_chunk * chunk, abort_callback &) override {
        if (!m_cfg.enabled) return true; // master switch: full passthrough

        const unsigned sr   = chunk->get_srate();
        const unsigned nch  = chunk->get_channels();
        const unsigned mask = chunk->get_channel_config();
        if (sr == 0 || nch == 0) return true;

        if (sr != m_srate || nch != m_nch || mask != m_mask) {
            emit_tail();                // finish the previous stream cleanly (old format)
            reconfigure(sr, nch, mask); // load the matching IR chain for the new format
        }

        if (!m_active) return true;     // bypass: no usable calibration files

        m_buf.clear();
        m_conv.process(chunk->get_data(), chunk->get_sample_count(), m_buf);
        emit_buf(sr, nch, mask);
        return false;                   // original chunk replaced by inserted output
    }

    void on_endofplayback(abort_callback &) override {
        // Stream is over: push out the buffered input plus the convolution tail.
        emit_tail();
    }

    void on_endoftrack(abort_callback &) override {
        // Intentionally empty: keep convolver state across track boundaries so
        // gapless playback stays gapless. Format changes are caught in on_chunk.
    }

    void flush() override {
        // Seek / buffer reset: the stream is discontinuous anyway, so drop
        // history without emitting a tail.
        m_conv.reset();
    }

    double get_latency() override {
        return (m_active && m_srate)
            ? double(m_conv.buffered()) / double(m_srate)
            : 0.0;
    }

    bool need_track_change_mark() override { return false; }

private:
    // ---- helpers --------------------------------------------------------

    void emit_buf(unsigned sr, unsigned nch, unsigned mask) {
        if (m_buf.empty()) return;
        const t_size frames = m_buf.size() / nch;
        audio_chunk * out = insert_chunk(m_buf.size());
        out->set_data_size(m_buf.size());
        memcpy(out->get_data(), m_buf.data(), m_buf.size() * sizeof(audio_sample));
        out->set_sample_count(frames);
        out->set_channels(nch, mask);
        out->set_srate(sr);
    }

    void emit_tail() {
        if (!m_active || !m_conv.ready()) return;
        m_buf.clear();
        m_conv.flush_tail(m_buf);
        emit_buf(m_srate, m_nch, m_mask);
    }

    void reconfigure(unsigned sr, unsigned nch, unsigned mask) {
        m_srate = sr; m_nch = nch; m_mask = mask;
        m_active = false;
        m_conv.clear();

        if (m_cfg.folder.is_empty()) {
            if (!m_warned_folder) {
                m_warned_folder = true;
                console::formatter() << "[Auto Calibration Convolver] no calibration folder configured"
                                        " - passing audio through (set it in the DSP configuration)";
            }
            return;
        }

        pfc::stringcvt::string_wide_from_utf8 folderw(m_cfg.folder);

        chain_result chain;
        std::string err;
        if (!build_ir_chain(folderw.get_ptr(), sr, nch, m_cfg.resample_enabled, chain, err)) {
            console::formatter() << "[Auto Calibration Convolver] cannot scan \"" << m_cfg.folder
                                 << "\" (" << err.c_str() << ") - passing through";
            return;
        }

        // One console line per matched file: what was used, what was skipped and why.
        for (const chain_file_info & f : chain.files) {
            const pfc::string8 p8(pfc::stringcvt::string_utf8_from_wide(f.path.c_str()));
            if (f.loaded) {
                console::formatter() << "[Auto Calibration Convolver] + " << p8 << " ("
                                     << (t_uint64)f.frames << " taps, " << f.channels << " ch"
                                     << (f.note.empty() ? "" : "; ") << f.note.c_str() << ")";
            } else {
                console::formatter() << "[Auto Calibration Convolver] skipped " << p8
                                     << " (" << f.note.c_str() << ")";
            }
        }

        if (chain.used_count == 0) {
            console::formatter() << "[Auto Calibration Convolver] no usable calibration WAV whose"
                                    " name contains " << sr
                                 << (m_cfg.resample_enabled ? " (or any nearby rate)" : "")
                                 << " found under \"" << m_cfg.folder << "\" - passing through";
            return;
        }

        if (chain.matched_rate != sr) {
            console::formatter() << "[Auto Calibration Convolver] no file named with " << sr
                                 << " Hz - using nearest available rate " << chain.matched_rate
                                 << " Hz (impulse responses resampled to " << sr << " Hz)";
        }

        // Level matching applies to the whole combined chain.
        const double gain = compute_gain(chain.combined);
        for (auto & c : chain.combined.ch)
            for (auto & v : c) v *= gain;

        const size_t block = pick_block(chain.combined.frames(), m_cfg.fft_adaptive);

        std::string cerr;
        if (!m_conv.setup(chain.combined.ch, nch, block, cerr)) {
            console::formatter() << "[Auto Calibration Convolver] convolver setup failed ("
                                 << cerr.c_str() << ") - passing through";
            return;
        }

        m_active = true;
        console::formatter() << "[Auto Calibration Convolver] chained " << (t_uint64)chain.used_count
                             << " file(s) -> " << (t_uint64)chain.combined.frames() << " taps, "
                             << chain.combined.channels << " ch for " << sr << " Hz, FFT block "
                             << (t_uint64)block << ", gain "
                             << pfc::format_float(20.0 * log10(gain > 0.0 ? gain : 1.0), 0, 2)
                             << " dB";
    }

    // Adaptive FFT block: the smallest power of two >= the combined IR
    // length, clamped to [kMinBlock, kMaxBlock]. By construction this is
    // always a power of two - odd or non-power-of-two sizes such as 16385
    // can never be produced. Larger blocks add latency, which foobar2000
    // compensates for via get_latency().
    static size_t pick_block(size_t irlen, bool adaptive) {
        if (!adaptive) return PartitionedConvolver::kDefaultBlock;
        size_t b = PartitionedConvolver::kMinBlock;
        while (b < irlen && b < PartitionedConvolver::kMaxBlock) b <<= 1;
        return b;
    }

    // Auto level matching: normalize the combined impulse response so its RMS
    // (white-noise) gain is unity - broadband program material keeps its
    // overall loudness while the relative balance between channels is
    // preserved (single global scale). A manual dB offset is applied on top.
    double compute_gain(const ir_data & ir) const {
        double g = 1.0;
        if (m_cfg.auto_gain) {
            double energy = 0.0;
            for (const auto & c : ir.ch)
                for (double v : c) energy += v * v;
            const double mean = ir.ch.empty() ? 0.0 : energy / double(ir.ch.size());
            if (mean > 1e-20) g = 1.0 / sqrt(mean);
        }
        g *= pow(10.0, m_cfg.gain_db / 20.0);
        return g;
    }

    // ---- state ----------------------------------------------------------

    autoconv_preset           m_cfg;
    PartitionedConvolver      m_conv;
    std::vector<audio_sample> m_buf;
    unsigned m_srate = 0, m_nch = 0, m_mask = 0;
    bool m_active = false;
    bool m_warned_folder = false;
};

static dsp_factory_t<dsp_autoconv> g_dsp_autoconv_factory;

} // namespace
