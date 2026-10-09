#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <commctrl.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>
#include <cwctype>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>

namespace fs = std::filesystem;

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

// ─── Palette (industrial / cyberpunk) ───
constexpr COLORREF kBgDark       = 0x000D0D0D;  // #0d0d0d
constexpr COLORREF kBgEditDark   = 0x001A1A1A;  // #1a1a1a
constexpr COLORREF kBorderDark   = 0x002D2D2D;  // #2d2d2d
constexpr COLORREF kTextDark     = 0x00E0E0E0;  // #e0e0e0
constexpr COLORREF kAccentDark   = 0x008A6A4A;  // #4a6a8a
constexpr COLORREF kBgLight      = 0x00F5F5F5;
constexpr COLORREF kBgEditLight  = 0x00FFFFFF;
constexpr COLORREF kBorderLight  = 0x00D0D0D0;
constexpr COLORREF kTextLight    = 0x001A1A1A;
constexpr COLORREF kAccentLight  = 0x008A6A4A;

constexpr wchar_t kClassName[]   = L"OmniboxLaunchpadWindow";
constexpr int kEditId            = 1001;
constexpr int kListId            = 1002;
constexpr int kAnimTimerId       = 2001;
constexpr int kAnimSteps         = 12;
constexpr int kAnimIntervalMs    = 12;
constexpr int kMaxResults        = 8;

// ─── Globals ───
HWND g_window       = nullptr;
HWND g_edit         = nullptr;
HWND g_list         = nullptr;
HFONT g_font        = nullptr;
HFONT g_listFont    = nullptr;
HBRUSH g_bg         = nullptr;
HBRUSH g_editBg     = nullptr;
HBRUSH g_border     = nullptr;
HBRUSH g_accent     = nullptr;
bool  g_dark        = true;
int   g_animStep    = kAnimSteps;
bool  g_animIn      = true;
uint8_t g_alpha     = 255;
HANDLE g_hotkeyThread = nullptr;
HHOOK  g_kbHook     = nullptr;
UINT   g_hotkeyMod  = MOD_ALT;
UINT   g_hotkeyVk   = VK_SPACE;

struct AppEntry {
    std::wstring name;
    std::wstring path;
    std::wstring iconPath;
    int score = 0;
};

std::vector<AppEntry> g_apps;
std::unordered_map<std::wstring, std::wstring> g_appIndex;
std::mutex g_appMutex;
bool g_appsReady = false;

// ─── Forward decls ───
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM);
void ScanApps();
void RefreshList(const std::wstring& query);
void Launch(const std::wstring& target);
void ToggleWindow();
void ApplyTheme();
void StartAnimation(bool show);
void SetUpDWM();

// ─── Window size ───
constexpr int kWinW = 620;
constexpr int kWinH = 380;
constexpr int kEditH = 48;
constexpr int kPad   = 12;
constexpr int kListY = kEditH + kPad;
constexpr int kItemH = 36;
constexpr int kListH = kItemH * kMaxResults;

// ═══════════════════════════════════════════
//  DWM BACKDROP (Acrylic / Mica)
// ═══════════════════════════════════════════

bool CheckWin11Build22000() {
    // Simple check: try to use DWMWA_SYSTEMBACKDROP_TYPE
    HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
    if (!dwm) return false;
    return reinterpret_cast<HRESULT(WINAPI*)(HWND,int,PVOID,DWORD)>(
        GetProcAddress(dwm, "DwmSetWindowAttribute")) != nullptr;
}

void SetUpDWM() {
    // Try Win11 Acrylic first (DWMSBT_MAINWINDOW = 2, DWMSBT_ACRYLIC = 3)
    bool win11 = CheckWin11Build22000();
    if (win11) {
        int backdrop = 3; // DWMSBT_ACRYLIC
        DwmSetWindowAttribute(g_window, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */,
                              &backdrop, sizeof(backdrop));
        // Rounded corners
        int corner = 1; // DWMWCP_ROUND
        DwmSetWindowAttribute(g_window, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */,
                              &corner, sizeof(corner));
        // Dark border
        int darkBorder = g_dark ? 1 : 0;
        DwmSetWindowAttribute(g_window, 19 /* DWMWA_USE_IMMERSIVE_DARK_MODE */,
                              &darkBorder, sizeof(darkBorder));
    } else {
        // Win10 fallback: blur-behind
        DWM_BLURBEHIND bb = {};
        bb.dwFlags = DWM_BB_ENABLE;
        bb.fEnable = TRUE;
        bb.hRgnBlur = nullptr;
        DwmEnableBlurBehindWindow(g_window, &bb);
    }

    // Extend frame into client area for borderless look
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_window, &margins);
}

// ═══════════════════════════════════════════
//  ANIMATION
// ═══════════════════════════════════════════

void StartAnimation(bool show) {
    g_animIn = show;
    g_animStep = 0;
    if (show) {
        g_alpha = 0;
        SetWindowPos(g_window, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetForegroundWindow(g_window);
        SetFocus(g_edit);
    }
    SetTimer(g_window, kAnimTimerId, kAnimIntervalMs, nullptr);
}

// ═══════════════════════════════════════════
//  THEME
// ═══════════════════════════════════════════

void ApplyTheme() {
    if (g_bg)       DeleteObject(g_bg);
    if (g_editBg)   DeleteObject(g_editBg);
    if (g_border)   DeleteObject(g_border);
    if (g_accent)   DeleteObject(g_accent);

    g_bg     = CreateSolidBrush(g_dark ? kBgDark     : kBgLight);
    g_editBg = CreateSolidBrush(g_dark ? kBgEditDark : kBgEditLight);
    g_border = CreateSolidBrush(g_dark ? kBorderDark : kBorderLight);
    g_accent = CreateSolidBrush(kAccentDark);

    if (g_font)       DeleteObject(g_font);
    if (g_listFont)   DeleteObject(g_listFont);

    g_font = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Display");

    g_listFont = CreateFontW(
        15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");

    // Apply to controls
    SendMessageW(g_edit, WM_SETFONT, (WPARAM)g_font, TRUE);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)g_listFont, TRUE);

    SetUpDWM();
    InvalidateRect(g_window, nullptr, TRUE);
}

// ═══════════════════════════════════════════
//  APP SCANNER (robust .lnk resolution)
// ═══════════════════════════════════════════

std::wstring ResolveLnk(const fs::path& lnkPath) {
    std::wstring result;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IShellLinkW* psl = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_IShellLinkW, (void**)&psl))) {
        IPersistFile* ppf = nullptr;
        if (SUCCEEDED(psl->QueryInterface(IID_IPersistFile, (void**)&ppf))) {
            if (SUCCEEDED(ppf->Load(lnkPath.c_str(), STGM_READ))) {
                wchar_t target[MAX_PATH] = {};
                if (SUCCEEDED(psl->GetPath(target, MAX_PATH, nullptr, SLGP_RAWPATH))) {
                    result = target;
                }
                ppf->Release();
            }
        }
        psl->Release();
    }
    CoUninitialize();
    return result;
}

void ScanDirectory(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    for (auto& entry : fs::recursive_directory_iterator(dir,
             fs::directory_options::skip_permission_denied, ec)) {
        auto& p = entry.path();
        std::wstring ext = p.extension().wstring();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);

        AppEntry app;
        if (ext == L".lnk") {
            app.name  = p.stem().wstring();
            app.path  = ResolveLnk(p);
            app.iconPath = p.wstring();
        } else if (ext == L".exe") {
            app.name  = p.stem().wstring();
            app.path  = p.wstring();
            app.iconPath = p.wstring();
        } else {
            continue;
        }

        if (app.path.empty() || app.name.empty()) continue;
        // Skip uninstallers and helpers
        std::wstring lowerName = app.name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::towlower);
        if (lowerName.find(L"uninstall") != std::wstring::npos ||
            lowerName.find(L"unins")     != std::wstring::npos ||
            lowerName.find(L"update")    != std::wstring::npos ||
            lowerName.find(L"setup")     != std::wstring::npos) continue;

        std::lock_guard<std::mutex> lock(g_appMutex);
        g_apps.push_back(app);
        g_appIndex[app.name] = app.path;
    }
}

void ScanApps() {
    std::vector<std::thread> workers;

    wchar_t buf[MAX_PATH];
    std::vector<fs::path> dirs;

    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, buf)))
        dirs.push_back(buf);
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_PROGRAMS, nullptr, 0, buf)))
        dirs.push_back(buf);
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_DESKTOPDIRECTORY, nullptr, 0, buf)))
        dirs.push_back(buf);

    // Also scan some common quick-access locations
    wchar_t localAppData[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, localAppData))) {
        fs::path winApps = fs::path(localAppData) / L"Microsoft" / L"WindowsApps";
        if (fs::exists(winApps)) dirs.push_back(winApps);
    }

    for (auto& d : dirs) {
        workers.emplace_back([d]() { ScanDirectory(d); });
    }
    for (auto& t : workers) t.join();

    g_appsReady = true;
}

// ═══════════════════════════════════════════
//  FUZZY MATCHING
// ═══════════════════════════════════════════

int FuzzyScore(const std::wstring& name, const std::wstring& query) {
    int score = 0;
    size_t qi = 0;
    size_t ni = 0;
    int consecutive = 0;

    while (qi < query.size() && ni < name.size()) {
        wchar_t qc = std::towlower(query[qi]);
        wchar_t nc = std::towlower(name[ni]);

        if (qc == nc) {
            score += 10 + consecutive * 5;
            // Big bonus for matching at word boundaries (after space, dash, dot, or start)
            if (ni == 0 || name[ni-1] == L' ' || name[ni-1] == L'-' || name[ni-1] == L'.' ||
                (ni > 0 && std::iswupper(name[ni]) && std::iswlower(name[ni-1]))) {
                score += 15;
            }
            consecutive++;
            qi++;
        } else {
            consecutive = 0;
            score -= 1;
        }
        ni++;
    }
    // Penalty for unmatched remaining chars
    score -= static_cast<int>((query.size() - qi) * 3);
    return score;
}

// ═══════════════════════════════════════════
//  LAUNCH (Zero CMD)
// ═══════════════════════════════════════════

void LaunchProcess(const std::wstring& path) {
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWNORMAL;
    PROCESS_INFORMATION pi = {};

    // Build a mutable command line (CreateProcessW may modify it)
    std::wstring cmdLine = L'"' + path + L'"';
    std::vector<wchar_t> mutableCmd(cmdLine.begin(), cmdLine.end());
    mutableCmd.push_back(L'\0');

    fs::path parent = fs::path(path).parent_path();

    if (CreateProcessW(
            nullptr, mutableCmd.data(),
            nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS,
            nullptr,
            parent.empty() ? nullptr : parent.c_str(),
            &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

void Launch(const std::wstring& raw) {
    std::wstring q = raw;
    // Trim
    q.erase(0, q.find_first_not_of(L" \t"));
    q.erase(q.find_last_not_of(L" \t") + 1);
    if (q.empty()) return;

    // URL detection
    std::wstring lower = q;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    if (lower.rfind(L"http://", 0) == 0 || lower.rfind(L"https://", 0) == 0 ||
        lower.rfind(L"www.", 0) == 0) {
        ShellExecuteW(g_window, L"open", q.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ToggleWindow();
        return;
    }

    // Web search prefix: "g " or "? "
    if (lower.rfind(L"g ", 0) == 0 || lower.rfind(L"? ", 0) == 0) {
        std::wstring search = q.substr(2);
        // URL-encode spaces -> %20
        for (auto& c : search) if (c == L' ') c = L'+';
        std::wstring url = L"https://www.google.com/search?q=" + search;
        ShellExecuteW(g_window, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ToggleWindow();
        return;
    }

    // File path detection (absolute)
    if (q.size() > 2 && q[1] == L':' || q[0] == L'\\') {
        fs::path fp(q);
        if (fs::exists(fp)) {
            LaunchProcess(q);
            ToggleWindow();
            return;
        }
    }

    // Fuzzy app match
    int bestScore = -9999;
    std::wstring bestPath;
    {
        std::lock_guard<std::mutex> lock(g_appMutex);
        for (auto& app : g_apps) {
            int s = FuzzyScore(app.name, q);
            if (s > bestScore) {
                bestScore = s;
                bestPath = app.path;
            }
        }
    }

    if (!bestPath.empty() && bestScore > 0) {
        LaunchProcess(bestPath);
    } else {
        // Fallback: web search
        std::wstring search = q;
        for (auto& c : search) if (c == L' ') c = L'+';
        ShellExecuteW(g_window, L"open",
                      (L"https://www.google.com/search?q=" + search).c_str(),
                      nullptr, nullptr, SW_SHOWNORMAL);
    }

    ToggleWindow();
}

// ═══════════════════════════════════════════
//  RESULT LIST
// ═══════════════════════════════════════════

void RefreshList(const std::wstring& query) {
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
    if (query.empty()) return;

    std::vector<AppEntry> results;
    {
        std::lock_guard<std::mutex> lock(g_appMutex);
        for (auto& app : g_apps) {
            AppEntry e = app;
            e.score = FuzzyScore(app.name, query);
            if (e.score > 0) results.push_back(e);
        }
    }

    std::sort(results.begin(), results.end(),
              [](const AppEntry& a, const AppEntry& b) { return a.score > b.score; });

    int count = 0;
    for (auto& r : results) {
        if (count++ >= kMaxResults) break;
        SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)r.name.c_str());
    }

    if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0)
        SendMessageW(g_list, LB_SETCURSEL, 0, 0);
}

// ═══════════════════════════════════════════
//  WINDOW TOGGLE
// ═══════════════════════════════════════════

void ToggleWindow() {
    if (IsWindowVisible(g_window)) {
        // hide
        KillTimer(g_window, kAnimTimerId);
        ShowWindow(g_window, SW_HIDE);
        g_alpha = 0;
    } else {
        // reposition center-screen
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);
        int x = (sw - kWinW) / 2;
        int y = (sh - kWinH) / 3;
        SetWindowPos(g_window, nullptr, x, y, kWinW, kWinH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        SetWindowTextW(g_edit, L"");
        RefreshList(L"");
        StartAnimation(true);
    }
}

// ═══════════════════════════════════════════
//  LOW-LEVEL KEYBOARD HOOK
// ═══════════════════════════════════════════

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* p = (KBDLLHOOKSTRUCT*)lParam;
        bool ctrl  = GetAsyncKeyState(VK_CONTROL) & 0x8000;
        bool alt   = GetAsyncKeyState(VK_MENU)    & 0x8000;
        bool shift = GetAsyncKeyState(VK_SHIFT)   & 0x8000;
        bool win   = GetAsyncKeyState(VK_LWIN)    & 0x8000 ||
                     GetAsyncKeyState(VK_RWIN)    & 0x8000;

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            if (p->vkCode == VK_SPACE && alt && !ctrl && !shift && !win) {
                ToggleWindow();
                return 1; // swallow the event
            }
        }
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

DWORD WINAPI HotkeyThreadProc(LPVOID) {
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                  GetModuleHandleW(nullptr), 0);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(g_kbHook);
    return 0;
}

// ═══════════════════════════════════════════
//  WINDOW PROCEDURE
// ═══════════════════════════════════════════

LRESULT CALLBACK EditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN) {
        if (wp == VK_RETURN) {
            wchar_t buf[512];
            GetWindowTextW(g_edit, buf, 512);
            Launch(buf);
            return 0;
        }
        if (wp == VK_ESCAPE) {
            ToggleWindow();
            return 0;
        }
        if (wp == VK_DOWN) {
            SetFocus(g_list);
            return 0;
        }
    }
    if (msg == WM_CHAR) {
        // After the edit processes the char, refresh the list
        LRESULT res = CallWindowProcW((WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA),
                                       hwnd, msg, wp, lp);
        wchar_t buf[512];
        GetWindowTextW(g_edit, buf, 512);
        RefreshList(buf);
        return res;
    }
    WNDPROC orig = (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        RECT rc;
        GetClientRect(hwnd, &rc);

        // Edit control
        g_edit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_MULTILINE,
            kPad, kPad, rc.right - kPad * 2, kEditH,
            hwnd, (HMENU)kEditId, GetModuleHandleW(nullptr), nullptr);

        // Subclass edit for ENTER/ESC handling
        WNDPROC origEdit = (WNDPROC)GetWindowLongPtrW(g_edit, GWLP_WNDPROC);
        SetWindowLongPtrW(g_edit, GWLP_USERDATA, (LONG_PTR)origEdit);
        SetWindowLongPtrW(g_edit, GWLP_WNDPROC, (LONG_PTR)EditProc);

        // Listbox for results
        g_list = CreateWindowExW(
            0, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | LBS_NOTIFY | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
            kPad, kListY, rc.right - kPad * 2, kListH,
            hwnd, (HMENU)kListId, GetModuleHandleW(nullptr), nullptr);

        ApplyTheme();

        // Start app scanner in background
        std::thread(ScanApps).detach();

        // Start global hotkey thread
        g_hotkeyThread = CreateThread(nullptr, 0, HotkeyThreadProc, nullptr, 0, nullptr);

        break;
    }

    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_dark ? kTextDark : kTextLight);
        SetBkColor(dc, g_dark ? kBgEditDark : kBgEditLight);
        return (LRESULT)g_editBg;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_dark ? kTextDark : kTextLight);
        SetBkColor(dc, g_dark ? kBgDark : kBgLight);
        return (LRESULT)g_bg;
    }

    case WM_COMMAND: {
        if (HIWORD(wp) == LBN_DBLCLK && LOWORD(wp) == kListId) {
            int idx = SendMessageW(g_list, LB_GETCURSEL, 0, 0);
            if (idx != LB_ERR) {
                wchar_t name[256];
                SendMessageW(g_list, LB_GETTEXT, idx, (LPARAM)name);
                std::wstring sname(name);
                std::lock_guard<std::mutex> lock(g_appMutex);
                auto it = g_appIndex.find(sname);
                if (it != g_appIndex.end())
                    LaunchProcess(it->second);
                ToggleWindow();
            }
        }
        break;
    }

    case WM_TIMER: {
        if (wp == kAnimTimerId) {
            g_animStep++;
            double t = (double)g_animStep / kAnimSteps;
            // Ease-out cubic
            double eased = 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t);
            g_alpha = (uint8_t)(255.0 * eased);
            SetLayeredWindowAttributes(hwnd, 0, g_alpha, LWA_ALPHA);
            if (g_animStep >= kAnimSteps) {
                KillTimer(hwnd, kAnimTimerId);
                g_alpha = 255;
                SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
            }
        }
        break;
    }

    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH br = g_dark ? g_bg : CreateSolidBrush(kBgLight);
        FillRect((HDC)wp, &rc, br);
        if (!g_dark) DeleteObject(br);
        return 1;
    }

    case WM_ACTIVATE: {
        if (LOWORD(wp) == WA_INACTIVE) {
            ToggleWindow();
        }
        break;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
    return 0;
}

} // anonymous namespace

// ═══════════════════════════════════════════
//  WINMAIN
// ═══════════════════════════════════════════

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    // Ensure COM is initialized for IShellLink
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Common controls (if needed)
    INITCOMMONCONTROLSEX icex = { sizeof(icex), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icex);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = kClassName;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassExW(&wc);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x  = (sw - kWinW) / 2;
    int y  = (sh - kWinH) / 3;

    g_window = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kClassName, L"Omnibox",
        WS_POPUP,
        x, y, kWinW, kWinH,
        nullptr, nullptr, hInstance, nullptr);

    if (!g_window) {
        CoUninitialize();
        return 1;
    }

    // Start hidden — user will summon with Alt+Space
    ShowWindow(g_window, SW_HIDE);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hotkeyThread) {
        PostThreadMessageW(GetThreadId(g_hotkeyThread), WM_QUIT, 0, 0);
        WaitForSingleObject(g_hotkeyThread, 2000);
        CloseHandle(g_hotkeyThread);
    }

    CoUninitialize();
    return 0;
}
