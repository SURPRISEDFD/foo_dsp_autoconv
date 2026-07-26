#pragma once
#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>

// Minimal iterative radix-2 complex FFT (double precision, power-of-two sizes).
// Deliberately dependency-free; fast enough for partitioned convolution at
// audio block sizes (the spectral multiply-accumulate dominates for long IRs).
class SimpleFFT {
public:
    void init(size_t n) {
        m_n = n;
        size_t log2n = 0;
        while ((size_t(1) << log2n) < n) ++log2n;

        m_brev.resize(n);
        for (size_t i = 0; i < n; ++i) {
            size_t r = 0;
            for (size_t b = 0; b < log2n; ++b)
                if (i & (size_t(1) << b)) r |= size_t(1) << (log2n - 1 - b);
            m_brev[i] = r;
        }

        m_tw.resize(n / 2);
        const double kPi = 3.14159265358979323846;
        for (size_t i = 0; i < n / 2; ++i) {
            const double a = -2.0 * kPi * double(i) / double(n);
            m_tw[i] = { std::cos(a), std::sin(a) };
        }
    }

    size_t size() const { return m_n; }

    void forward(std::complex<double> * x) const { transform(x, false); }

    void inverse(std::complex<double> * x) const {
        transform(x, true);
        const double s = 1.0 / double(m_n);
        for (size_t i = 0; i < m_n; ++i) x[i] *= s;
    }

private:
    void transform(std::complex<double> * x, bool inv) const {
        const size_t n = m_n;
        for (size_t i = 0; i < n; ++i) {
            const size_t j = m_brev[i];
            if (j > i) std::swap(x[i], x[j]);
        }
        for (size_t len = 2; len <= n; len <<= 1) {
            const size_t half = len >> 1;
            const size_t step = n / len;
            for (size_t base = 0; base < n; base += len) {
                for (size_t k = 0; k < half; ++k) {
                    std::complex<double> w = m_tw[k * step];
                    if (inv) w = std::conj(w);
                    const std::complex<double> u = x[base + k];
                    const std::complex<double> v = x[base + k + half] * w;
                    x[base + k]        = u + v;
                    x[base + k + half] = u - v;
                }
            }
        }
    }

    size_t m_n = 0;
    std::vector<size_t> m_brev;
    std::vector<std::complex<double>> m_tw;
};
