#pragma once
// Shared helpers for The Sauce Suite DSP. No JUCE dependency, no allocation in process paths.
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>

namespace sauce
{
constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float dbToGain (float db)          { return db <= -150.0f ? 0.0f : std::pow (10.0f, db * 0.05f); }
inline float gainToDb (float g, float floorDb = -150.0f) { return g > 0.0f ? std::max (floorDb, 20.0f * std::log10 (g)) : floorDb; }
template <typename T> inline T clampv (T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

/** One-pole coefficient for a time constant in ms (63% settle time). */
inline float tcCoef (float ms, double sr)
{
    if (ms <= 0.0f) return 0.0f;
    return (float) std::exp (-1.0 / (0.001 * ms * sr));
}

/** Linear-ramp smoother used for gains and mix amounts (click-free parameter changes). */
struct Smoothed
{
    void reset (double sr, float rampMs, float value) { steps = std::max (1, (int) (sr * rampMs * 0.001)); cur = tgt = value; left = 0; }
    void setTarget (float v) { if (v != tgt) { tgt = v; left = steps; inc = (tgt - cur) / (float) steps; } }
    void snap (float v) { cur = tgt = v; left = 0; }
    inline float next() { if (left > 0) { cur += inc; if (--left == 0) cur = tgt; } return cur; }
    float current() const { return cur; }
    float target() const  { return tgt; }
    bool isSmoothing() const { return left > 0; }
    float cur = 0, tgt = 0, inc = 0; int steps = 1, left = 0;
};

/** Simple fixed-capacity circular delay line (power-of-two size). */
struct DelayLine
{
    void prepare (int maxSamples)
    {
        int n = 1; while (n < maxSamples + 4) n <<= 1;
        buf.assign ((size_t) n, 0.0f); mask = n - 1; w = 0;
    }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    inline void push (float x) { buf[(size_t) w] = x; w = (w + 1) & mask; }
    /** Read 'delay' samples behind the most recently pushed sample (delay >= 1 means one sample old). */
    inline float readInt (int delay) const { return buf[(size_t) ((w - 1 - delay) & mask)]; }
    /** Cubic (Hermite) fractional read. */
    inline float read (float delay) const
    {
        const float d = std::max (0.0f, delay);
        const int   i = (int) d; const float f = d - (float) i;
        const float y0 = readInt (i - 1), y1 = readInt (i), y2 = readInt (i + 1), y3 = readInt (i + 2);
        const float c0 = y1, c1 = 0.5f * (y2 - y0);
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + c0;
    }
    int size() const { return mask + 1; }
    std::vector<float> buf; int mask = 0, w = 0;
};

/** Envelope follower with separate attack/release (operates on |x| or power). */
struct EnvFollower
{
    void prepare (double sr, float atkMs, float relMs) { a = tcCoef (atkMs, sr); r = tcCoef (relMs, sr); env = 0; }
    inline float process (float x) { const float c = x > env ? a : r; env = x + c * (env - x); return env; }
    float a = 0, r = 0, env = 0;
};

inline void flushDenormal (float& x) { if (std::abs (x) < 1.0e-20f) x = 0.0f; }
} // namespace sauce
