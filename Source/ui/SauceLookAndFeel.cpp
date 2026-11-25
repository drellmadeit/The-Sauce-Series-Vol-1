#include "SauceLookAndFeel.h"
#include "SauceAssets.h"

using namespace juce;

namespace SauceFonts
{
    static Typeface::Ptr load (const char* data, int size) { return Typeface::createSystemTypefaceFor (data, (size_t) size); }
    static Typeface::Ptr tfRegular() { static auto t = load (SauceAssets::PoppinsRegular_ttf, SauceAssets::PoppinsRegular_ttfSize); return t; }
    static Typeface::Ptr tfMedium()  { static auto t = load (SauceAssets::PoppinsMedium_ttf, SauceAssets::PoppinsMedium_ttfSize); return t; }
    static Typeface::Ptr tfBold()    { static auto t = load (SauceAssets::PoppinsBold_ttf, SauceAssets::PoppinsBold_ttfSize); return t; }
    static Typeface::Ptr tfBrand()   { static auto t = load (SauceAssets::PoppinsBoldItalic_ttf, SauceAssets::PoppinsBoldItalic_ttfSize); return t; }
    Font regular (float h) { return Font (FontOptions (tfRegular()).withHeight (h)); }
    Font medium (float h)  { return Font (FontOptions (tfMedium()).withHeight (h)); }
    Font bold (float h)    { return Font (FontOptions (tfBold()).withHeight (h)); }
    Font brand (float h)   { return Font (FontOptions (tfBrand()).withHeight (h)); }
}

//==============================================================================
namespace SauceDraw
{
void panel (Graphics& g, Rectangle<float> r, float radius, bool glowWhite)
{
    if (glowWhite)
    {
        Path p; p.addRoundedRectangle (r, radius);
        DropShadow (Colours::white.withAlpha (0.35f), 14, {}).drawForPath (g, p);
    }
    g.setGradientFill (ColourGradient (SauceColours::panelTop, r.getX(), r.getY(), SauceColours::panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (glowWhite ? Colours::white.withAlpha (0.85f) : SauceColours::border);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, glowWhite ? 1.6f : 1.0f);
    // faint top sheen for the matte look
    g.setGradientFill (ColourGradient (Colours::white.withAlpha (0.035f), r.getX(), r.getY(), Colours::transparentWhite, r.getX(), r.getY() + 40, false));
    g.fillRoundedRectangle (r.reduced (1.0f), radius);
    // sticker edge: a sprayed blue line along the top-left corner, fading out
    if (r.getWidth() > 120 && r.getHeight() > 60)
    {
        Path edge;
        edge.startNewSubPath (r.getX() + 0.5f, r.getY() + jmin (90.0f, r.getHeight() * 0.5f));
        edge.lineTo (r.getX() + 0.5f, r.getY() + radius);
        edge.quadraticTo (r.getX() + 0.5f, r.getY() + 0.5f, r.getX() + radius, r.getY() + 0.5f);
        edge.lineTo (r.getX() + jmin (160.0f, r.getWidth() * 0.45f), r.getY() + 0.5f);
        g.setGradientFill (ColourGradient (SauceColours::neonHi.withAlpha (0.9f), r.getX(), r.getY(), SauceColours::neon.withAlpha (0.0f),
                                           r.getX() + jmin (160.0f, r.getWidth() * 0.45f), r.getY() + 60, false));
        g.strokePath (edge, PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded));
    }
}

static float hashf (int a, int b) { uint32_t h = (uint32_t) (a * 73856093) ^ (uint32_t) (b * 19349663); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (float) (h & 0xffff) / 65535.0f; }

float tagText (Graphics& g, const String& text, float x, float baseline, float height, bool drips, bool centred)
{
    const auto font = SauceFonts::brand (height);
    GlyphArrangement ga; ga.addLineOfText (font, text, 0, 0);
    const float width = ga.getBoundingBox (0, -1, true).getWidth() + height * 0.1f;
    if (centred) x -= width * 0.5f;
    Path letters; std::vector<Rectangle<float>> boxes;
    int seed = 0; for (auto c : text) seed = seed * 31 + (int) c;
    for (int i = 0; i < ga.getNumGlyphs(); ++i)
    {
        auto& gl = ga.getGlyph (i);
        if (gl.isWhitespace()) continue;
        Path gp; gl.createPath (gp);
        const auto b = gp.getBounds();
        const float ang = (hashf (seed, i) - 0.5f) * 0.16f;                // each letter leans a little differently
        const float dy = (hashf (seed + 7, i) - 0.5f) * height * 0.08f;
        const float sc = 1.0f + (hashf (seed + 3, i) - 0.5f) * 0.08f;
        gp.applyTransform (AffineTransform::rotation (ang, b.getCentreX(), b.getBottom()).scaled (sc, sc, b.getCentreX(), b.getBottom()).translated (x, baseline + dy));
        boxes.push_back (gp.getBounds());
        letters.addPath (gp);
    }
    // 3D drop (extruded down-right in deep blue)
    for (int k = 4; k >= 1; --k)
    {
        g.setColour (SauceColours::neonDeep.darker (0.25f * (float) k).withAlpha (0.9f));
        Path e (letters); e.applyTransform (AffineTransform::translation (height * 0.022f * (float) k, height * 0.028f * (float) k));
        g.fillPath (e);
    }
    // thick dark outline
    g.setColour (Colour (0xff020305));
    g.strokePath (letters, PathStrokeType (height * 0.11f, PathStrokeType::curved, PathStrokeType::rounded));
    // drips from some letters
    if (drips)
        for (size_t i = 0; i < boxes.size(); ++i)
        {
            if (hashf (seed + 11, (int) i) > 0.42f) continue;
            const auto& b = boxes[i];
            const float dx = b.getX() + b.getWidth() * (0.3f + 0.4f * hashf (seed + 13, (int) i));
            const float len = height * (0.18f + 0.45f * hashf (seed + 17, (int) i)), w = height * 0.075f;
            Path d; d.addRoundedRectangle (dx - w * 0.5f, b.getBottom() - height * 0.08f, w, len, w * 0.5f);
            d.addEllipse (dx - w * 0.75f, b.getBottom() - height * 0.08f + len - w * 0.9f, w * 1.5f, w * 1.6f);
            g.setColour (Colour (0xff020305)); g.strokePath (d, PathStrokeType (height * 0.05f));
            g.setGradientFill (ColourGradient (SauceColours::neonHi, dx, b.getBottom(), SauceColours::neon, dx, b.getBottom() + len, false));
            g.fillPath (d);
        }
    // fill: white at the top melting into electric blue
    const auto lb = letters.getBounds();
    g.setGradientFill (ColourGradient (Colours::white, lb.getX(), lb.getY(), SauceColours::spray, lb.getX(), lb.getBottom(), false));
    g.fillPath (letters);
    // inner shine line + a sparkle
    g.setColour (Colours::white.withAlpha (0.55f));
    g.strokePath (letters, PathStrokeType (height * 0.018f));
    if (! boxes.empty())
    {
        const auto& f = boxes.front();
        const float sx = f.getX() + f.getWidth() * 0.25f, sy = f.getY() + height * 0.05f, s = height * 0.14f;
        Path star; star.addStar ({ sx, sy }, 4, s * 0.18f, s, 0.0f);
        g.setColour (Colours::white); g.fillPath (star);
    }
    return width;
}

void splatter (Graphics& g, Point<float> c, float radius, Colour col, int seed)
{
    Random rnd (seed);
    for (int i = 0; i < 40; ++i)
    {
        const float a = rnd.nextFloat() * MathConstants<float>::twoPi, d = radius * std::pow (rnd.nextFloat(), 0.7f);
        const float s = (1.0f - d / radius) * jmin (radius * 0.06f, 5.0f) * (0.4f + rnd.nextFloat()) + 0.6f;
        g.setColour (col.withAlpha (0.25f + 0.55f * rnd.nextFloat()));
        g.fillEllipse (c.x + std::cos (a) * d - s, c.y + std::sin (a) * d - s, s * 2, s * 2);
    }
}

Image sprayWall (int w, int h)
{
    Image img (Image::ARGB, w, h, true);
    Graphics g (img);
    g.fillAll (SauceColours::bg);
    Random rnd (1337);
    // bricks
    const float bh = 46, bw = 118;
    for (int row = 0; row * bh < h; ++row)
        for (int col = -1; col * bw < w; ++col)
        {
            const float x = (float) col * bw + (row % 2 ? bw * 0.5f : 0.0f), y = (float) row * bh;
            const float v = 0.75f + 0.5f * rnd.nextFloat();
            g.setColour (Colour::fromFloatRGBA (0.035f * v, 0.042f * v, 0.055f * v, 1.0f));
            g.fillRoundedRectangle (x + 3, y + 3, bw - 6, bh - 6, 3);
        }
    // spray clouds in electric blue / cyan
    struct Cloud { float x, y, r; Colour c; float a; };
    const Cloud clouds[] = { { 0.10f, 0.08f, 0.34f, SauceColours::neon, 0.30f }, { 0.88f, 0.92f, 0.42f, SauceColours::neonDeep, 0.30f },
                             { 0.62f, 0.05f, 0.24f, SauceColours::spray, 0.16f }, { 0.30f, 0.98f, 0.28f, SauceColours::neon, 0.22f },
                             { 0.97f, 0.30f, 0.20f, SauceColours::spray, 0.14f } };
    for (auto& cl : clouds)
    {
        const float cx = cl.x * (float) w, cy = cl.y * (float) h, r = cl.r * (float) w;
        ColourGradient grad (cl.c.withAlpha (cl.a), cx, cy, cl.c.withAlpha (0.0f), cx + r, cy, true);
        g.setGradientFill (grad); g.fillEllipse (cx - r, cy - r, r * 2, r * 2);
        splatter (g, { cx + r * 0.35f, cy + r * 0.2f }, r * 0.45f, cl.c.brighter (0.3f), (int) (cl.x * 1000));
    }
    // drips running down from the top clouds
    for (int i = 0; i < 9; ++i)
    {
        const float x = (float) w * (0.02f + 0.2f * rnd.nextFloat()) + (i > 4 ? (float) w * 0.5f : 0.0f);
        const float len = 40.0f + 160.0f * rnd.nextFloat(), dw = 2.5f + 3.0f * rnd.nextFloat();
        g.setGradientFill (ColourGradient (SauceColours::neon.withAlpha (0.35f), x, 0, SauceColours::neon.withAlpha (0.0f), x, len, false));
        g.fillRoundedRectangle (x, 0, dw, len, dw * 0.5f);
    }
    // fine spray grain
    for (int i = 0; i < w * h / 90; ++i)
    {
        g.setColour ((rnd.nextBool() ? SauceColours::neonHi : Colours::white).withAlpha (0.02f + 0.05f * rnd.nextFloat()));
        g.fillRect ((float) rnd.nextInt (w), (float) rnd.nextInt (h), 1.4f, 1.4f);
    }
    return img;
}

void glowPath (Graphics& g, const Path& p, Colour c, float w, int glowRadius)
{
    if (glowRadius > 0)
    {
        Path stroked; PathStrokeType (w * 2.2f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (stroked, p);
        DropShadow (c.withAlpha (0.75f), glowRadius, {}).drawForPath (g, stroked);
    }
    g.setColour (c);
    g.strokePath (p, PathStrokeType (w, PathStrokeType::curved, PathStrokeType::rounded));
}

void neonRoundedRect (Graphics& g, Rectangle<float> r, float radius, Colour c, float w, int glowRadius)
{
    Path p; p.addRoundedRectangle (r, radius);
    glowPath (g, p, c, w, glowRadius);
}

void fieldBox (Graphics& g, Rectangle<float> r, float radius)
{
    g.setColour (SauceColours::field);
    g.fillRoundedRectangle (r, radius);
    g.setColour (SauceColours::borderHi);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

Path icon (const String& name, Rectangle<float> a)
{
    Path p;
    const float x = a.getX(), y = a.getY(), w = a.getWidth(), h = a.getHeight(), cx = a.getCentreX(), cy = a.getCentreY();
    const float r = jmin (w, h) * 0.5f;
    if (name == "power")
    {
        p.addCentredArc (cx, cy + r * 0.08f, r * 0.78f, r * 0.78f, 0, 0.6f, MathConstants<float>::twoPi - 0.6f, true);
        p.startNewSubPath (cx, cy - r * 0.9f); p.lineTo (cx, cy - r * 0.05f);
    }
    else if (name == "headphones")
    {
        p.addCentredArc (cx, cy + r * 0.1f, r * 0.8f, r * 0.8f, 0, -MathConstants<float>::halfPi * 1.1f, MathConstants<float>::halfPi * 1.1f, true);
        p.addRoundedRectangle (cx - r * 0.95f, cy + r * 0.05f, r * 0.42f, r * 0.75f, r * 0.12f);
        p.addRoundedRectangle (cx + r * 0.53f, cy + r * 0.05f, r * 0.42f, r * 0.75f, r * 0.12f);
    }
    else if (name == "trash")
    {
        p.startNewSubPath (cx - r * 0.8f, cy - r * 0.55f); p.lineTo (cx + r * 0.8f, cy - r * 0.55f);
        p.startNewSubPath (cx - r * 0.3f, cy - r * 0.55f); p.lineTo (cx - r * 0.3f, cy - r * 0.85f); p.lineTo (cx + r * 0.3f, cy - r * 0.85f); p.lineTo (cx + r * 0.3f, cy - r * 0.55f);
        p.startNewSubPath (cx - r * 0.6f, cy - r * 0.45f); p.lineTo (cx - r * 0.5f, cy + r * 0.9f); p.lineTo (cx + r * 0.5f, cy + r * 0.9f); p.lineTo (cx + r * 0.6f, cy - r * 0.45f);
        p.startNewSubPath (cx - r * 0.18f, cy - r * 0.2f); p.lineTo (cx - r * 0.18f, cy + r * 0.6f);
        p.startNewSubPath (cx + r * 0.18f, cy - r * 0.2f); p.lineTo (cx + r * 0.18f, cy + r * 0.6f);
    }
    else if (name == "undo" || name == "redo")
    {
        const float s = name == "undo" ? 1.0f : -1.0f;
        p.addCentredArc (cx, cy + r * 0.1f, r * 0.7f, r * 0.65f, 0, s * -MathConstants<float>::halfPi * 1.6f, s * MathConstants<float>::halfPi * 0.9f, true);
        const float ax = cx - s * r * 0.68f, ay = cy - r * 0.45f;
        p.startNewSubPath (ax - s * r * 0.05f, ay - r * 0.45f); p.lineTo (ax - s * r * 0.02f, ay + r * 0.1f); p.lineTo (ax + s * r * 0.5f, ay + r * 0.05f);
    }
    else if (name == "gear")
    {
        for (int i = 0; i < 8; ++i)
        {
            const float ang = (float) i * MathConstants<float>::twoPi / 8.0f;
            p.startNewSubPath (cx + std::cos (ang) * r * 0.62f, cy + std::sin (ang) * r * 0.62f);
            p.lineTo (cx + std::cos (ang) * r * 0.95f, cy + std::sin (ang) * r * 0.95f);
        }
        p.addEllipse (cx - r * 0.62f, cy - r * 0.62f, r * 1.24f, r * 1.24f);
        p.addEllipse (cx - r * 0.25f, cy - r * 0.25f, r * 0.5f, r * 0.5f);
    }
    else if (name == "info")
    {
        p.startNewSubPath (cx, cy - r * 0.15f); p.lineTo (cx, cy + r * 0.8f);
        p.addEllipse (cx - r * 0.1f, cy - r * 0.65f, r * 0.2f, r * 0.2f);
    }
    else if (name == "pencil")
    {
        p.startNewSubPath (x + w * 0.15f, y + h * 0.85f); p.lineTo (x + w * 0.2f, y + h * 0.62f); p.lineTo (x + w * 0.7f, y + h * 0.12f);
        p.lineTo (x + w * 0.88f, y + h * 0.3f); p.lineTo (x + w * 0.38f, y + h * 0.8f); p.closeSubPath();
    }
    else if (name == "link")
    {
        p.addRoundedRectangle (cx - r * 0.95f, cy - r * 0.2f, r * 1.0f, r * 0.55f, r * 0.27f);
        p.addRoundedRectangle (cx - r * 0.05f, cy - r * 0.35f, r * 1.0f, r * 0.55f, r * 0.27f);
        p.applyTransform (AffineTransform::rotation (-0.6f, cx, cy));
    }
    else if (name == "expand")
    {
        const float k = r * 0.35f;
        for (int sx : { -1, 1 }) for (int sy : { -1, 1 })
        {
            const float px = cx + sx * r * 0.85f, py = cy + sy * r * 0.85f;
            p.startNewSubPath (px - sx * k, py); p.lineTo (px, py); p.lineTo (px, py - sy * k);
            p.startNewSubPath (px, py); p.lineTo (px - sx * k * 1.2f, py - sy * k * 1.2f);
        }
    }
    else if (name == "curve" || name == "eq")
    {
        p.startNewSubPath (x, cy + h * 0.15f);
        p.cubicTo (x + w * 0.3f, cy + h * 0.15f, x + w * 0.35f, y + h * 0.15f, cx, y + h * 0.15f);
        p.cubicTo (x + w * 0.65f, y + h * 0.15f, x + w * 0.7f, cy + h * 0.15f, x + w, cy + h * 0.15f);
    }
    else if (name == "gate")
    {
        p.addCentredArc (cx - r * 0.25f, cy, r * 0.55f, r * 0.9f, 0, MathConstants<float>::pi * 1.15f, MathConstants<float>::pi * 1.85f, true);
        p.addCentredArc (cx + r * 0.25f, cy, r * 0.55f, r * 0.9f, 0, MathConstants<float>::pi * 0.15f, MathConstants<float>::pi * 0.85f, true);
        p.startNewSubPath (cx, cy - r * 0.6f); p.lineTo (cx, cy + r * 0.6f);
    }
    else if (name == "tune" || name == "wave" || name == "limiter" || name == "brand")
    {
        const float hs[] = { 0.3f, 0.6f, 1.0f, 0.75f, 0.45f, 0.85f, 0.35f };
        const int n = name == "limiter" ? 3 : (name == "tune" ? 5 : 7);
        for (int i = 0; i < n; ++i)
        {
            const float px = x + w * ((float) i + 0.5f) / (float) n;
            const float hh = (name == "limiter" ? (i == 1 ? 1.0f : 0.7f) : hs[i]) * h * 0.5f;
            p.startNewSubPath (px, cy - hh); p.lineTo (px, cy + hh);
        }
    }
    else if (name == "match")
    {
        p.addEllipse (cx - r * 0.95f, cy - r * 0.6f, r * 1.2f, r * 1.2f);
        p.addEllipse (cx - r * 0.25f, cy - r * 0.6f, r * 1.2f, r * 1.2f);
    }
    else if (name == "comp")
    {
        for (int i = 0; i < 4; ++i)
        {
            const float py = y + h * (0.2f + 0.2f * (float) i);
            p.startNewSubPath (x + w * (i == 0 ? 0.35f : 0.1f), py); p.lineTo (x + w * 0.9f, py);
        }
    }
    else if (name == "fx" || name == "bulb")
    {
        if (name == "fx")
        {
            p.startNewSubPath (cx, y); p.quadraticTo (cx, cy, x + w, cy); p.quadraticTo (cx, cy, cx, y + h);
            p.quadraticTo (cx, cy, x, cy); p.quadraticTo (cx, cy, cx, y);
        }
        else
        {
            p.addEllipse (cx - r * 0.45f, cy - r * 0.6f, r * 0.9f, r * 0.9f);
            p.startNewSubPath (cx - r * 0.2f, cy + r * 0.45f); p.lineTo (cx + r * 0.2f, cy + r * 0.45f);
            p.startNewSubPath (cx - r * 0.15f, cy + r * 0.65f); p.lineTo (cx + r * 0.15f, cy + r * 0.65f);
            for (int i = 0; i < 5; ++i)
            {
                const float ang = MathConstants<float>::pi * (1.0f + 0.25f * (float) i);
                p.startNewSubPath (cx + std::cos (ang) * r * 0.65f, cy - r * 0.15f + std::sin (ang) * r * 0.65f);
                p.lineTo (cx + std::cos (ang) * r * 0.9f, cy - r * 0.15f + std::sin (ang) * r * 0.9f);
            }
        }
    }
    else if (name == "delay")
    {
        for (int i = -2; i <= 2; ++i) for (int j = -2; j <= 2; ++j)
            if (std::abs (i) + std::abs (j) <= 2) { const float d = r * 0.12f * (1.0f - 0.12f * (float) (std::abs (i) + std::abs (j))); p.addEllipse (cx + i * r * 0.38f - d, cy + j * r * 0.38f - d, d * 2, d * 2); }
    }
    else if (name == "reverb" || name == "coach")
    {
        for (int i = 0; i < 3; ++i)
        {
            const float oy = cy - r * 0.45f + (float) i * r * 0.38f;
            p.startNewSubPath (cx - r * 0.9f, oy); p.lineTo (cx, oy - r * 0.38f); p.lineTo (cx + r * 0.9f, oy); p.lineTo (cx, oy + r * 0.38f); p.closeSubPath();
            if (name == "coach") break;
        }
        if (name == "coach") { p.startNewSubPath (cx - r * 0.9f, cy - r * 0.45f); p.lineTo (cx - r * 0.9f, cy + r * 0.25f); p.lineTo (cx, cy + r * 0.65f); p.lineTo (cx + r * 0.9f, cy + r * 0.25f); p.lineTo (cx + r * 0.9f, cy - r * 0.45f); }
    }
    else if (name == "pop")
    {
        // mic grille with a burst of air hitting it
        const float gx = x + w * 0.62f, gy = cy, gr = r * 0.55f;
        p.addEllipse (gx - gr, gy - gr, gr * 2, gr * 2);
        p.startNewSubPath (gx - gr * 0.6f, gy); p.lineTo (gx + gr * 0.6f, gy);
        p.startNewSubPath (gx, gy - gr * 0.6f); p.lineTo (gx, gy + gr * 0.6f);
        for (int k = -1; k <= 1; ++k)
        {
            const float yy = cy + (float) k * h * 0.26f;
            p.startNewSubPath (x + w * 0.02f, yy + (float) k * h * 0.04f); p.lineTo (x + w * 0.24f, yy);
        }
    }
    else if (name == "deess")
    {
        // an "S" with a small down arrow beside it
        const float sx = x + w * 0.12f, sw = w * 0.5f;
        p.startNewSubPath (sx + sw, y + h * 0.18f);
        p.cubicTo (sx + sw * 0.6f, y, sx, y + h * 0.08f, sx + sw * 0.1f, y + h * 0.32f);
        p.cubicTo (sx + sw * 0.2f, y + h * 0.52f, sx + sw, y + h * 0.48f, sx + sw * 0.95f, y + h * 0.72f);
        p.cubicTo (sx + sw * 0.9f, y + h * 0.98f, sx + sw * 0.2f, y + h, sx, y + h * 0.82f);
        const float ax = x + w * 0.84f;
        p.startNewSubPath (ax, y + h * 0.2f); p.lineTo (ax, y + h * 0.85f);
        p.startNewSubPath (ax - w * 0.1f, y + h * 0.68f); p.lineTo (ax, y + h * 0.85f); p.lineTo (ax + w * 0.1f, y + h * 0.68f);
    }
    else if (name == "chevron")
    {
        p.startNewSubPath (x, y + h * 0.3f); p.lineTo (cx, y + h * 0.75f); p.lineTo (x + w, y + h * 0.3f);
    }
    return p;
}
} // namespace SauceDraw

//==============================================================================
SauceLookAndFeel::SauceLookAndFeel()
{
    using namespace SauceColours;
    setColour (ResizableWindow::backgroundColourId, bg);
    setColour (Label::textColourId, text);
    setColour (Slider::textBoxTextColourId, text);
    setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
    setColour (Slider::textBoxBackgroundColourId, Colours::transparentBlack);
    setColour (ComboBox::textColourId, text);
    setColour (ComboBox::arrowColourId, text);
    setColour (PopupMenu::backgroundColourId, Colour (0xff0d1116));
    setColour (PopupMenu::textColourId, text);
    setColour (PopupMenu::headerTextColourId, neonHi);
    setColour (PopupMenu::highlightedBackgroundColourId, neonDeep);
    setColour (PopupMenu::highlightedTextColourId, Colours::white);
    setColour (TextButton::textColourOffId, text);
    setColour (TextButton::textColourOnId, Colours::white);
    setColour (TextEditor::backgroundColourId, field);
    setColour (TextEditor::outlineColourId, borderHi);
    setColour (TextEditor::focusedOutlineColourId, neon);
    setColour (TextEditor::textColourId, text);
    setColour (TextEditor::highlightColourId, neonDeep);
    setColour (AlertWindow::backgroundColourId, Colour (0xff0d1116));
    setColour (AlertWindow::textColourId, text);
    setColour (AlertWindow::outlineColourId, neon);
    setColour (TooltipWindow::textColourId, text);
    setColour (CaretComponent::caretColourId, neonHi);
}

void SauceLookAndFeel::drawRotarySlider (Graphics& g, int x, int y, int w, int h, float pos, float start, float end, Slider& s)
{
    const bool white = s.getProperties().getWithDefault ("whiteArc", false);
    const bool mini  = s.getProperties().getWithDefault ("mini", false);
    const auto accent = white ? Colours::white : SauceColours::neon;
    const auto b = Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (mini ? 3.0f : 6.0f);
    const float r = jmin (b.getWidth(), b.getHeight()) * 0.5f, cx = b.getCentreX(), cy = b.getCentreY();
    const float arcW = mini ? 2.5f : jmax (3.0f, r * 0.09f);
    const float arcR = r - arcW;
    const float ang = start + pos * (end - start);
    const float alpha = s.isEnabled() ? 1.0f : 0.35f;

    Path track; track.addCentredArc (cx, cy, arcR, arcR, 0, start, end, true);
    g.setColour (Colour (0xff1b2029).withMultipliedAlpha (alpha));
    g.strokePath (track, PathStrokeType (arcW, PathStrokeType::curved, PathStrokeType::rounded));

    if (pos > 0.001f)
    {
        Path val; val.addCentredArc (cx, cy, arcR, arcR, 0, start, ang, true);
        SauceDraw::glowPath (g, val, accent.withMultipliedAlpha (alpha), arcW, mini ? 4 : 9);
    }

    const float kr = arcR - arcW * 1.6f;
    ColourGradient body (Colour (0xff2c323b), cx - kr * 0.4f, cy - kr * 0.6f, Colour (0xff07090b), cx + kr * 0.5f, cy + kr * 0.8f, true);
    g.setGradientFill (body);
    g.fillEllipse (cx - kr, cy - kr, kr * 2, kr * 2);
    g.setColour (Colour (0xff3b424d));
    g.drawEllipse (cx - kr, cy - kr, kr * 2, kr * 2, 1.0f);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.drawEllipse (cx - kr + 2, cy - kr + 2, kr * 2 - 4, kr * 2 - 4, 1.0f);

    Path ptr;
    ptr.startNewSubPath (cx + std::sin (ang) * kr * 0.25f, cy - std::cos (ang) * kr * 0.25f);
    ptr.lineTo (cx + std::sin (ang) * kr * 0.88f, cy - std::cos (ang) * kr * 0.88f);
    g.setColour (Colours::white.withMultipliedAlpha (alpha));
    g.strokePath (ptr, PathStrokeType (mini ? 1.8f : 2.6f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SauceLookAndFeel::drawLinearSlider (Graphics& g, int x, int y, int w, int h, float pos, float, float, Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearHorizontal) { LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, s); return; }
    const float cy = (float) y + (float) h * 0.5f;
    const auto track = Rectangle<float> ((float) x, cy - 4, (float) w, 8);
    g.setColour (Colour (0xff1b2029)); g.fillRoundedRectangle (track, 4);
    auto filled = track.withRight (pos);
    Path fp; fp.addRoundedRectangle (filled, 4);
    DropShadow (SauceColours::neon.withAlpha (0.7f), 10, {}).drawForPath (g, fp);
    g.setGradientFill (ColourGradient (SauceColours::neonDeep, filled.getX(), cy, SauceColours::neonHi, filled.getRight(), cy, false));
    g.fillPath (fp);
    g.setColour (Colours::white);
    g.fillEllipse (pos - 10, cy - 10, 20, 20);
    g.setColour (SauceColours::neon.withAlpha (0.6f));
    g.drawEllipse (pos - 10, cy - 10, 20, 20, 1.5f);
}

Label* SauceLookAndFeel::createSliderTextBox (Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (SauceFonts::medium (s.getProperties().getWithDefault ("mini", false) ? 11.5f : 17.0f));
    l->setJustificationType (Justification::centred);
    l->setColour (Label::backgroundColourId, SauceColours::field);
    l->setColour (Label::outlineColourId, SauceColours::borderHi);
    l->setColour (Label::textColourId, SauceColours::text);
    return l;
}

Slider::SliderLayout SauceLookAndFeel::getSliderLayout (Slider& s)
{
    auto layout = LookAndFeel_V4::getSliderLayout (s);
    if (s.getSliderStyle() == Slider::RotaryVerticalDrag && s.getTextBoxPosition() == Slider::TextBoxBelow)
    {
        auto b = s.getLocalBounds();
        const int tbh = s.getTextBoxHeight(), tbw = jmin (s.getTextBoxWidth(), b.getWidth());
        layout.textBoxBounds = Rectangle<int> (b.getCentreX() - tbw / 2, b.getBottom() - tbh, tbw, tbh);
        layout.sliderBounds = b.withTrimmedBottom (tbh + 4);
    }
    return layout;
}

void SauceLookAndFeel::drawComboBox (Graphics& g, int w, int h, bool, int, int, int, int, ComboBox& cb)
{
    const auto r = Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (SauceColours::field); g.fillRoundedRectangle (r, 6);
    g.setColour (cb.hasKeyboardFocus (true) ? SauceColours::neon : Colour (0xff5b6472));
    g.drawRoundedRectangle (r, 6, 1.2f);
    if (cb.getProperties().getWithDefault ("noArrow", false)) return;
    const float a = (float) h * 0.28f;
    const auto ar = Rectangle<float> ((float) w - a * 2.1f, (float) h * 0.5f - a * 0.45f, a, a * 0.9f);
    g.setColour (SauceColours::text.withAlpha (cb.isEnabled() ? 1.0f : 0.4f));
    g.strokePath (SauceDraw::icon ("chevron", ar), PathStrokeType (1.8f, PathStrokeType::curved, PathStrokeType::rounded));
}

void SauceLookAndFeel::positionComboBoxText (ComboBox& cb, Label& l)
{
    l.setBounds (10, 1, cb.getWidth() - (int) (cb.getHeight() * 0.8f) - 12, cb.getHeight() - 2);
    l.setFont (getComboBoxFont (cb));
}

Font SauceLookAndFeel::getComboBoxFont (ComboBox& cb) { return SauceFonts::medium (jlimit (12.0f, 19.0f, (float) cb.getHeight() * 0.52f)); }
Font SauceLookAndFeel::getPopupMenuFont() { return SauceFonts::medium (15.0f); }
void SauceLookAndFeel::drawPopupMenuBackground (Graphics& g, int w, int h)
{
    g.fillAll (findColour (PopupMenu::backgroundColourId));
    g.setColour (SauceColours::neon.withAlpha (0.6f)); g.drawRect (0, 0, w, h, 1);
}

void SauceLookAndFeel::drawButtonBackground (Graphics& g, Button& b, const Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (1.5f);
    const float rad = b.getProperties().getWithDefault ("radius", 7.0f);
    const bool primary = b.getProperties().getWithDefault ("primary", false);
    const bool on = b.getToggleState();
    if (primary)
    {
        Path p; p.addRoundedRectangle (r, rad);
        DropShadow (SauceColours::neon.withAlpha (over ? 0.9f : 0.65f), 12, {}).drawForPath (g, p);
        g.setGradientFill (ColourGradient (SauceColours::neonHi, r.getX(), r.getY(), SauceColours::neonDeep, r.getX(), r.getBottom(), false));
        g.fillPath (p);
        return;
    }
    g.setColour (down ? Colour (0xff141a22) : (on ? Colour (0xff0b1a2e) : SauceColours::field));
    g.fillRoundedRectangle (r, rad);
    if (on) SauceDraw::neonRoundedRect (g, r, rad, SauceColours::neon, 1.6f, 7);
    else { g.setColour (over ? Colour (0xff7a8494) : Colour (0xff4a525e)); g.drawRoundedRectangle (r, rad, 1.2f); }
}

void SauceLookAndFeel::drawButtonText (Graphics& g, TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? SauceColours::neonHi : SauceColours::text);
    if (b.getProperties().getWithDefault ("primary", false)) g.setColour (Colours::white);
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), Justification::centred, 1);
}

Font SauceLookAndFeel::getTextButtonFont (TextButton& b, int h)
{
    const float scale = b.getProperties().getWithDefault ("fontScale", 0.45f);
    return SauceFonts::medium (jlimit (11.0f, 20.0f, (float) h * scale));
}
Font SauceLookAndFeel::getLabelFont (Label& l) { return l.getFont(); }

void SauceLookAndFeel::drawTooltip (Graphics& g, const String& text, int w, int h)
{
    g.fillAll (Colour (0xff0d1116));
    g.setColour (SauceColours::neon.withAlpha (0.7f)); g.drawRect (0, 0, w, h, 1);
    g.setColour (SauceColours::text); g.setFont (SauceFonts::regular (13.0f));
    g.drawFittedText (text, Rectangle<int> (0, 0, w, h).reduced (6, 2), Justification::centredLeft, 4);
}
