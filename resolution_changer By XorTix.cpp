#pragma comment(linker, "/SUBSYSTEM:WINDOWS")

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>

#ifndef DM_DISPLAYFIXEDOUTPUT
#define DM_DISPLAYFIXEDOUTPUT 0x20000000
#endif
#ifndef DMDFO_DEFAULT
#define DMDFO_DEFAULT 0
#define DMDFO_STRETCH 1
#define DMDFO_CENTER  2
#endif

#define ID_COMBO    101
#define ID_APPLY    102
#define ID_RESET    103
#define ID_FULL     104
#define ID_STATUS   105
#define ID_SCALE    106

struct CustomRes { DWORD w, h; };
static const CustomRes kCustomRes[] = {
    { 1440, 1080 },
};

struct Mode {
    DEVMODEW dm;
    bool custom;
};

static std::vector<Mode> g_modes;
static HWND g_combo, g_scale, g_status, g_full;
static bool g_fullscreen = false;
static WINDOWPLACEMENT g_prevPlacement = { sizeof(WINDOWPLACEMENT) };
static LONG g_prevStyle = 0;

static void SetStatus(const std::wstring& s) {
    SetWindowTextW(g_status, s.c_str());
}

static bool SameMode(const DEVMODEW& a, const DEVMODEW& b) {
    return a.dmPelsWidth == b.dmPelsWidth && a.dmPelsHeight == b.dmPelsHeight &&
        a.dmDisplayFrequency == b.dmDisplayFrequency;
}

static bool Exists(const DEVMODEW& dm) {
    for (auto& m : g_modes) if (SameMode(m.dm, dm)) return true;
    return false;
}

static void EnumerateModes() {
    g_modes.clear();
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(nullptr, i, &dm); ++i) {
        if (dm.dmBitsPerPel < 32) continue;
        if (!Exists(dm)) g_modes.push_back({ dm, false });
    }

    DEVMODEW cur = {};
    cur.dmSize = sizeof(cur);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &cur);
    DWORD freqs[2] = { cur.dmDisplayFrequency, 60 };
    for (const auto& c : kCustomRes) {
        for (DWORD f : freqs) {
            DEVMODEW cm = cur;
            cm.dmPelsWidth = c.w;
            cm.dmPelsHeight = c.h;
            cm.dmDisplayFrequency = f;
            cm.dmBitsPerPel = 32;
            if (!Exists(cm)) g_modes.push_back({ cm, true });
        }
    }

    std::sort(g_modes.begin(), g_modes.end(), [](const Mode& a, const Mode& b) {
        if (a.dm.dmPelsWidth != b.dm.dmPelsWidth) return a.dm.dmPelsWidth > b.dm.dmPelsWidth;
        if (a.dm.dmPelsHeight != b.dm.dmPelsHeight) return a.dm.dmPelsHeight > b.dm.dmPelsHeight;
        return a.dm.dmDisplayFrequency > b.dm.dmDisplayFrequency;
        });
}

static void FillCombo() {
    SendMessageW(g_combo, CB_RESETCONTENT, 0, 0);
    DEVMODEW cur = {};
    cur.dmSize = sizeof(cur);
    EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &cur);
    int sel = 0;
    for (size_t i = 0; i < g_modes.size(); ++i) {
        const auto& m = g_modes[i].dm;
        std::wstring t = std::to_wstring(m.dmPelsWidth) + L" x " + std::to_wstring(m.dmPelsHeight) +
            L"  @ " + std::to_wstring(m.dmDisplayFrequency) + L" Hz";
        if (g_modes[i].custom) t += L"  (perso)";
        SendMessageW(g_combo, CB_ADDSTRING, 0, (LPARAM)t.c_str());
        if (SameMode(m, cur)) sel = (int)i;
    }
    SendMessageW(g_combo, CB_SETCURSEL, sel, 0);
}

static void ApplyMode() {
    int idx = (int)SendMessageW(g_combo, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)g_modes.size()) return;
    DEVMODEW dm = g_modes[idx].dm;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;

    int sc = (int)SendMessageW(g_scale, CB_GETCURSEL, 0, 0);
    DWORD fixedOutput = (sc == 1) ? DMDFO_CENTER : (sc == 2) ? DMDFO_DEFAULT : DMDFO_STRETCH;
    bool scalingRequested = (sc != 2);

    DEVMODEW withScale = dm;
    withScale.dmFields |= DM_DISPLAYFIXEDOUTPUT;
    withScale.dmDisplayFixedOutput = fixedOutput;

    bool scalingApplied = scalingRequested;
    DEVMODEW* chosen = scalingRequested ? &withScale : &dm;

    LONG t = ChangeDisplaySettingsExW(nullptr, chosen, nullptr, CDS_TEST, nullptr);
    if (t != DISP_CHANGE_SUCCESSFUL && scalingRequested) {
        chosen = &dm;
        scalingApplied = false;
        t = ChangeDisplaySettingsExW(nullptr, chosen, nullptr, CDS_TEST, nullptr);
    }
    if (t != DISP_CHANGE_SUCCESSFUL) {
        std::wstring msg = L"Resolution refused by the pilot.";
        if (g_modes[idx].custom)
            msg += L"\nAMD : active \"GPU Scaling\" in Adrenalin (Settings > Display)"
            L" or creates a custom resolution.";
        SetStatus(msg);
        return;
    }

    LONG r = ChangeDisplaySettingsExW(nullptr, chosen, nullptr, 0, nullptr);
    if (r == DISP_CHANGE_SUCCESSFUL) {
        std::wstring msg = L"Applied : " + std::to_wstring(dm.dmPelsWidth) + L"x" + std::to_wstring(dm.dmPelsHeight);
        if (scalingRequested && !scalingApplied)
            msg += L"\n(scale not applied: Enables GPU scaling in Adrenalin)";
        SetStatus(msg);
    }
    else {
        SetStatus(L"Failed to change resolution.");
    }
}

static void ResetMode() {
    LONG r = ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
    SetStatus(r == DISP_CHANGE_SUCCESSFUL ? L"Original resolution restored." : L"Failed to restore original resolution.");
    FillCombo();
}

static void ToggleFullscreen(HWND hwnd) {
    if (!g_fullscreen) {
        g_prevStyle = GetWindowLongW(hwnd, GWL_STYLE);
        GetWindowPlacement(hwnd, &g_prevPlacement);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongW(hwnd, GWL_STYLE, g_prevStyle & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = true;
    }
    else {
        SetWindowLongW(hwnd, GWL_STYLE, g_prevStyle);
        SetWindowPlacement(hwnd, &g_prevPlacement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        g_fullscreen = false;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        HWND lbl = CreateWindowW(L"STATIC", L"RResolution:", WS_CHILD | WS_VISIBLE,
            20, 15, 300, 20, hwnd, nullptr, nullptr, nullptr);
        g_combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 38, 340, 300, hwnd, (HMENU)ID_COMBO, nullptr, nullptr);
        HWND lbl2 = CreateWindowW(L"STATIC", L"Scale:", WS_CHILD | WS_VISIBLE,
            20, 72, 300, 20, hwnd, nullptr, nullptr, nullptr);
        g_scale = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20, 95, 340, 200, hwnd, (HMENU)ID_SCALE, nullptr, nullptr);
        SendMessageW(g_scale, CB_ADDSTRING, 0, (LPARAM)L"Fullscreen (stretch image)");
        SendMessageW(g_scale, CB_ADDSTRING, 0, (LPARAM)L"Centered (black bars)");
        SendMessageW(g_scale, CB_ADDSTRING, 0, (LPARAM)L"Default driver setting");
        SendMessageW(g_scale, CB_SETCURSEL, 0, 0);
        HWND bApply = CreateWindowW(L"BUTTON", L"Apply", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            20, 135, 160, 32, hwnd, (HMENU)ID_APPLY, nullptr, nullptr);
        HWND bReset = CreateWindowW(L"BUTTON", L"Reset (restore normal)", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            190, 135, 170, 32, hwnd, (HMENU)ID_RESET, nullptr, nullptr);
        g_full = CreateWindowW(L"BUTTON", L"Fullscreen window (F11)", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20, 180, 340, 22, hwnd, (HMENU)ID_FULL, nullptr, nullptr);
        g_status = CreateWindowW(L"STATIC", L"Ready.", WS_CHILD | WS_VISIBLE,
            20, 212, 340, 55, hwnd, (HMENU)ID_STATUS, nullptr, nullptr);
        for (HWND h : { lbl, g_combo, lbl2, g_scale, bApply, bReset, g_full, g_status })
            SendMessageW(h, WM_SETFONT, (WPARAM)font, TRUE);
        EnumerateModes();
        FillCombo();
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_APPLY: ApplyMode(); break;
        case ID_RESET: ResetMode(); break;
        case ID_FULL:  ToggleFullscreen(hwnd); break;
        }
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_F11) {
            ToggleFullscreen(hwnd);
            SendMessageW(g_full, BM_SETCHECK, g_fullscreen ? BST_CHECKED : BST_UNCHECKED, 0);
        }
        return 0;
    case WM_DESTROY:
        ChangeDisplaySettingsExW(nullptr, nullptr, nullptr, 0, nullptr);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nShow) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ResChangerWnd";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Resolution Change",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 330, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}