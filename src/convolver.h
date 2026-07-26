#pragma once
#include "fb2k_sdk.h"
#include "fft.h"
#include <vector>
#include <complex>
#include <string>

// Uniform partitioned overlap-save convolver.
//   block size  B = kBlock
//   FFT size    N = 2*B
//   partitions  P = ceil(irLength / B)
//
// Streaming behaviour: input is accumulated into B-sample blocks; each full
// block produces exactly B output frames, so output lags input by the current
// FIFO fill (< B frames). Report buffered() through dsp::get_latency() so the
// host can compensate. flush_tail() drains the remaining input plus the full
// convolution tail (irLength-1 frames) so no audio is ever lost at stream end.
class PartitionedConvolver {
public:
    static const size_t kBlock = 4096;

    // ir: per-channel impulse response, already gain-scaled.
    // nch: stream channel count. Channel mapping:
    //   1 IR channel        -> applied to every stream channel
    //   IR channels == nch  -> one IR channel per stream channel
    //   anything else       -> IR channel 0 everywhere (caller should warn)
    bool setup(const std::vector<std::vector<double>> & ir, unsigned nch, std::string & err);

    void reset();                                    // drop history/FIFO, keep IR
    void clear() { *this = PartitionedConvolver(); } // full teardown

    bool     ready()     const { return m_ready; }
    unsigned channels()  const { return m_nch; }
    size_t   ir_length() const { return m_irlen; }
    size_t   buffered()  const { return m_fill; }    // frames held, not yet output

    // Feed interleaved input; append processed interleaved frames to out.
    void process(const audio_sample * in, size_t frames, std::vector<audio_sample> & out);

    // Emit remaining buffered input + convolution tail, then reset().
    // No-op if nothing was processed since the last reset (avoids emitting
    // a silent tail for empty streams).
    void flush_tail(std::vector<audio_sample> & out);

private:
    void run_block(std::vector<audio_sample> & out, size_t emit_frames);

    SimpleFFT m_fft;
    size_t   m_B = 0, m_N = 0, m_P = 0;
    size_t   m_fill = 0, m_irlen = 0, m_xpos = 0;
    unsigned m_nch = 0;
    bool     m_ready = false;
    bool     m_dirty = false; // input seen since last reset

    std::vector<size_t> m_map;   // stream channel -> H bank index
    // m_H[bank][partition][bin]
    std::vector<std::vector<std::vector<std::complex<double>>>> m_H;

    struct ChState {
        std::vector<double> cur, prev;                    // time blocks, size B
        std::vector<std::vector<std::complex<double>>> X; // freq delay line [P][N]
        std::vector<double> outTmp;                       // size B
    };
    std::vector<ChState> m_chs;
    std::vector<std::complex<double>> m_work, m_acc;      // size N scratch
};
