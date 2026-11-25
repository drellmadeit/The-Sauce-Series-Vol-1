// The Sauce Suite - Windows installer. Embeds the VST3 DLL as a resource and copies it to
// C:\Program Files\Common Files\VST3\The Sauce Suite.vst3\Contents\x86_64-win\ (the standard VST3 folder).
#ifndef UNICODE
 #define UNICODE
#endif
#ifndef _UNICODE
 #define _UNICODE
#endif
#include <windows.h>
#include <shlobj.h>
#include <string>

static const wchar_t* kTitle = L"The Sauce Suite 1.3.1 Setup";

static std::wstring vst3Root()
{
    wchar_t* common = nullptr; std::wstring r;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_ProgramFilesCommon, 0, nullptr, &common))) r = common;
    else r = L"C:\\Program Files\\Common Files";
    CoTaskMemFree (common);
    return r + L"\\VST3";
}

static bool makeDirs (const std::wstring& path) { return SHCreateDirectoryExW (nullptr, path.c_str(), nullptr) == ERROR_SUCCESS || GetFileAttributesW (path.c_str()) != INVALID_FILE_ATTRIBUTES; }

static bool removeTree (const std::wstring& dir)
{
    std::wstring from = dir; from.push_back (L'\0');
    SHFILEOPSTRUCTW op {}; op.wFunc = FO_DELETE; op.pFrom = from.c_str(); op.fFlags = FOF_NO_UI;
    return SHFileOperationW (&op) == 0;
}

static bool silent = false;
static int msg (const std::wstring& t, UINT f, int silentAnswer) { return silent ? silentAnswer : MessageBoxW (nullptr, t.c_str(), kTitle, f); }

int WINAPI wWinMain (HINSTANCE, HINSTANCE, PWSTR cmd, int)
{
    silent = cmd && (wcsstr (cmd, L"/S") || wcsstr (cmd, L"/s"));   // silent install: Setup.exe /S   (silent uninstall: /S /U)
    const bool silentUninstall = silent && cmd && (wcsstr (cmd, L"/U") || wcsstr (cmd, L"/u"));
    const std::wstring bundle = vst3Root() + L"\\The Sauce Suite.vst3";
    const std::wstring binDir = bundle + L"\\Contents\\x86_64-win";
    const std::wstring dll    = binDir + L"\\The Sauce Suite.vst3";
    const bool installed = GetFileAttributesW (dll.c_str()) != INVALID_FILE_ATTRIBUTES;

    std::wstring q = L"Install The Sauce Suite (VST3, 64-bit) to:\n\n" + bundle + L"\n\n";
    UINT buttons = MB_OKCANCEL;
    if (installed) { q = L"The Sauce Suite is already installed at:\n\n" + bundle + L"\n\nYes = update / reinstall\nNo = uninstall\nCancel = do nothing"; buttons = MB_YESNOCANCEL; }
    const int choice = msg (q, buttons | MB_ICONQUESTION, silentUninstall ? IDNO : (installed ? IDYES : IDOK));
    if (choice == IDCANCEL) return 0;

    if (installed && choice == IDNO)
    {
        const bool ok = removeTree (bundle);
        msg (ok ? L"The Sauce Suite was removed. Your saved presets were kept." : L"Couldn't remove the plugin. Close your DAW and try again.", ok ? MB_ICONINFORMATION : MB_ICONERROR, IDOK);
        return ok ? 0 : 1;
    }

    HRSRC res = FindResourceW (nullptr, L"SAUCE_VST3", RT_RCDATA);
    HGLOBAL h = res ? LoadResource (nullptr, res) : nullptr;
    const void* data = h ? LockResource (h) : nullptr;
    const DWORD size = res ? SizeofResource (nullptr, res) : 0;
    if (! data || size == 0) { msg (L"Installer is damaged (plugin data missing).", MB_ICONERROR, IDOK); return 1; }

    if (! makeDirs (binDir)) { msg ((L"Couldn't create " + binDir).c_str(), MB_ICONERROR, IDOK); return 1; }
    HANDLE f = CreateFileW (dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
    {
        msg (L"Couldn't write the plugin file.\n\nIf your DAW is open, close it and run Setup again.", MB_ICONERROR, IDOK);
        return 1;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile (f, data, size, &written, nullptr) && written == size;
    CloseHandle (f);
    if (! ok) { msg (L"Writing the plugin failed (disk full?).", MB_ICONERROR, IDOK); return 1; }

    msg ((L"Installed!\n\n" + bundle +
        L"\n\nNext: open your DAW and rescan plugins.\n"
        L"FL Studio: Options > Manage plugins > Find more plugins.\n"
        L"Then add \"The Sauce Suite\" (by Drellmadeit) to your vocal track's effects."), MB_ICONINFORMATION, IDOK);
    return 0;
}
