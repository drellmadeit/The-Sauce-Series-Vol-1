// The Sauce Suite - branded setup wizard for Windows.
// Native Win32 + GDI+, custom-drawn (matte black / neon blue). Everything it installs is embedded as resources.
//
//   TheSauceSuite-Setup.exe               wizard (install / update)
//   TheSauceSuite-Setup.exe /S            silent install with the default options
//   Uninstall.exe /uninstall [/S]         uninstall (registered in Windows "Apps & features")
//   TheSauceSuite-Setup.exe /snapshot DIR renders every page to PNG (for checking the design)
#ifndef UNICODE
 #define UNICODE
#endif
#ifndef _UNICODE
 #define _UNICODE
#endif
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <objidl.h>
#include <algorithm>
namespace Gdiplus { using std::min; using std::max; }
#include <gdiplus.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include "resource.h"

using namespace Gdiplus;

static const wchar_t* kVersion   = L"1.4.0";
static const wchar_t* kProduct   = L"The Sauce Suite";
static const wchar_t* kPublisher = L"Drellmadeit";
static const wchar_t* kUninstKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TheSauceSuite";

// ---------------------------------------------------------------- colours
static const Color cBg (255, 4, 6, 9), cRail (255, 9, 12, 16), cPanel (255, 14, 18, 24), cBorder (255, 37, 43, 52),
                   cNeon (255, 31, 143, 255), cNeonHi (255, 82, 180, 255), cNeonDeep (255, 11, 79, 216),
                   cText (255, 238, 243, 248), cDim (255, 140, 150, 165), cRed (255, 255, 77, 94);

// ---------------------------------------------------------------- small helpers
struct Blob { const void* data = nullptr; DWORD size = 0; };
static Blob resource (int id)
{
    HRSRC r = FindResourceW (nullptr, MAKEINTRESOURCEW (id), RT_RCDATA);
    if (! r) return {};
    HGLOBAL h = LoadResource (nullptr, r);
    return { h ? LockResource (h) : nullptr, SizeofResource (nullptr, r) };
}
static std::wstring knownFolder (REFKNOWNFOLDERID id)
{
    PWSTR p = nullptr; std::wstring s;
    if (SUCCEEDED (SHGetKnownFolderPath (id, 0, nullptr, &p))) s = p;
    CoTaskMemFree (p);
    return s;
}
static bool exists (const std::wstring& p) { return GetFileAttributesW (p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool makeDirs (const std::wstring& p) { const int r = SHCreateDirectoryExW (nullptr, p.c_str(), nullptr); return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || exists (p); }
static bool removeTree (const std::wstring& dir)
{
    if (! exists (dir)) return true;
    std::wstring from = dir; from.push_back (L'\0');
    SHFILEOPSTRUCTW op {}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_NO_UI;
    return SHFileOperationW (&op) == 0;
}
static bool writeFile (const std::wstring& path, const void* data, DWORD size)
{
    HANDLE f = CreateFileW (path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD w = 0; const BOOL ok = WriteFile (f, data, size, &w, nullptr) && w == size;
    CloseHandle (f); return ok;
}

// ---------------------------------------------------------------- paths
struct Paths
{
    std::wstring vst3Root, bundle, binDir, dll, appDir, uninstaller, docsDir, presetsDir, shortcut;
    Paths()
    {
        std::wstring common = knownFolder (FOLDERID_ProgramFilesCommon); if (common.empty()) common = L"C:\\Program Files\\Common Files";
        std::wstring pf = knownFolder (FOLDERID_ProgramFiles);            if (pf.empty()) pf = L"C:\\Program Files";
        vst3Root = common + L"\\VST3";
        bundle = vst3Root + L"\\The Sauce Suite.vst3";
        binDir = bundle + L"\\Contents\\x86_64-win";
        dll = binDir + L"\\The Sauce Suite.vst3";
        appDir = pf + L"\\Drellmadeit\\The Sauce Suite";
        uninstaller = appDir + L"\\Uninstall.exe";
        docsDir = knownFolder (FOLDERID_Documents) + L"\\The Sauce Suite";
        presetsDir = knownFolder (FOLDERID_RoamingAppData) + L"\\Drellmadeit\\The Sauce Suite\\Presets";   // where the plugin keeps user presets
        shortcut = knownFolder (FOLDERID_Desktop) + L"\\The Sauce Suite Presets.lnk";
    }
};

static std::wstring installedVersion()
{
    HKEY k; std::wstring v;
    if (RegOpenKeyExW (HKEY_LOCAL_MACHINE, kUninstKey, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS)
    {
        wchar_t buf[64]; DWORD sz = sizeof (buf), type = 0;
        if (RegQueryValueExW (k, L"DisplayVersion", nullptr, &type, (LPBYTE) buf, &sz) == ERROR_SUCCESS && type == REG_SZ) v = buf;
        RegCloseKey (k);
    }
    if (v.empty() && exists (Paths().dll)) v = L"an earlier version";
    return v;
}

static std::wstring findFLStudio()
{
    std::wstring best;
    for (auto root : { knownFolder (FOLDERID_ProgramFiles), knownFolder (FOLDERID_ProgramFilesX86) })
    {
        if (root.empty()) continue;
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW ((root + L"\\Image-Line\\FL Studio*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do
        {
            if (! (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            for (auto exe : { L"FL64.exe", L"FL.exe" })
            {
                const std::wstring p = root + L"\\Image-Line\\" + fd.cFileName + L"\\" + exe;
                if (exists (p) && p > best) { best = p; break; }
            }
        } while (FindNextFileW (h, &fd));
        FindClose (h);
    }
    return best;
}

/** Opens a file/program as the signed-in user (not elevated) by handing it to Explorer. */
static void openAsUser (const std::wstring& path)
{
    ShellExecuteW (nullptr, L"open", L"explorer.exe", (L"\"" + path + L"\"").c_str(), nullptr, SW_SHOWNORMAL);
}

// ---------------------------------------------------------------- install / uninstall work
struct Options { bool docs = true, shortcut = true, openDaw = true, showNew = true; };

struct Job
{
    std::atomic<float> progress { 0 };
    std::atomic<bool> done { false }, failed { false };
    std::mutex m; std::wstring status, error; std::vector<std::wstring> log;
    void say (const std::wstring& s) { std::lock_guard<std::mutex> l (m); status = s; }
    void logLine (const std::wstring& s) { std::lock_guard<std::mutex> l (m); log.push_back (s); }
    void fail (const std::wstring& e) { std::lock_guard<std::mutex> l (m); error = e; failed = true; }
};

static void runInstall (Job& job, Options opt, bool animate)
{
    Paths P;
    auto pause = [animate] (int ms) { if (animate) Sleep (ms); };
    job.say (L"Preparing..."); pause (250);
    if (! makeDirs (P.binDir)) { job.fail (L"Couldn't create " + P.binDir); return; }
    job.progress = 0.06f;

    Blob dll = resource (IDR_PLUGIN);
    if (! dll.data) { job.fail (L"The installer is damaged (plugin data missing)."); return; }
    job.say (L"Installing The Sauce Suite.vst3");
    HANDLE f = CreateFileW (P.dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { job.fail (L"Couldn't write the plugin. If your DAW is open, close it and run Setup again."); return; }
    const DWORD chunk = 1 << 16; DWORD off = 0;
    while (off < dll.size)
    {
        const DWORD n = std::min (chunk, dll.size - off); DWORD w = 0;
        if (! WriteFile (f, (const char*) dll.data + off, n, &w, nullptr) || w != n) { CloseHandle (f); job.fail (L"Writing the plugin failed (disk full?)."); return; }
        off += n; job.progress = 0.06f + 0.70f * (float) off / (float) dll.size;
        if (animate) Sleep (7);
    }
    CloseHandle (f);
    job.logLine (L"VST3 plugin installed to " + P.vst3Root);

    if (opt.docs)
    {
        job.say (L"Copying the preset list and guide"); pause (200);
        if (makeDirs (P.docsDir))
        {
            Blob pl = resource (IDR_PRESETS), wn = resource (IDR_WHATSNEW), rm = resource (IDR_README);
            writeFile (P.docsDir + L"\\Preset List.txt", pl.data, pl.size);
            writeFile (P.docsDir + L"\\What's New.txt", wn.data, wn.size);
            writeFile (P.docsDir + L"\\Read Me.txt", rm.data, rm.size);
            job.logLine (L"Preset list and guide copied to Documents\\The Sauce Suite");
        }
    }
    job.progress = 0.84f;

    makeDirs (P.presetsDir);
    if (opt.shortcut)
    {
        job.say (L"Creating the desktop shortcut"); pause (150);
        IShellLinkW* link = nullptr;
        if (SUCCEEDED (CoCreateInstance (CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**) &link)))
        {
            link->SetPath (P.presetsDir.c_str());
            link->SetDescription (L"Your saved The Sauce Suite presets");
            IPersistFile* pf = nullptr;
            if (SUCCEEDED (link->QueryInterface (IID_IPersistFile, (void**) &pf))) { pf->Save (P.shortcut.c_str(), TRUE); pf->Release(); }
            link->Release();
            job.logLine (L"Desktop shortcut \"The Sauce Suite Presets\" created");
        }
    }
    job.progress = 0.9f;

    job.say (L"Registering with Windows"); pause (200);
    wchar_t self[MAX_PATH]; GetModuleFileNameW (nullptr, self, MAX_PATH);
    if (makeDirs (P.appDir) && (lstrcmpiW (self, P.uninstaller.c_str()) == 0 || CopyFileW (self, P.uninstaller.c_str(), FALSE)))
    {
        HKEY k;
        if (RegCreateKeyExW (HKEY_LOCAL_MACHINE, kUninstKey, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, nullptr) == ERROR_SUCCESS)
        {
            auto sz = [&] (const wchar_t* n, const std::wstring& v) { RegSetValueExW (k, n, 0, REG_SZ, (const BYTE*) v.c_str(), (DWORD) ((v.size() + 1) * sizeof (wchar_t))); };
            auto dw = [&] (const wchar_t* n, DWORD v) { RegSetValueExW (k, n, 0, REG_DWORD, (const BYTE*) &v, sizeof v); };
            sz (L"DisplayName", kProduct); sz (L"DisplayVersion", kVersion); sz (L"Publisher", kPublisher);
            sz (L"DisplayIcon", P.uninstaller + L",0");
            sz (L"UninstallString", L"\"" + P.uninstaller + L"\" /uninstall");
            sz (L"QuietUninstallString", L"\"" + P.uninstaller + L"\" /uninstall /S");
            sz (L"InstallLocation", P.bundle);
            dw (L"EstimatedSize", (DWORD) (dll.size / 1024 + 64)); dw (L"NoModify", 1); dw (L"NoRepair", 1);
            RegCloseKey (k);
            job.logLine (L"Added to Windows \"Apps & features\" (with uninstaller)");
        }
    }
    job.progress = 1.0f; pause (250);
    job.say (L"Done");
    job.done = true;
}

static void runUninstall (Job& job, bool animate)
{
    Paths P;
    auto pause = [animate] (int ms) { if (animate) Sleep (ms); };
    job.say (L"Removing the plugin"); pause (300);
    if (! removeTree (P.bundle)) { job.fail (L"Couldn't remove the plugin. Close your DAW and try again."); return; }
    job.progress = 0.45f; job.logLine (L"Removed The Sauce Suite.vst3");
    job.say (L"Removing the guide and shortcut"); pause (250);
    for (auto n : { L"\\Preset List.txt", L"\\What's New.txt", L"\\Read Me.txt" }) DeleteFileW ((P.docsDir + n).c_str());
    RemoveDirectoryW (P.docsDir.c_str());
    DeleteFileW (P.shortcut.c_str());
    job.progress = 0.75f;
    job.say (L"Removing it from Apps & features"); pause (250);
    RegDeleteKeyExW (HKEY_LOCAL_MACHINE, kUninstKey, KEY_WOW64_64KEY, 0);
    // the uninstaller can't delete itself while running: let cmd remove it a moment after we exit
    const std::wstring cmd = L"/c ping 127.0.0.1 -n 3 >nul & del /f /q \"" + P.uninstaller + L"\" & rmdir \"" + P.appDir + L"\" & rmdir \"" + P.appDir.substr (0, P.appDir.rfind (L'\\')) + L"\"";
    ShellExecuteW (nullptr, L"open", L"cmd.exe", cmd.c_str(), nullptr, SW_HIDE);
    job.logLine (L"Your own saved presets were kept");
    job.progress = 1.0f; pause (200);
    job.say (L"Done"); job.done = true;
}

// ---------------------------------------------------------------- UI
enum Page { Welcome, License, Choose, Installing, Finish, UConfirm, URunning, UDone };
enum Hot { HNone, HBack, HNext, HCancel, HClose, HMin, HAccept, HOptDocs, HOptShortcut, HOptDaw, HOptNew };

struct Wizard
{
    HWND hwnd = nullptr;
    Page page = Welcome;
    bool uninstallMode = false, accepted = false;
    Options opt;
    Job job; std::thread worker;
    std::wstring prevVersion, flPath;
    int hover = HNone, pressed = HNone;
    float licenseScroll = 0, licenseHeight = 0, pulse = 0;
    struct Region { RectF r; int id; bool enabled; };
    std::vector<Region> regions;

    PrivateFontCollection fonts; FontFamily* families = nullptr; int nFamilies = 0;
    Bitmap* logo = nullptr;
    std::wstring licenseText;

    static constexpr int W = 840, H = 520, Rail = 236;

    // ---------- resources
    void loadAssets()
    {
        for (int id : { IDR_FONT_REGULAR, IDR_FONT_MEDIUM, IDR_FONT_BOLD })
        {
            Blob b = resource (id);
            if (b.data) fonts.AddMemoryFont (b.data, (INT) b.size);
        }
        nFamilies = fonts.GetFamilyCount();
        families = new FontFamily[(size_t) std::max (1, nFamilies)];
        int found = 0; fonts.GetFamilies (nFamilies, families, &found); nFamilies = found;
        Blob lg = resource (IDR_LOGO);
        if (lg.data)
            if (IStream* s = SHCreateMemStream ((const BYTE*) lg.data, lg.size)) { logo = Bitmap::FromStream (s); s->Release(); }
        Blob lic = resource (IDR_LICENSE);
        if (lic.data)
        {
            const int n = MultiByteToWideChar (CP_UTF8, 0, (const char*) lic.data, (int) lic.size, nullptr, 0);
            licenseText.resize ((size_t) n);
            MultiByteToWideChar (CP_UTF8, 0, (const char*) lic.data, (int) lic.size, licenseText.data(), n);
        }
    }
    Font* font (float px, bool bold = false, bool medium = false)
    {
        // family 0..n: "Poppins" (Regular/Bold) and "Poppins Medium"
        const FontFamily* fam = nullptr;
        for (int i = 0; i < nFamilies; ++i)
        {
            wchar_t name[LF_FACESIZE]; families[i].GetFamilyName (name);
            const bool isMedium = wcsstr (name, L"Medium") != nullptr;
            if (isMedium == (medium && ! bold)) { fam = &families[i]; break; }
        }
        if (fam) return new Font (fam, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
        return new Font (L"Segoe UI", px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    }

    // ---------- drawing primitives
    static void rounded (GraphicsPath& p, RectF r, float rad)
    {
        const float d = rad * 2;
        p.AddArc (r.X, r.Y, d, d, 180, 90); p.AddArc (r.GetRight() - d, r.Y, d, d, 270, 90);
        p.AddArc (r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90); p.AddArc (r.X, r.GetBottom() - d, d, d, 90, 90);
        p.CloseFigure();
    }
    static void glowRect (Graphics& g, RectF r, float rad, Color c, int layers = 6)
    {
        for (int i = layers; i >= 1; --i)
        {
            GraphicsPath p; RectF e = r; e.Inflate ((REAL) i * 1.6f, (REAL) i * 1.6f); rounded (p, e, rad + (REAL) i * 1.6f);
            Pen pen (Color ((BYTE) (60 / i), c.GetR(), c.GetG(), c.GetB()), 2.0f); g.DrawPath (&pen, &p);
        }
        GraphicsPath p; rounded (p, r, rad); Pen pen (c, 1.6f); g.DrawPath (&pen, &p);
    }
    void text (Graphics& g, const std::wstring& s, RectF r, Font* f, Color c, StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentNear)
    {
        StringFormat fmt; fmt.SetAlignment (h); fmt.SetLineAlignment (v);
        SolidBrush b (c); g.DrawString (s.c_str(), (INT) s.size(), f, r, &fmt, &b);
    }
    void addRegion (RectF r, int id, bool enabled = true) { regions.push_back ({ r, id, enabled }); }

    void button (Graphics& g, RectF r, const std::wstring& label, int id, bool primary, bool enabled = true)
    {
        addRegion (r, id, enabled);
        const bool hot = hover == id && enabled, down = pressed == id && enabled;
        GraphicsPath p; rounded (p, r, 9);
        if (primary)
        {
            if (enabled) glowRect (g, r, 9, hot ? cNeonHi : cNeon, hot ? 8 : 5);
            LinearGradientBrush br (PointF (0, r.Y), PointF (0, r.GetBottom()),
                                    enabled ? (down ? cNeonDeep : cNeonHi) : Color (255, 40, 50, 64), enabled ? cNeonDeep : Color (255, 30, 36, 46));
            g.FillPath (&br, &p);
        }
        else
        {
            SolidBrush br (down ? Color (255, 20, 26, 34) : Color (255, 10, 13, 17)); g.FillPath (&br, &p);
            Pen pen (hot ? Color (255, 122, 132, 148) : Color (255, 74, 82, 94), 1.2f); g.DrawPath (&pen, &p);
        }
        Font* f = font (15.5f, false, true);
        text (g, label, r, f, enabled ? (primary ? Color (255, 255, 255, 255) : cText) : cDim, StringAlignmentCenter, StringAlignmentCenter);
        delete f;
    }

    void checkbox (Graphics& g, RectF r, const std::wstring& title, const std::wstring& sub, bool on, int id, bool locked = false)
    {
        addRegion (r, id, ! locked);
        const bool hot = hover == id && ! locked;
        GraphicsPath card; rounded (card, r, 10);
        SolidBrush bg (on ? Color (255, 9, 22, 40) : Color (255, 11, 14, 19)); g.FillPath (&bg, &card);
        if (on) glowRect (g, r, 10, Color (255, 31, 143, 255), hot ? 5 : 3);
        else { Pen pen (hot ? Color (255, 100, 110, 125) : cBorder, 1.2f); g.DrawPath (&pen, &card); }
        // toggle
        RectF t (r.X + 18, r.Y + r.Height / 2 - 11, 40, 22);
        GraphicsPath tp; rounded (tp, t, 11);
        SolidBrush tb (on ? (locked ? Color (255, 31, 110, 200) : cNeon) : Color (255, 40, 46, 56)); g.FillPath (&tb, &tp);
        SolidBrush knob (Color (255, 245, 248, 252));
        g.FillEllipse (&knob, on ? t.GetRight() - 19 : t.X + 3, t.Y + 3, 16.0f, 16.0f);
        Font* f1 = font (16, false, true); Font* f2 = font (13);
        text (g, title + (locked ? L"  (required)" : L""), RectF (r.X + 74, r.Y + 10, r.Width - 90, 24), f1, cText);
        text (g, sub, RectF (r.X + 74, r.Y + 34, r.Width - 90, r.Height - 38), f2, cDim);
        delete f1; delete f2;
    }

    // ---------- page painter
    void paint (Graphics& g)
    {
        regions.clear();
        g.SetSmoothingMode (SmoothingModeAntiAlias);
        g.SetTextRenderingHint (TextRenderingHintAntiAliasGridFit);
        g.SetInterpolationMode (InterpolationModeHighQualityBicubic);
        SolidBrush bg (cBg); g.FillRectangle (&bg, 0, 0, W, H);

        // left rail
        LinearGradientBrush rail (PointF (0, 0), PointF (0, (REAL) H), Color (255, 13, 17, 23), Color (255, 6, 8, 11));
        g.FillRectangle (&rail, 0, 0, Rail, H);
        Pen sep (cBorder, 1.0f); g.DrawLine (&sep, (REAL) Rail, 0.0f, (REAL) Rail, (REAL) H);
        if (logo) { const float lw = Rail - 28.0f, lh = lw * (REAL) logo->GetHeight() / (REAL) logo->GetWidth(); g.DrawImage (logo, 14.0f, 26.0f, lw, lh); }

        const wchar_t* steps[] = { L"Welcome", L"License", L"Options", L"Install", L"Finish" };
        const wchar_t* usteps[] = { L"Confirm", L"Remove", L"Done" };
        const int n = uninstallMode ? 3 : 5;
        const int cur = uninstallMode ? (page == UConfirm ? 0 : page == URunning ? 1 : 2) : (int) page;
        Font* sf = font (15, false, true);
        for (int i = 0; i < n; ++i)
        {
            const float y = 130.0f + (float) i * 46.0f;
            if (i < n - 1) { Pen ln (i < cur ? cNeon : cBorder, 2.0f); g.DrawLine (&ln, 40.0f, y + 20, 40.0f, y + 44); }
            RectF dot (30, y, 20, 20);
            if (i == cur)
            {
                for (int k = 5; k >= 1; --k) { SolidBrush gl (Color ((BYTE) (40 / k), 31, 143, 255)); g.FillEllipse (&gl, dot.X - k * 2.5f, dot.Y - k * 2.5f, dot.Width + k * 5.0f, dot.Height + k * 5.0f); }
                SolidBrush b (cNeonHi); g.FillEllipse (&b, dot);
            }
            else if (i < cur)
            {
                SolidBrush b (cNeonDeep); g.FillEllipse (&b, dot);
                Pen ck (Color (255, 255, 255, 255), 2.2f); ck.SetLineJoin (LineJoinRound);
                PointF pts[] = { { dot.X + 5, dot.Y + 10.5f }, { dot.X + 8.5f, dot.Y + 14 }, { dot.X + 15, dot.Y + 6.5f } };
                g.DrawLines (&ck, pts, 3);
            }
            else { Pen p (Color (255, 70, 78, 90), 2.0f); g.DrawEllipse (&p, dot); }
            text (g, uninstallMode ? usteps[i] : steps[i], RectF (64, y - 2, 160, 24), sf, i == cur ? cNeonHi : (i < cur ? cText : cDim));
        }
        delete sf;
        Font* vf = font (12.5f);
        text (g, std::wstring (L"Version ") + kVersion + L"   \u00B7   " + kPublisher, RectF (24, (REAL) H - 40, Rail - 30.0f, 20), vf, cDim);
        delete vf;

        // window buttons
        const RectF closeR ((REAL) W - 46, 10, 34, 28), minR ((REAL) W - 84, 10, 34, 28);
        addRegion (closeR, HClose, page != Installing && page != URunning); addRegion (minR, HMin);
        if (hover == HClose) { SolidBrush hb (Color (255, 160, 30, 45)); g.FillRectangle (&hb, closeR); }
        if (hover == HMin) { SolidBrush hb (Color (255, 30, 36, 46)); g.FillRectangle (&hb, minR); }
        Pen wp (cText, 1.4f);
        g.DrawLine (&wp, closeR.X + 12, closeR.Y + 9, closeR.X + 22, closeR.Y + 19); g.DrawLine (&wp, closeR.X + 22, closeR.Y + 9, closeR.X + 12, closeR.Y + 19);
        g.DrawLine (&wp, minR.X + 12, minR.Y + 14, minR.X + 22, minR.Y + 14);

        const RectF content ((REAL) Rail + 36, 54, (REAL) (W - Rail - 72), (REAL) H - 140);
        Font* h1 = font (30, true); Font* body = font (15); Font* small = font (13);
        auto heading = [&] (const std::wstring& t, const std::wstring& sub)
        {
            text (g, t, RectF (content.X, content.Y, content.Width, 44), h1, cText);
            text (g, sub, RectF (content.X, content.Y + 46, content.Width, 44), body, cDim);
        };
        const RectF next ((REAL) W - 168, (REAL) H - 66, 136, 44), back ((REAL) W - 314, (REAL) H - 66, 132, 44), cancel ((REAL) Rail + 36, (REAL) H - 66, 120, 44);

        switch (page)
        {
            case Welcome:
            {
                heading (L"Welcome", prevVersion.empty() ? std::wstring (L"This will install ") + kProduct + L" " + kVersion + L" on your computer."
                                                          : std::wstring (L"This will update ") + kProduct + L" from " + prevVersion + L" to " + kVersion + L".");
                const wchar_t* feats[] = { L"240 presets in 19 genres, plus artist-inspired starting points",
                                           L"Auto Tune with Auto Key: it finds your song's key for you",
                                           L"Mix / Tracking latency: down to 0.5 ms for recording",
                                           L"Vocal Coach, Match EQ, ducking delay and reverb, limiter" };
                for (int i = 0; i < 4; ++i)
                {
                    const float y = content.Y + 116 + (float) i * 40;
                    SolidBrush d (cNeon); g.FillEllipse (&d, content.X + 2, y + 7, 9.0f, 9.0f);
                    for (int k = 3; k >= 1; --k) { SolidBrush gl (Color ((BYTE) (35 / k), 31, 143, 255)); g.FillEllipse (&gl, content.X + 2 - k * 2.0f, y + 7 - k * 2.0f, 9.0f + k * 4, 9.0f + k * 4); }
                    text (g, feats[i], RectF (content.X + 26, y, content.Width - 26, 30), body, cText);
                }
                RectF note (content.X, content.Y + 290, content.Width, 52);
                GraphicsPath np; rounded (np, note, 8); SolidBrush nb (cPanel); g.FillPath (&nb, &np); Pen npn (cBorder, 1); g.DrawPath (&npn, &np);
                text (g, L"Close FL Studio and any other DAW before you continue.", RectF (note.X + 16, note.Y, note.Width - 32, note.Height), small, cDim, StringAlignmentNear, StringAlignmentCenter);
                button (g, cancel, L"Cancel", HCancel, false);
                button (g, next, L"Next", HNext, true);
                break;
            }
            case License:
            {
                heading (L"License & terms", L"Please read and accept to continue.");
                RectF box (content.X, content.Y + 96, content.Width, 214);
                GraphicsPath bp; rounded (bp, box, 10); SolidBrush bb (Color (255, 8, 10, 14)); g.FillPath (&bb, &bp); Pen bpn (cBorder, 1); g.DrawPath (&bpn, &bp);
                RectF inner (box.X + 18, box.Y + 14, box.Width - 44, box.Height - 28);
                StringFormat fmt; RectF measured; g.MeasureString (licenseText.c_str(), (INT) licenseText.size(), small, RectF (0, 0, inner.Width, 100000), &fmt, &measured);
                licenseHeight = measured.Height;
                licenseScroll = std::max (0.0f, std::min (licenseScroll, licenseHeight - inner.Height));
                g.SetClip (inner);
                text (g, licenseText, RectF (inner.X, inner.Y - licenseScroll, inner.Width, licenseHeight + 20), small, Color (255, 200, 208, 218));
                g.ResetClip();
                if (licenseHeight > inner.Height)   // scrollbar
                {
                    const float trackH = inner.Height, thumbH = std::max (28.0f, trackH * inner.Height / licenseHeight);
                    const float thumbY = inner.Y + (trackH - thumbH) * licenseScroll / (licenseHeight - inner.Height);
                    SolidBrush tr (Color (255, 24, 29, 36)); g.FillRectangle (&tr, box.GetRight() - 14, inner.Y, 4.0f, trackH);
                    SolidBrush th (cNeon); g.FillRectangle (&th, box.GetRight() - 14, thumbY, 4.0f, thumbH);
                }
                checkbox (g, RectF (content.X, content.Y + 324, content.Width, 58), L"I accept these terms", L"Scroll the box to read everything.", accepted, HAccept);
                button (g, cancel, L"Cancel", HCancel, false);
                button (g, back, L"Back", HBack, false);
                button (g, next, L"Next", HNext, true, accepted);
                break;
            }
            case Choose:
            {
                Paths P;
                heading (L"Choose what to install", L"Installs to the standard VST3 folder that every DAW scans.");
                checkbox (g, RectF (content.X, content.Y + 96, content.Width, 64), L"VST3 plugin", P.vst3Root, true, HNone, true);
                checkbox (g, RectF (content.X, content.Y + 170, content.Width, 64), L"Preset list & guide", L"Documents\\The Sauce Suite: all 240 presets + quick start", opt.docs, HOptDocs);
                checkbox (g, RectF (content.X, content.Y + 244, content.Width, 64), L"Desktop shortcut to your presets", L"Opens the folder where your saved presets live", opt.shortcut, HOptShortcut);
                text (g, L"Space required: about 11 MB", RectF (content.X, content.Y + 322, content.Width, 20), small, cDim);
                button (g, cancel, L"Cancel", HCancel, false);
                button (g, back, L"Back", HBack, false);
                button (g, next, prevVersion.empty() ? L"Install" : L"Update", HNext, true);
                break;
            }
            case Installing: case URunning:
            {
                const bool u = page == URunning;
                heading (u ? L"Removing..." : L"Installing...", u ? L"Please wait while The Sauce Suite is removed." : L"Please wait while The Sauce Suite is installed.");
                std::wstring st; std::vector<std::wstring> lg;
                { std::lock_guard<std::mutex> l (job.m); st = job.status; lg = job.log; }
                const float pr = job.progress.load();
                RectF bar (content.X, content.Y + 130, content.Width, 16);
                GraphicsPath tp; rounded (tp, bar, 8); SolidBrush tb (Color (255, 22, 27, 34)); g.FillPath (&tb, &tp);
                if (pr > 0.005f)
                {
                    RectF fill (bar.X, bar.Y, std::max (16.0f, bar.Width * pr), bar.Height);
                    glowRect (g, fill, 8, Color (255, 31, 143, 255), 4 + (int) (2.5f * (1 + std::sin (pulse))));
                    GraphicsPath fp; rounded (fp, fill, 8);
                    LinearGradientBrush fb (PointF (fill.X, 0), PointF (fill.GetRight() + 1, 0), cNeonDeep, cNeonHi); g.FillPath (&fb, &fp);
                }
                Font* pf = font (22, true);
                text (g, std::to_wstring ((int) (pr * 100 + 0.5f)) + L"%", RectF (content.X, content.Y + 156, content.Width, 32), pf, cNeonHi, StringAlignmentFar);
                delete pf;
                text (g, st, RectF (content.X, content.Y + 160, content.Width - 80, 24), body, cText);
                for (size_t i = 0; i < lg.size(); ++i)
                    text (g, L"\u2713  " + lg[i], RectF (content.X, content.Y + 214 + (float) i * 26, content.Width, 24), small, cDim);
                break;
            }
            case Finish: case UDone:
            {
                const bool u = page == UDone;
                if (job.failed)
                {
                    std::wstring e; { std::lock_guard<std::mutex> l (job.m); e = job.error; }
                    heading (L"Something went wrong", e);
                    button (g, next, L"Close", HNext, true);
                    break;
                }
                heading (u ? L"Removed" : L"You're all set", u ? L"The Sauce Suite was removed. Your own saved presets were kept."
                                                              : std::wstring (kProduct) + L" " + kVersion + L" is installed.");
                if (! u)
                {
                    RectF tip (content.X, content.Y + 100, content.Width, 92);
                    GraphicsPath tp; rounded (tp, tip, 10); SolidBrush tb (cPanel); g.FillPath (&tb, &tp); glowRect (g, tip, 10, Color (255, 31, 143, 255), 2);
                    Font* tf = font (15, false, true);
                    text (g, L"Next: rescan plugins in your DAW", RectF (tip.X + 18, tip.Y + 12, tip.Width - 36, 24), tf, cNeonHi);
                    delete tf;
                    text (g, L"FL Studio: Options  >  Manage plugins  >  Find more plugins.\nThen add \"The Sauce Suite\" (by Drellmadeit) to your vocal track.",
                          RectF (tip.X + 18, tip.Y + 40, tip.Width - 36, 50), small, cText);
                    float y = content.Y + 206;
                    if (! flPath.empty()) { checkbox (g, RectF (content.X, y, content.Width, 56), L"Open FL Studio now", flPath.substr (flPath.rfind (L'\\', flPath.rfind (L'\\') - 1) + 1), opt.openDaw, HOptDaw); y += 66; }
                    if (opt.docs) checkbox (g, RectF (content.X, y, content.Width, 56), L"Show what's new", L"Opens \"What's New.txt\"", opt.showNew, HOptNew);
                }
                button (g, next, L"Finish", HNext, true);
                break;
            }
            case UConfirm:
            {
                heading (L"Remove The Sauce Suite?", L"This removes the plugin, the guide and the desktop shortcut.");
                RectF note (content.X, content.Y + 110, content.Width, 60);
                GraphicsPath np; rounded (np, note, 8); SolidBrush nb (cPanel); g.FillPath (&nb, &np); Pen npn (cBorder, 1); g.DrawPath (&npn, &np);
                text (g, L"Presets you saved yourself are kept. Close your DAW first.", RectF (note.X + 16, note.Y, note.Width - 32, note.Height), body, cText, StringAlignmentNear, StringAlignmentCenter);
                button (g, cancel, L"Cancel", HCancel, false);
                button (g, next, L"Uninstall", HNext, true);
                break;
            }
        }
        delete h1; delete body; delete small;
    }

    // ---------- behaviour
    void startJob (bool uninstall)
    {
        page = uninstall ? URunning : Installing;
        worker = std::thread ([this, uninstall]
        {
            CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
            if (uninstall) runUninstall (job, true); else runInstall (job, opt, true);
            CoUninitialize();
            PostMessageW (hwnd, WM_APP + 1, 0, 0);
        });
    }
    void onClick (int id)
    {
        switch (id)
        {
            case HClose: case HCancel: if (page != Installing && page != URunning) DestroyWindow (hwnd); return;
            case HMin: ShowWindow (hwnd, SW_MINIMIZE); return;
            case HAccept: accepted = ! accepted; break;
            case HOptDocs: opt.docs = ! opt.docs; break;
            case HOptShortcut: opt.shortcut = ! opt.shortcut; break;
            case HOptDaw: opt.openDaw = ! opt.openDaw; break;
            case HOptNew: opt.showNew = ! opt.showNew; break;
            case HBack: if (page == License) page = Welcome; else if (page == Choose) page = License; break;
            case HNext:
                if (page == Welcome) page = License;
                else if (page == License && accepted) page = Choose;
                else if (page == Choose) startJob (false);
                else if (page == UConfirm) startJob (true);
                else if (page == Finish || page == UDone)
                {
                    if (page == Finish && ! job.failed)
                    {
                        if (opt.openDaw && ! flPath.empty()) openAsUser (flPath);
                        if (opt.showNew && opt.docs) openAsUser (Paths().docsDir + L"\\What's New.txt");
                    }
                    DestroyWindow (hwnd); return;
                }
                break;
            default: break;
        }
        InvalidateRect (hwnd, nullptr, FALSE);
    }
    int hitTest (float x, float y) const
    {
        for (auto it = regions.rbegin(); it != regions.rend(); ++it)
            if (it->enabled && it->r.Contains (x, y)) return it->id;
        return HNone;
    }
};

static Wizard* gWiz = nullptr;

static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    Wizard& w = *gWiz;
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps; HDC dc = BeginPaint (h, &ps);
            Bitmap buf (Wizard::W, Wizard::H, PixelFormat32bppPARGB);
            { Graphics g (&buf); w.paint (g); }
            Graphics screen (dc); screen.DrawImage (&buf, 0, 0);
            EndPaint (h, &ps); return 0;
        }
        case WM_ERASEBKGND: return 1;
        case WM_MOUSEMOVE:
        {
            const int id = w.hitTest ((float) GET_X_LPARAM (lp), (float) GET_Y_LPARAM (lp));
            if (id != w.hover) { w.hover = id; InvalidateRect (h, nullptr, FALSE); SetCursor (LoadCursor (nullptr, id ? IDC_HAND : IDC_ARROW)); }
            TRACKMOUSEEVENT t { sizeof t, TME_LEAVE, h, 0 }; TrackMouseEvent (&t);
            return 0;
        }
        case WM_MOUSELEAVE: w.hover = HNone; InvalidateRect (h, nullptr, FALSE); return 0;
        case WM_SETCURSOR: if (LOWORD (lp) == HTCLIENT) { SetCursor (LoadCursor (nullptr, w.hover ? IDC_HAND : IDC_ARROW)); return TRUE; } break;
        case WM_LBUTTONDOWN:
        {
            const int id = w.hitTest ((float) GET_X_LPARAM (lp), (float) GET_Y_LPARAM (lp));
            if (id == HNone) { ReleaseCapture(); SendMessageW (h, WM_NCLBUTTONDOWN, HTCAPTION, 0); return 0; }   // drag the window
            w.pressed = id; SetCapture (h); InvalidateRect (h, nullptr, FALSE); return 0;
        }
        case WM_LBUTTONUP:
        {
            ReleaseCapture();
            const int id = w.hitTest ((float) GET_X_LPARAM (lp), (float) GET_Y_LPARAM (lp));
            const int was = w.pressed; w.pressed = HNone;
            if (id == was && id != HNone) w.onClick (id); else InvalidateRect (h, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: w.licenseScroll -= (float) GET_WHEEL_DELTA_WPARAM (wp) / 2.0f; InvalidateRect (h, nullptr, FALSE); return 0;
        case WM_KEYDOWN:
            if (wp == VK_RETURN) w.onClick (HNext);
            else if (wp == VK_ESCAPE) w.onClick (HCancel);
            return 0;
        case WM_TIMER:
            if (w.page == Installing || w.page == URunning) { w.pulse += 0.25f; InvalidateRect (h, nullptr, FALSE); }
            return 0;
        case WM_APP + 1:
            if (w.worker.joinable()) w.worker.join();
            w.page = w.page == URunning ? UDone : Finish;
            InvalidateRect (h, nullptr, FALSE); return 0;
        case WM_DESTROY: if (w.worker.joinable()) w.worker.join(); PostQuitMessage (0); return 0;
        default: break;
    }
    return DefWindowProcW (h, msg, wp, lp);
}

static bool hasArg (PWSTR cmd, const wchar_t* a) { return cmd && wcsstr (cmd, a) != nullptr; }

static CLSID pngEncoder()
{
    UINT n = 0, size = 0; GetImageEncodersSize (&n, &size);
    std::vector<BYTE> buf (size); auto* info = (ImageCodecInfo*) buf.data(); GetImageEncoders (n, size, info);
    for (UINT i = 0; i < n; ++i) if (wcscmp (info[i].MimeType, L"image/png") == 0) return info[i].Clsid;
    return CLSID {};
}

int WINAPI wWinMain (HINSTANCE inst, HINSTANCE, PWSTR cmd, int show)
{
    CoInitializeEx (nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR gdiToken; GdiplusStartupInput gsi; GdiplusStartup (&gdiToken, &gsi, nullptr);
    SetProcessDPIAware();
    int rc = 0;
    {
        Wizard w; gWiz = &w;
        w.uninstallMode = hasArg (cmd, L"/uninstall") || hasArg (cmd, L"/U") || hasArg (cmd, L"/u");
        w.page = w.uninstallMode ? UConfirm : Welcome;
        w.prevVersion = installedVersion();
        w.flPath = findFLStudio();
        w.loadAssets();

        if (hasArg (cmd, L"/snapshot"))
        {
            std::wstring dir = cmd; dir = dir.substr (dir.find (L"/snapshot") + 9);
            while (! dir.empty() && dir.front() == L' ') dir.erase (0, 1);
            if (! dir.empty() && dir.front() == L'"') dir = dir.substr (1, dir.find (L'"', 1) - 1);
            const CLSID png = pngEncoder();
            auto shot = [&] (Page p, const wchar_t* name, std::function<void()> setup)
            {
                w.page = p; setup();
                Bitmap b (Wizard::W, Wizard::H, PixelFormat32bppARGB);
                { Graphics g (&b); w.paint (g); }
                b.Save ((dir + L"\\" + name).c_str(), &png, nullptr);
            };
            w.prevVersion = L"1.3.1"; w.flPath = L"C:\\Program Files\\Image-Line\\FL Studio 2025\\FL64.exe";
            shot (Welcome, L"1-welcome.png", [] {});
            shot (License, L"2-license.png", [&] { w.accepted = true; w.licenseScroll = 0; });
            shot (Choose, L"3-options.png", [] {});
            shot (Installing, L"4-installing.png", [&] { w.job.progress = 0.62f; w.job.status = L"Installing The Sauce Suite.vst3"; w.job.log = { L"Preparing" }; w.job.log.clear(); });
            shot (Finish, L"5-finish.png", [&] { w.job.log.clear(); });
            w.uninstallMode = true;
            shot (UConfirm, L"6-uninstall.png", [] {});
            rc = 0;
        }
        else if (hasArg (cmd, L"/S") || hasArg (cmd, L"/s"))
        {
            if (w.uninstallMode) runUninstall (w.job, false); else runInstall (w.job, w.opt, false);
            rc = w.job.failed ? 1 : 0;
        }
        else
        {
            WNDCLASSEXW wc { sizeof wc };
            wc.style = CS_DROPSHADOW; wc.lpfnWndProc = wndProc; wc.hInstance = inst;
            wc.hIcon = LoadIconW (inst, MAKEINTRESOURCEW (IDI_APP)); wc.hIconSm = wc.hIcon;
            wc.hCursor = LoadCursor (nullptr, IDC_ARROW); wc.lpszClassName = L"SauceSuiteSetup";
            RegisterClassExW (&wc);
            RECT wa; SystemParametersInfoW (SPI_GETWORKAREA, 0, &wa, 0);
            const int x = wa.left + (wa.right - wa.left - Wizard::W) / 2, y = wa.top + (wa.bottom - wa.top - Wizard::H) / 2;
            w.hwnd = CreateWindowExW (WS_EX_APPWINDOW, wc.lpszClassName, w.uninstallMode ? L"Uninstall The Sauce Suite" : L"The Sauce Suite Setup",
                                      WS_POPUP | WS_MINIMIZEBOX | WS_SYSMENU, x, y, Wizard::W, Wizard::H, nullptr, nullptr, inst, nullptr);
            ShowWindow (w.hwnd, show); UpdateWindow (w.hwnd);
            SetTimer (w.hwnd, 1, 33, nullptr);
            MSG m; while (GetMessageW (&m, nullptr, 0, 0) > 0) { TranslateMessage (&m); DispatchMessageW (&m); }
            rc = w.job.failed ? 1 : 0;
        }
        delete[] w.families; delete w.logo;
    }
    GdiplusShutdown (gdiToken);
    CoUninitialize();
    return rc;
}
