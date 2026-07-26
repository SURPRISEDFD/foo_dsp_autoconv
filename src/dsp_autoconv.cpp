#include "fb2k_sdk.h"
#include "preset.h"
#include "convolver.h"
#include "wav_loader.h"
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
            reconfigure(sr, nch, mask); // load the matching IR for the new format
        }

        if (!m_active) return true;     // bypass: no usable calibration file

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

        const pfc::string8 path8 = m_cfg.build_full_path(sr);
        pfc::stringcvt::string_wide_from_utf8 pathw(path8);

        ir_data ir;
        std::string err;
        if (!load_wav_ir(pathw.get_ptr(), ir, err)) {
            console::formatter() << "[Auto Calibration Convolver] no calibration for " << sr
                                 << " Hz: " << path8 << " (" << err.c_str()
                                 << ") - passing through";
            return;
        }
        if (ir.sample_rate != sr) {
            console::formatter() << "[Auto Calibration Convolver] " << path8
                                 << " is " << ir.sample_rate << " Hz but the stream is "
                                 << sr << " Hz - passing through";
            return;
        }
        if (ir.channels != 1 && ir.channels != nch) {
            console::formatter() << "[Auto Calibration Convolver] IR has " << ir.channels
                                 << " channels, stream has " << nch
                                 << " - using IR channel 1 for all stream channels";
        }
        if (strstr(m_cfg.name_template.get_ptr(), "{samplerate}") == nullptr && !m_warned_template) {
            m_warned_template = true;
            console::formatter() << "[Auto Calibration Convolver] note: filename template has no"
                                    " {samplerate} placeholder - the same file is used for every rate";
        }

        // Level matching, applied by scaling the IR before building spectra.
        const double gain = compute_gain(ir);
        for (auto & c : ir.ch)
            for (auto & v : c) v *= gain;

        std::string cerr;
        if (!m_conv.setup(ir.ch, nch, cerr)) {
            console::formatter() << "[Auto Calibration Convolver] convolver setup failed ("
                                 << cerr.c_str() << ") - passing through";
            return;
        }

        m_active = true;
        console::formatter() << "[Auto Calibration Convolver] loaded " << path8
                             << " (" << (t_uint64)ir.frames() << " taps, " << ir.channels
                             << " ch) for " << sr << " Hz, gain "
                             << pfc::format_float(20.0 * log10(gain > 0.0 ? gain : 1.0), 0, 2)
                             << " dB";
    }

    // Auto level matching: normalize the IR so its RMS (white-noise) gain is
    // unity - broadband program material keeps its overall loudness while the
    // relative balance between IR channels is preserved (single global scale).
    // A manual dB offset is applied on top.
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
    bool m_warned_template = false;
};

static dsp_factory_t<dsp_autoconv> g_dsp_autoconv_factory;

} // namespace
