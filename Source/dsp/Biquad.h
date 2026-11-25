#pragma once
#include "DspCommon.h"
#include <complex>

namespace sauce
{
enum class FilterType : int { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, NumTypes };

struct BiquadCoefs
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    /** RBJ cookbook designs. */
    static BiquadCoefs make (FilterType t, double sr, double freq, double q, double gainDb)
    {
        BiquadCoefs c;
        freq = clampv (freq, 10.0, sr * 0.49);
        q    = std::max (0.05, q);
        const double A  = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * 3.14159265358979323846 * freq / sr;
        const double cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / (2.0 * q);
        double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
        switch (t)
        {
            case FilterType::Bell:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case FilterType::LowShelf:
            {
                const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sq); b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sq);  a0 = (A + 1) + (A - 1) * cw + sq;
                a1 = -2 * ((A - 1) + (A + 1) * cw);      a2 = (A + 1) + (A - 1) * cw - sq; break;
            }
            case FilterType::HighShelf:
            {
                const double sq = 2 * std::sqrt (A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sq); b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sq);  a0 = (A + 1) - (A - 1) * cw + sq;
                a1 = 2 * ((A - 1) - (A + 1) * cw);       a2 = (A + 1) - (A - 1) * cw - sq; break;
            }
            case FilterType::LowCut:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case FilterType::HighCut:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case FilterType::Notch:
                b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case FilterType::BandPass:
                b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            default: break;
        }
        c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
        return c;
    }

    /** Magnitude in dB at a frequency (used by the EQ graph and Match EQ fitting). */
    double magnitudeDb (double freq, double sr) const
    {
        const double w = 2.0 * 3.14159265358979323846 * freq / sr;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        const auto num = b0 + b1 * z1 + b2 * z2;
        const auto den = 1.0 + a1 * z1 + a2 * z2;
        return 20.0 * std::log10 (std::max (1e-12, std::abs (num / den)));
    }
};

/** Transposed direct form II biquad, double state for low-frequency precision. */
struct Biquad
{
    BiquadCoefs c;
    double z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float xf)
    {
        const double x = xf;
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};
} // namespace sauce
