#include "convolver.h"
#include <algorithm>

bool PartitionedConvolver::setup(const std::vector<std::vector<double>> & ir,
                                 unsigned nch, size_t block, std::string & err) {
    clear();
    if (nch == 0 || nch > 32) { err = "bad channel count"; return false; }
    if (ir.empty())           { err = "empty impulse response"; return false; }
    if (block < kMinBlock || block > kMaxBlock || (block & (block - 1)) != 0) {
        err = "invalid FFT block size (must be a power of two in [512, 32768])";
        return false;
    }

    size_t irlen = 0;
    for (const auto & c : ir) irlen = (std::max)(irlen, c.size());
    if (irlen == 0) { err = "empty impulse response"; return false; }

    m_B = block;
    m_N = 2 * m_B;
    m_P = (irlen + m_B - 1) / m_B;
    m_irlen = irlen;
    m_nch = nch;
    m_fft.init(m_N);

    // Channel mapping (see header).
    size_t banks = 1;
    m_map.assign(nch, 0);
    if (ir.size() == nch && nch > 1) {
        banks = nch;
        for (unsigned c = 0; c < nch; ++c) m_map[c] = c;
    }

    // Precompute partitioned IR spectra.
    m_H.assign(banks, std::vector<std::vector<std::complex<double>>>(
                          m_P, std::vector<std::complex<double>>(m_N)));
    std::vector<std::complex<double>> tmp(m_N);
    for (size_t b = 0; b < banks; ++b) {
        const std::vector<double> & h = ir[b < ir.size() ? b : 0];
        for (size_t p = 0; p < m_P; ++p) {
            std::fill(tmp.begin(), tmp.end(), std::complex<double>(0.0, 0.0));
            const size_t off = p * m_B;
            const size_t n = off < h.size() ? (std::min)(m_B, h.size() - off) : 0;
            for (size_t i = 0; i < n; ++i) tmp[i] = std::complex<double>(h[off + i], 0.0);
            m_fft.forward(tmp.data());
            m_H[b][p] = tmp;
        }
    }

    m_chs.assign(nch, ChState());
    for (auto & cs : m_chs) {
        cs.cur.assign(m_B, 0.0);
        cs.prev.assign(m_B, 0.0);
        cs.outTmp.assign(m_B, 0.0);
        cs.X.assign(m_P, std::vector<std::complex<double>>(m_N, std::complex<double>(0.0, 0.0)));
    }
    m_work.resize(m_N);
    m_acc.resize(m_N);
    m_fill = 0;
    m_xpos = 0;
    m_dirty = false;
    m_ready = true;
    return true;
}

void PartitionedConvolver::reset() {
    if (!m_ready) return;
    m_fill = 0;
    m_xpos = 0;
    m_dirty = false;
    for (auto & cs : m_chs) {
        std::fill(cs.cur.begin(),  cs.cur.end(),  0.0);
        std::fill(cs.prev.begin(), cs.prev.end(), 0.0);
        for (auto & x : cs.X)
            std::fill(x.begin(), x.end(), std::complex<double>(0.0, 0.0));
    }
}

void PartitionedConvolver::run_block(std::vector<audio_sample> & out, size_t emit_frames) {
    // Advance the ring so m_xpos addresses the newest block after this call.
    m_xpos = (m_xpos + m_P - 1) % m_P;

    for (unsigned c = 0; c < m_nch; ++c) {
        ChState & cs = m_chs[c];

        // Overlap-save input frame: [prev | cur], length N = 2B.
        for (size_t i = 0; i < m_B; ++i) m_work[i]       = std::complex<double>(cs.prev[i], 0.0);
        for (size_t i = 0; i < m_B; ++i) m_work[m_B + i] = std::complex<double>(cs.cur[i], 0.0);
        m_fft.forward(m_work.data());
        cs.X[m_xpos] = m_work;

        // Y = sum over partitions of X[delay k] * H[k]
        const auto & bank = m_H[m_map[c]];
        std::fill(m_acc.begin(), m_acc.end(), std::complex<double>(0.0, 0.0));
        for (size_t p = 0; p < m_P; ++p) {
            const auto & X = cs.X[(m_xpos + p) % m_P];
            const auto & H = bank[p];
            for (size_t k = 0; k < m_N; ++k) m_acc[k] += X[k] * H[k];
        }
        m_fft.inverse(m_acc.data());

        // Overlap-save: with filter partitions of length B and N = 2B, the
        // last B samples are alias-free linear convolution output.
        for (size_t i = 0; i < m_B; ++i) cs.outTmp[i] = m_acc[m_B + i].real();

        cs.prev.swap(cs.cur); // cur is fully rewritten before the next block
    }

    const size_t emit = (std::min)(emit_frames, m_B);
    const size_t base = out.size();
    out.resize(base + emit * m_nch);
    audio_sample * dst = out.data() + base;
    for (size_t f = 0; f < emit; ++f)
        for (unsigned c = 0; c < m_nch; ++c)
            *dst++ = (audio_sample)m_chs[c].outTmp[f];
}

void PartitionedConvolver::process(const audio_sample * in, size_t frames,
                                   std::vector<audio_sample> & out) {
    if (!m_ready || frames == 0) return;
    m_dirty = true;
    for (size_t f = 0; f < frames; ++f) {
        for (unsigned c = 0; c < m_nch; ++c)
            m_chs[c].cur[m_fill] = (double)in[f * m_nch + c];
        if (++m_fill == m_B) {
            run_block(out, m_B);
            m_fill = 0;
        }
    }
}

void PartitionedConvolver::flush_tail(std::vector<audio_sample> & out) {
    if (!m_ready) return;
    if (!m_dirty) { reset(); return; } // nothing played since reset: no tail

    size_t needed = m_fill + (m_irlen > 0 ? m_irlen - 1 : 0);

    // Pad the current partial block with zeros, then keep pushing zero blocks
    // until the buffered input and the full convolution tail have been emitted.
    for (unsigned c = 0; c < m_nch; ++c)
        std::fill(m_chs[c].cur.begin() + m_fill, m_chs[c].cur.end(), 0.0);
    m_fill = 0;

    while (needed > 0) {
        const size_t emit = (std::min)(needed, m_B);
        run_block(out, emit);
        needed -= emit;
        for (unsigned c = 0; c < m_nch; ++c)
            std::fill(m_chs[c].cur.begin(), m_chs[c].cur.end(), 0.0);
    }
    reset();
}
