#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <windowsx.h>
#include <commdlg.h>
#include <stdio.h>
#include <wchar.h>
#include <vector>

template<typename T> static inline T MIN(T a, T b) { return a < b ? a : b; }
template<typename T> static inline T MAX(T a, T b) { return a > b ? a : b; }

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

typedef BOOL (WINAPI *pSetProcessDpiAwarenessContext)(DPI_AWARENESS_CONTEXT);
typedef BOOL (WINAPI *pSetWindowDisplayAffinity)(HWND, DWORD);
typedef BOOL (WINAPI *pGetWindowDisplayAffinity)(HWND, DWORD*);

enum ACCENT_STATE {
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
};

struct ACCENT_POLICY {
    ACCENT_STATE AccentState;
    DWORD AccentFlags;
    DWORD GradientColor;
    DWORD AnimationId;
};

struct WINDOWCOMPOSITIONATTRIBDATA {
    DWORD Attribute;
    PVOID Data;
    SIZE_T Size;
};

#define WCA_ACCENT_POLICY 19

typedef BOOL (WINAPI *pSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);

// ############################################################
// #              ★  可调参数区（改了直接重编）  ★              #
// ############################################################

#define APP_NAME  L"ScreenBlur"

#define BORDER_W 8
#define MIN_W    24
#define MIN_H    24

#define BORDER_OUTER RGB(40, 40, 40)
#define BORDER_INNER RGB(220, 220, 220)
#define PADDING_COLOR RGB(32, 32, 32)

const int BLUR_RADII[] = { 3, 6, 12, 32 };
const wchar_t* BLUR_NAMES[] = {
    L"轻度", L"中度", L"重度", L"极重"
};

const int REFRESH_MS_OPTIONS[] = { 0, 100, 500, 1000, 2000 };
const wchar_t* REFRESH_MS_NAMES[] = {
    L"关闭", L"0.1 秒", L"0.5 秒", L"1 秒", L"2 秒"
};
#define REFRESH_OPTION_COUNT \
    (sizeof(REFRESH_MS_OPTIONS) / sizeof(REFRESH_MS_OPTIONS[0]))

#define ENGINE_GAUSSIAN 0
#define ENGINE_ACRYLIC  1

#define ACRYLIC_COLOR_SLOTS 5
#define ACRYLIC_COLOR_UNSET ((COLORREF)0xFFFFFFFF)

const int ACRYLIC_ALPHA_PERCENTS[] = {
    0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100
};
#define ACRYLIC_ALPHA_COUNT \
    (sizeof(ACRYLIC_ALPHA_PERCENTS) / sizeof(ACRYLIC_ALPHA_PERCENTS[0]))

#define ACRYLIC_BG_ALPHA 1
#define ACRYLIC_FAST_ALPHA   220

// ############################################################

#define WND_CLASS L"ScreenBlurWnd"
#define TRAY_ID   1
#define WM_TRAY   (WM_USER + 100)
#define WM_DELAYED_CAPTURE (WM_APP + 1)

#define IDM_BLUR_1 1001
#define IDM_BLUR_2 1002
#define IDM_BLUR_3 1003
#define IDM_BLUR_4 1004

#define IDM_LOCK    1010
#define IDM_TOPMOST 1011
#define IDM_RESET   1012
#define IDM_REFRESH 1013

#define IDM_RF_BASE 1020

#define IDM_ENGINE_GAUSSIAN 1030
#define IDM_ENGINE_ACRYLIC  1031
#define IDM_CAPTURABLE      1032

#define IDM_ABOUT 1098
#define IDM_EXIT  1099

#define IDM_COLOR_BASE      1100
#define IDM_COLOR_EDIT_BASE 1110
#define IDM_ALPHA_BASE      1120

#define TIMER_ID 1

// ============================================================
//  全局
// ============================================================
HINSTANCE g_hInst = nullptr;
HWND      g_hwnd  = nullptr;
HICON     g_hIcon = nullptr;

pSetWindowDisplayAffinity g_pSetWDA = nullptr;
pGetWindowDisplayAffinity g_pGetWDA = nullptr;
pSetWindowCompositionAttribute g_pSetWCA = nullptr;
BOOL g_wdaOK = FALSE;
BOOL g_wcaOK = FALSE;

int  g_engine     = ENGINE_GAUSSIAN;
int  g_refreshMs  = 1000;
BOOL g_capturable = FALSE;

COLORREF g_acrylicColors[ACRYLIC_COLOR_SLOTS] = {
    RGB(255, 255, 255),
    ACRYLIC_COLOR_UNSET,
    ACRYLIC_COLOR_UNSET,
    ACRYLIC_COLOR_UNSET,
    ACRYLIC_COLOR_UNSET
};
int g_acrylicColorCurrent = 0;
int g_acrylicAlphaPercent = 50;

COLORREF g_custColors[16] = {};

int  g_blurLevel = 1;
BOOL g_locked    = FALSE;
BOOL g_topmost   = TRUE;

int  g_winX = 100, g_winY = 100, g_winW = 600, g_winH = 400;

BOOL g_dragging   = FALSE;
BOOL g_showBorder = FALSE;
BOOL g_fastMode   = FALSE;
int  g_dragOffX = 0, g_dragOffY = 0;

HDC     g_hdcMem = nullptr;
HBITMAP g_hBitmap = nullptr;
BYTE*   g_pBits = nullptr;
int     g_captureW = 0, g_captureH = 0;

HDC     g_hdcLayered = nullptr;
HBITMAP g_hbmLayered = nullptr;
void*   g_pLayeredBits = nullptr;
int     g_layeredW = 0, g_layeredH = 0;

// ============================================================
//  自绘菜单项
// ============================================================
struct OwnerDrawItem {
    enum Type { COLOR, EDIT_COLOR, ALPHA } type;
    int index;
    COLORREF color;
    int alphaPercent;
    BOOL isSet;
    BOOL isSelected;
};

std::vector<OwnerDrawItem*> g_ownerDrawItems;

static OwnerDrawItem* AllocOwnerDraw() {
    OwnerDrawItem* p = new OwnerDrawItem();
    g_ownerDrawItems.push_back(p);
    return p;
}

static void FreeAllOwnerDraw() {
    for (size_t i = 0; i < g_ownerDrawItems.size(); i++) {
        delete g_ownerDrawItems[i];
    }
    g_ownerDrawItems.clear();
}

// ============================================================
//  前向声明
// ============================================================
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
void CaptureAndBlur();
void SaveSettings();
void LoadSettings();
void AddTrayIcon();
void RemoveTrayIcon();
void ShowContextMenu();
void CleanupCapture();
void ApplyRefreshTimer();
void ShowAboutBox(HWND owner);
void ApplyWDAState();
void ApplyAcrylic();
void DisableAcrylic();
void SwitchEngine(int newEngine);
void ApplyWindowStyles();
void EnterFastMode();
void ExitFastMode();
void UpdateLayeredDisplay();
void FreeLayeredBmp();
DWORD GetCurrentAcrylicColor();
void EditColorSlot(HWND owner, int index);

// ============================================================
//  INI
// ============================================================
static const wchar_t* GetIniPath() {
    static wchar_t path[MAX_PATH] = {};
    if (path[0] == 0) {
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        wchar_t* p = wcsrchr(path, L'\\');
        if (p) *(p + 1) = 0;
        lstrcatW(path, L"ScreenBlur.ini");
    }
    return path;
}

// ============================================================
//  软件说明
// ============================================================
void ShowAboutBox(HWND owner) {
    const wchar_t* title = L"软件说明";
    const wchar_t* body =
        L"ScreenBlur  v1.2\r\n"
        L"\r\n"
        L"一个半透明磨砂窗口，用来遮住下边的显示内容。\r\n"
        L"\r\n"
        L"【操作】\r\n"
        L"  拖动窗口中间 ………… 移动位置\r\n"
        L"  拖动窗口边缘 ………… 调整大小\r\n"
        L"  右键窗口/托盘图标 … 打开菜单\r\n"
        L"\r\n"
        L"【模糊方式】——两种模式\r\n"
        L"\r\n"
        L"  ▸ 高斯模糊\r\n"
        L"    使用高斯模糊窗口下层内容\r\n"
        L"    可调模糊程度和刷新频率；\r\n"
        L"    勾选\"允许截图/录屏\"后，才能被截图/录制到本遮罩窗口，\r\n"
        L"    此时不会自动刷新窗口内容。\r\n"
        L"\r\n"
        L"  ▸ 彩色磨砂\r\n"
        L"    调用 Windows Acrylic 原生磨砂效果。\r\n"
        L"    可单独调整遮罩颜色和颜色浓度。\r\n"
        L"    画面完全实时更新，可被截图/录制到本遮罩窗口。\r\n"
        L"\r\n"
        L"【刷新频率】——仅高斯模糊模式\r\n"
        L"  关闭 / 0.1秒 / 0.5秒 / 1秒 / 2秒\r\n"
        L"  频率越快，底下内容的更新越实时，系统开销越高；\r\n"
        L"  窗口面积越小，系统开销越低。\r\n"
        L"\r\n"
        L"【锁定窗口】——鼠标穿透\r\n"
        L"  勾选后窗口不可拖动、不可调整大小，\r\n"
        L"  且鼠标点击会穿透到下面的窗口，\r\n"
        L"  此时无法在窗口上右键弹出菜单，\r\n"
        L"  请从托盘菜单中取消勾选以解锁。\r\n"
        L"\r\n"
        L"【重置位置】\r\n"
        L"  重置窗口大小位置到桌面左上角附近，\r\n"
        L"  在难以发现半透明窗口时使用 (●'◡'●)\r\n"
        L"\r\n"
        L"配置文件：同目录下 ScreenBlur.ini\r\n"
        L"系统要求：Windows 10 1803 或更高版本\r\n"
        L"\r\n"
        L"功能指导：愤怒的越野兔\r\n"
        L"程序代码：DeepSeek";

    MessageBoxW(owner, body, title, MB_OK | MB_ICONINFORMATION);
}

// ============================================================
//  模糊算法
// ============================================================
static void BoxBlurHorizontal(BYTE* src, BYTE* dst, int w, int h, int radius) {
    int diameter = radius * 2 + 1;
    for (int y = 0; y < h; y++) {
        BYTE* s = src + y * w * 4;
        BYTE* d = dst + y * w * 4;
        int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
        for (int i = -radius; i <= radius; i++) {
            int xi = MIN(MAX(i, 0), w - 1);
            BYTE* p = s + xi * 4;
            sumB += p[0]; sumG += p[1]; sumR += p[2]; sumA += p[3];
        }
        for (int x = 0; x < w; x++) {
            d[x * 4 + 0] = (BYTE)(sumB / diameter);
            d[x * 4 + 1] = (BYTE)(sumG / diameter);
            d[x * 4 + 2] = (BYTE)(sumR / diameter);
            d[x * 4 + 3] = (BYTE)(sumA / diameter);
            int xOut = MIN(MAX(x - radius, 0), w - 1);
            int xIn  = MIN(MAX(x + radius + 1, 0), w - 1);
            BYTE* pOut = s + xOut * 4;
            BYTE* pIn  = s + xIn * 4;
            sumB += pIn[0] - pOut[0];
            sumG += pIn[1] - pOut[1];
            sumR += pIn[2] - pOut[2];
            sumA += pIn[3] - pOut[3];
        }
    }
}

static void BoxBlurVertical(BYTE* src, BYTE* dst, int w, int h, int radius) {
    int diameter = radius * 2 + 1;
    for (int x = 0; x < w; x++) {
        BYTE* s = src + x * 4;
        BYTE* d = dst + x * 4;
        int sumB = 0, sumG = 0, sumR = 0, sumA = 0;
        for (int i = -radius; i <= radius; i++) {
            int yi = MIN(MAX(i, 0), h - 1);
            BYTE* p = s + yi * w * 4;
            sumB += p[0]; sumG += p[1]; sumR += p[2]; sumA += p[3];
        }
        for (int y = 0; y < h; y++) {
            d[y * w * 4 + 0] = (BYTE)(sumB / diameter);
            d[y * w * 4 + 1] = (BYTE)(sumG / diameter);
            d[y * w * 4 + 2] = (BYTE)(sumR / diameter);
            d[y * w * 4 + 3] = (BYTE)(sumA / diameter);
            int yOut = MIN(MAX(y - radius, 0), h - 1);
            int yIn  = MIN(MAX(y + radius + 1, 0), h - 1);
            BYTE* pOut = s + yOut * w * 4;
            BYTE* pIn  = s + yIn * w * 4;
            sumB += pIn[0] - pOut[0];
            sumG += pIn[1] - pOut[1];
            sumR += pIn[2] - pOut[2];
            sumA += pIn[3] - pOut[3];
        }
    }
}

static void FastBlur(BYTE* data, int w, int h, int radius) {
    if (radius < 1) return;
    std::vector<BYTE> temp(w * h * 4);
    for (int pass = 0; pass < 3; pass++) {
        BoxBlurHorizontal(data, temp.data(), w, h, radius);
        BoxBlurVertical(temp.data(), data, w, h, radius);
    }
}

// ============================================================
//  分层位图
// ============================================================
void FreeLayeredBmp() {
    if (g_hbmLayered) { DeleteObject(g_hbmLayered); g_hbmLayered = nullptr; }
    if (g_hdcLayered) { DeleteDC(g_hdcLayered); g_hdcLayered = nullptr; }
    g_pLayeredBits = nullptr;
    g_layeredW = 0;
    g_layeredH = 0;
}

static void EnsureLayeredBmp(int w, int h) {
    if (g_layeredW == w && g_layeredH == h && g_hdcLayered) return;
    FreeLayeredBmp();

    HDC hdcScreen = GetDC(nullptr);
    g_hdcLayered = CreateCompatibleDC(hdcScreen);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    g_hbmLayered = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS,
                                    &g_pLayeredBits, nullptr, 0);
    SelectObject(g_hdcLayered, g_hbmLayered);

    ReleaseDC(nullptr, hdcScreen);
    g_layeredW = w;
    g_layeredH = h;
}

static void FillLayeredTransparent() {
    if (!g_pLayeredBits) return;
    DWORD* p = (DWORD*)g_pLayeredBits;
    DWORD pixel = ((DWORD)ACRYLIC_BG_ALPHA << 24);
    int n = g_layeredW * g_layeredH;
    for (int i = 0; i < n; i++) p[i] = pixel;
}

static void FillLayeredFastMode() {
    if (!g_pLayeredBits) return;
    DWORD* p = (DWORD*)g_pLayeredBits;
    DWORD gray = 0xFF000000 |
                 ((DWORD)GetBValue(PADDING_COLOR) << 16) |
                 ((DWORD)GetGValue(PADDING_COLOR) << 8) |
                 ((DWORD)GetRValue(PADDING_COLOR));
    int n = g_layeredW * g_layeredH;
    for (int i = 0; i < n; i++) p[i] = gray;

    int cw = g_layeredW;
    int ch = g_layeredH;
    if (cw < 4 || ch < 4) return;

    HBRUSH hOldBrush = (HBRUSH)SelectObject(g_hdcLayered, GetStockObject(NULL_BRUSH));
    HPEN hPenOuter = CreatePen(PS_SOLID, 1, BORDER_OUTER);
    HPEN hOldPen = (HPEN)SelectObject(g_hdcLayered, hPenOuter);
    MoveToEx(g_hdcLayered, 0, 0, nullptr);
    LineTo(g_hdcLayered, cw - 1, 0);
    LineTo(g_hdcLayered, cw - 1, ch - 1);
    LineTo(g_hdcLayered, 0, ch - 1);
    LineTo(g_hdcLayered, 0, 0);

    HPEN hPenInner = CreatePen(PS_SOLID, 1, BORDER_INNER);
    SelectObject(g_hdcLayered, hPenInner);
    MoveToEx(g_hdcLayered, 1, 1, nullptr);
    LineTo(g_hdcLayered, cw - 2, 1);
    LineTo(g_hdcLayered, cw - 2, ch - 2);
    LineTo(g_hdcLayered, 1, ch - 2);
    LineTo(g_hdcLayered, 1, 1);

    SelectObject(g_hdcLayered, hOldPen);
    SelectObject(g_hdcLayered, hOldBrush);
    DeleteObject(hPenOuter);
    DeleteObject(hPenInner);
}

void UpdateLayeredDisplay() {
    if (!g_hwnd) return;
    if (g_engine != ENGINE_ACRYLIC) return;

    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    EnsureLayeredBmp(w, h);
    if (!g_hdcLayered || !g_pLayeredBits) return;

    if (g_fastMode) FillLayeredFastMode();
    else            FillLayeredTransparent();

    POINT ptSrc = { 0, 0 };
    POINT ptDst = { rc.left, rc.top };
    SIZE size = { w, h };

    BLENDFUNCTION bf = {};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    HDC hdcScreen = GetDC(nullptr);
    UpdateLayeredWindow(g_hwnd, hdcScreen, &ptDst, &size,
                        g_hdcLayered, &ptSrc, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, hdcScreen);
}

// ============================================================
//  Acrylic 颜色合成
// ============================================================
DWORD GetCurrentAcrylicColor() {
    COLORREF c = g_acrylicColors[g_acrylicColorCurrent];
    if (c == ACRYLIC_COLOR_UNSET) c = RGB(255, 255, 255);

    BYTE r = GetRValue(c);
    BYTE g = GetGValue(c);
    BYTE b = GetBValue(c);

    int pct = g_acrylicAlphaPercent;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    DWORD a = (DWORD)((pct * 255 + 50) / 100);

    return (a << 24) | ((DWORD)b << 16) | ((DWORD)g << 8) | r;
}

// ============================================================
//  Acrylic 开 / 关
// ============================================================
void ApplyAcrylic() {
    if (!g_pSetWCA || !g_hwnd) return;
    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags = 2;
    policy.GradientColor = GetCurrentAcrylicColor();
    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attribute = WCA_ACCENT_POLICY;
    data.Data = &policy;
    data.Size = sizeof(policy);
    g_pSetWCA(g_hwnd, &data);
}

void DisableAcrylic() {
    if (!g_pSetWCA || !g_hwnd) return;
    ACCENT_POLICY policy = {};
    policy.AccentState = ACCENT_DISABLED;
    WINDOWCOMPOSITIONATTRIBDATA data = {};
    data.Attribute = WCA_ACCENT_POLICY;
    data.Data = &policy;
    data.Size = sizeof(policy);
    g_pSetWCA(g_hwnd, &data);
}

// ============================================================
//  WDA
// ============================================================
void ApplyWDAState() {
    if (!g_pSetWDA || !g_hwnd) return;

    BOOL wantExclude = (g_engine == ENGINE_ACRYLIC) ? FALSE : (!g_capturable);

    if (wantExclude) {
        BOOL ok = g_pSetWDA(g_hwnd, WDA_EXCLUDEFROMCAPTURE);
        if (!ok) {
            g_pSetWDA(g_hwnd, WDA_NONE);
            ok = g_pSetWDA(g_hwnd, WDA_EXCLUDEFROMCAPTURE);
        }
        g_wdaOK = ok ? TRUE : FALSE;
    } else {
        g_pSetWDA(g_hwnd, WDA_NONE);
        g_wdaOK = FALSE;
    }
}

// ============================================================
//  窗口样式
// ============================================================
void ApplyWindowStyles() {
    if (!g_hwnd) return;

    LONG_PTR exOld = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
    LONG_PTR exNew = exOld;

    BOOL needLayered = (g_engine == ENGINE_ACRYLIC) || g_locked;
    if (needLayered) exNew |= WS_EX_LAYERED;
    else             exNew &= ~WS_EX_LAYERED;

    if (g_locked) exNew |= WS_EX_TRANSPARENT;
    else          exNew &= ~WS_EX_TRANSPARENT;

    if (exNew != exOld) {
        SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, exNew);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    if (g_engine == ENGINE_GAUSSIAN && g_locked) {
        SetLayeredWindowAttributes(g_hwnd, 0, 255, LWA_ALPHA);
    }
}

// ============================================================
//  快速模式
// ============================================================
void EnterFastMode() {
    if (g_engine != ENGINE_ACRYLIC) return;
    if (g_fastMode) return;
    g_fastMode = TRUE;
    DisableAcrylic();
    UpdateLayeredDisplay();
}

void ExitFastMode() {
    if (g_engine != ENGINE_ACRYLIC) return;
    if (!g_fastMode) return;
    g_fastMode = FALSE;
    ApplyAcrylic();
    UpdateLayeredDisplay();
}

// ============================================================
//  引擎切换
// ============================================================
void SwitchEngine(int newEngine) {
    if (g_engine == newEngine) return;
    g_engine = newEngine;
    g_fastMode = FALSE;

    if (g_engine == ENGINE_ACRYLIC) {
        KillTimer(g_hwnd, TIMER_ID);
        CleanupCapture();
        ApplyWindowStyles();
        ApplyWDAState();
        ApplyAcrylic();
        UpdateLayeredDisplay();
    } else {
        DisableAcrylic();
        ApplyWindowStyles();
        ApplyWDAState();
        PostMessage(g_hwnd, WM_DELAYED_CAPTURE, 0, 0);
        ApplyRefreshTimer();
    }
    InvalidateRect(g_hwnd, nullptr, TRUE);
    UpdateWindow(g_hwnd);
    SaveSettings();
}

// ============================================================
//  清理 / 截屏
// ============================================================
void CleanupCapture() {
    if (g_hBitmap) { DeleteObject(g_hBitmap); g_hBitmap = nullptr; }
    if (g_hdcMem)  { DeleteDC(g_hdcMem); g_hdcMem = nullptr; }
    g_pBits = nullptr;
    g_captureW = 0;
    g_captureH = 0;
}

void CaptureAndBlur() {
    if (!g_hwnd) return;
    if (g_engine != ENGINE_GAUSSIAN) return;

    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    if (w != g_captureW || h != g_captureH || !g_hdcMem) {
        CleanupCapture();
        HDC hdcScreen = GetDC(nullptr);
        g_hdcMem = CreateCompatibleDC(hdcScreen);
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        g_hBitmap = CreateDIBSection(hdcScreen, &bmi, DIB_RGB_COLORS,
                                     (void**)&g_pBits, nullptr, 0);
        SelectObject(g_hdcMem, g_hBitmap);
        ReleaseDC(nullptr, hdcScreen);
        g_captureW = w;
        g_captureH = h;
    }

    if (!g_pBits) return;
    if (!g_wdaOK) ShowWindow(g_hwnd, SW_HIDE);
    HDC hdcScreen = GetDC(nullptr);
    BitBlt(g_hdcMem, 0, 0, w, h, hdcScreen, rc.left, rc.top, SRCCOPY);
    ReleaseDC(nullptr, hdcScreen);
    if (!g_wdaOK) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);

    FastBlur(g_pBits, w, h, BLUR_RADII[g_blurLevel]);
    InvalidateRect(g_hwnd, nullptr, FALSE);
    UpdateWindow(g_hwnd);
}

// ============================================================
//  定时器
// ============================================================
void ApplyRefreshTimer() {
    if (!g_hwnd) return;
    KillTimer(g_hwnd, TIMER_ID);
    if (g_engine == ENGINE_GAUSSIAN && !g_capturable && g_refreshMs > 0) {
        SetTimer(g_hwnd, TIMER_ID, (UINT)g_refreshMs, nullptr);
    }
}

// ============================================================
//  保存 / 加载
// ============================================================
void SaveSettings() {
    if (!g_hwnd) return;
    RECT rc;
    GetWindowRect(g_hwnd, &rc);
    const wchar_t* ini = GetIniPath();
    wchar_t buf[32];

    #define WI(s, k, v) do { swprintf_s(buf, L"%u", (unsigned)(v)); WritePrivateProfileStringW(s, k, buf, ini); } while(0)

    WI(L"window", L"x", rc.left);
    WI(L"window", L"y", rc.top);
    WI(L"window", L"w", rc.right - rc.left);
    WI(L"window", L"h", rc.bottom - rc.top);
    WI(L"settings", L"blur", g_blurLevel);
    WI(L"settings", L"locked", g_locked ? 1 : 0);
    WI(L"settings", L"topmost", g_topmost ? 1 : 0);
    WI(L"settings", L"refresh_ms", g_refreshMs);
    WI(L"settings", L"blur_engine", g_engine);
    WI(L"settings", L"capturable", g_capturable ? 1 : 0);

    for (int i = 0; i < ACRYLIC_COLOR_SLOTS; i++) {
        wchar_t key[16];
        swprintf_s(key, L"color%d", i + 1);
        WI(L"acrylic", key, (unsigned)g_acrylicColors[i]);
    }
    WI(L"acrylic", L"color_current", g_acrylicColorCurrent);
    WI(L"acrylic", L"alpha_percent", g_acrylicAlphaPercent);

    #undef WI
}

void LoadSettings() {
    const wchar_t* ini = GetIniPath();
    g_winX = GetPrivateProfileIntW(L"window", L"x", 100, ini);
    g_winY = GetPrivateProfileIntW(L"window", L"y", 100, ini);
    g_winW = GetPrivateProfileIntW(L"window", L"w", 600, ini);
    g_winH = GetPrivateProfileIntW(L"window", L"h", 400, ini);
    g_blurLevel = GetPrivateProfileIntW(L"settings", L"blur", 1, ini);
    g_locked = GetPrivateProfileIntW(L"settings", L"locked", 0, ini);
    g_topmost = GetPrivateProfileIntW(L"settings", L"topmost", 1, ini);
    g_refreshMs = GetPrivateProfileIntW(L"settings", L"refresh_ms", 1000, ini);
    g_engine = GetPrivateProfileIntW(L"settings", L"blur_engine", ENGINE_GAUSSIAN, ini);
    g_capturable = GetPrivateProfileIntW(L"settings", L"capturable", 0, ini);

    for (int i = 0; i < ACRYLIC_COLOR_SLOTS; i++) {
        wchar_t key[16];
        wchar_t defVal[16];
        swprintf_s(key, L"color%d", i + 1);
        if (i == 0) swprintf_s(defVal, L"%u", (unsigned)RGB(255, 255, 255));
        else        swprintf_s(defVal, L"%u", (unsigned)ACRYLIC_COLOR_UNSET);

        wchar_t buf[32];
        GetPrivateProfileStringW(L"acrylic", key, defVal, buf, 32, ini);
        unsigned v = (unsigned)wcstoul(buf, nullptr, 10);
        g_acrylicColors[i] = (COLORREF)v;
    }
    g_acrylicColorCurrent = GetPrivateProfileIntW(L"acrylic", L"color_current", 0, ini);
    g_acrylicAlphaPercent = GetPrivateProfileIntW(L"acrylic", L"alpha_percent", 50, ini);

    if (g_blurLevel < 0) g_blurLevel = 0;
    if (g_blurLevel > 3) g_blurLevel = 3;
    if (g_winW < MIN_W) g_winW = MIN_W;
    if (g_winH < MIN_H) g_winH = MIN_H;
    if (g_acrylicColorCurrent < 0) g_acrylicColorCurrent = 0;
    if (g_acrylicColorCurrent >= ACRYLIC_COLOR_SLOTS) g_acrylicColorCurrent = 0;
    if (g_acrylicAlphaPercent < 0) g_acrylicAlphaPercent = 0;
    if (g_acrylicAlphaPercent > 100) g_acrylicAlphaPercent = 100;

    if (g_acrylicColors[g_acrylicColorCurrent] == ACRYLIC_COLOR_UNSET) {
        for (int i = 0; i < ACRYLIC_COLOR_SLOTS; i++) {
            if (g_acrylicColors[i] != ACRYLIC_COLOR_UNSET) {
                g_acrylicColorCurrent = i;
                break;
            }
        }
    }

    BOOL valid = FALSE;
    for (int i = 0; i < (int)REFRESH_OPTION_COUNT; i++) {
        if (g_refreshMs == REFRESH_MS_OPTIONS[i]) { valid = TRUE; break; }
    }
    if (!valid) g_refreshMs = 1000;
    if (g_engine != ENGINE_GAUSSIAN && g_engine != ENGINE_ACRYLIC) {
        g_engine = ENGINE_GAUSSIAN;
    }
}

// ============================================================
//  编辑颜色槽位
// ============================================================
void EditColorSlot(HWND owner, int index) {
    if (index < 0 || index >= ACRYLIC_COLOR_SLOTS) return;

    COLORREF initial = g_acrylicColors[index];
    if (initial == ACRYLIC_COLOR_UNSET) initial = RGB(255, 255, 255);

    CHOOSECOLORW cc = {};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = owner;
    cc.lpCustColors = g_custColors;
    cc.rgbResult = initial;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;

    if (ChooseColorW(&cc)) {
        g_acrylicColors[index] = cc.rgbResult;
        g_acrylicColorCurrent = index;
        if (g_engine == ENGINE_ACRYLIC) {
            ApplyAcrylic();
            UpdateLayeredDisplay();
        }
        SaveSettings();
    }
}

// ============================================================
//  托盘
// ============================================================
void AddTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon = g_hIcon;
    lstrcpynW(nid.szTip, APP_NAME, 128);
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void RemoveTrayIcon() {
    NOTIFYICONDATAW nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = g_hwnd;
    nid.uID = TRAY_ID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ============================================================
//  右键菜单
// ============================================================
void ShowContextMenu() {
    POINT pt;
    GetCursorPos(&pt);

    FreeAllOwnerDraw();

    HMENU hMenu       = CreatePopupMenu();
    HMENU hEngine     = CreatePopupMenu();
    HMENU hBlur       = CreatePopupMenu();
    HMENU hRefresh    = CreatePopupMenu();
    HMENU hColor      = CreatePopupMenu();
    HMENU hEditColor  = CreatePopupMenu();
    HMENU hAlpha      = CreatePopupMenu();

    BOOL isGaussian = (g_engine == ENGINE_GAUSSIAN);

    // 模糊方式
    AppendMenuW(hEngine, MF_STRING | (isGaussian ? MF_CHECKED : 0),
                IDM_ENGINE_GAUSSIAN, L"高斯模糊");
    AppendMenuW(hEngine, MF_STRING | (!isGaussian ? MF_CHECKED : 0) |
                (g_wcaOK ? 0 : MF_GRAYED),
                IDM_ENGINE_ACRYLIC, L"彩色磨砂");

    // 模糊程度
    for (int i = 0; i < 4; i++) {
        UINT f = MF_STRING;
        if (i == g_blurLevel) f |= MF_CHECKED;
        AppendMenuW(hBlur, f, IDM_BLUR_1 + i, BLUR_NAMES[i]);
    }

    // 刷新频率
    for (int i = 0; i < (int)REFRESH_OPTION_COUNT; i++) {
        UINT f = MF_STRING;
        if (g_refreshMs == REFRESH_MS_OPTIONS[i]) f |= MF_CHECKED;
        AppendMenuW(hRefresh, f, IDM_RF_BASE + i, REFRESH_MS_NAMES[i]);
    }

    // 遮罩颜色（自绘）
    for (int i = 0; i < ACRYLIC_COLOR_SLOTS; i++) {
        OwnerDrawItem* item = AllocOwnerDraw();
        item->type = OwnerDrawItem::COLOR;
        item->index = i;
        item->color = g_acrylicColors[i];
        item->isSet = (g_acrylicColors[i] != ACRYLIC_COLOR_UNSET);
        item->isSelected = (g_acrylicColorCurrent == i);
        item->alphaPercent = 0;

        UINT flags = MF_OWNERDRAW;
        if (!item->isSet) flags |= MF_GRAYED;

        AppendMenuW(hColor, flags, IDM_COLOR_BASE + i, (LPCWSTR)item);
    }

    // 编辑颜色（自绘，带色块）
    for (int i = 0; i < ACRYLIC_COLOR_SLOTS; i++) {
        OwnerDrawItem* item = AllocOwnerDraw();
        item->type = OwnerDrawItem::EDIT_COLOR;
        item->index = i;
        item->color = g_acrylicColors[i];
        item->isSet = (g_acrylicColors[i] != ACRYLIC_COLOR_UNSET);
        item->isSelected = FALSE;
        item->alphaPercent = 0;

        AppendMenuW(hEditColor, MF_OWNERDRAW,
                    IDM_COLOR_EDIT_BASE + i, (LPCWSTR)item);
    }

    // 颜色浓度（自绘）
    for (int i = 0; i < (int)ACRYLIC_ALPHA_COUNT; i++) {
        OwnerDrawItem* item = AllocOwnerDraw();
        item->type = OwnerDrawItem::ALPHA;
        item->index = i;
        item->alphaPercent = ACRYLIC_ALPHA_PERCENTS[i];
        item->isSet = TRUE;
        item->isSelected = (g_acrylicAlphaPercent == ACRYLIC_ALPHA_PERCENTS[i]);
        item->color = 0;

        AppendMenuW(hAlpha, MF_OWNERDRAW,
                    IDM_ALPHA_BASE + i, (LPCWSTR)item);
    }

    // ---- 主菜单 ----
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hEngine, L"模糊方式");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 高斯组
    UINT gaussianFlag = isGaussian ? 0 : MF_GRAYED;
    AppendMenuW(hMenu, MF_POPUP | gaussianFlag, (UINT_PTR)hBlur, L"模糊程度");
    AppendMenuW(hMenu, MF_POPUP | gaussianFlag, (UINT_PTR)hRefresh, L"刷新频率");
    AppendMenuW(hMenu, MF_STRING | gaussianFlag | (g_capturable ? MF_CHECKED : 0),
                IDM_CAPTURABLE, L"允许截图/录屏");
    AppendMenuW(hMenu, MF_STRING | gaussianFlag, IDM_REFRESH, L"刷新窗口");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // Acrylic 组
    UINT acrylicFlag = isGaussian ? MF_GRAYED : 0;
    AppendMenuW(hMenu, MF_POPUP | acrylicFlag, (UINT_PTR)hColor,     L"遮罩颜色");
    AppendMenuW(hMenu, MF_POPUP | acrylicFlag, (UINT_PTR)hEditColor, L"编辑颜色");
    AppendMenuW(hMenu, MF_POPUP | acrylicFlag, (UINT_PTR)hAlpha,     L"颜色浓度");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    // 窗口组：锁定 / 置顶 / 重置
    AppendMenuW(hMenu, MF_STRING | (g_locked ? MF_CHECKED : 0),
                IDM_LOCK, L"锁定窗口");
    AppendMenuW(hMenu, MF_STRING | (g_topmost ? MF_CHECKED : 0),
                IDM_TOPMOST, L"始终置顶");
    AppendMenuW(hMenu, MF_STRING, IDM_RESET, L"重置位置");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);

    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"软件说明");
    AppendMenuW(hMenu, MF_STRING, IDM_EXIT,  L"退出");

    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    PostMessage(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);

    FreeAllOwnerDraw();
}

// ============================================================
//  自绘菜单项绘制
// ============================================================
static void DrawOwnerMenuItem(DRAWITEMSTRUCT* dis) {
    OwnerDrawItem* item = (OwnerDrawItem*)dis->itemData;
    if (!item) return;

    BOOL isHighlight = (dis->itemState & ODS_SELECTED) ? TRUE : FALSE;
    BOOL isDisabled  = (dis->itemState & ODS_GRAYED)   ? TRUE : FALSE;

    COLORREF bgColor = isHighlight ? GetSysColor(COLOR_HIGHLIGHT)
                                   : GetSysColor(COLOR_MENU);
    COLORREF fgColor = isHighlight ? GetSysColor(COLOR_HIGHLIGHTTEXT)
                                   : GetSysColor(COLOR_MENUTEXT);
    if (isDisabled) fgColor = GetSysColor(COLOR_GRAYTEXT);

    HBRUSH hBg = CreateSolidBrush(bgColor);
    FillRect(dis->hDC, &dis->rcItem, hBg);
    DeleteObject(hBg);

    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, fgColor);

    int cy = dis->rcItem.bottom - dis->rcItem.top;
    int x  = dis->rcItem.left + 4;
    int y  = dis->rcItem.top;

    // 勾选标记
    if (item->isSelected) {
        int dotCx = x + 4;
        int dotCy = y + cy / 2;
        HBRUSH hDot = CreateSolidBrush(fgColor);
        HBRUSH hOld = (HBRUSH)SelectObject(dis->hDC, hDot);
        HPEN hOldPen = (HPEN)SelectObject(dis->hDC, GetStockObject(NULL_PEN));
        Ellipse(dis->hDC, dotCx - 3, dotCy - 3, dotCx + 3, dotCy + 3);
        SelectObject(dis->hDC, hOld);
        SelectObject(dis->hDC, hOldPen);
        DeleteObject(hDot);
    }
    x += 14;

    // 色块（COLOR 或 EDIT_COLOR 类型）
    if (item->type == OwnerDrawItem::COLOR ||
        item->type == OwnerDrawItem::EDIT_COLOR) {

        RECT rcSwatch = { x, y + 3, x + 16, y + cy - 3 };
        if (item->isSet) {
            HBRUSH hColor = CreateSolidBrush(item->color);
            FillRect(dis->hDC, &rcSwatch, hColor);
            DeleteObject(hColor);
            FrameRect(dis->hDC, &rcSwatch, GetSysColorBrush(COLOR_WINDOWFRAME));
        } else {
            HBRUSH hOldBrush = (HBRUSH)SelectObject(dis->hDC, GetStockObject(NULL_BRUSH));
            HPEN hPen = CreatePen(PS_SOLID, 1, GetSysColor(COLOR_GRAYTEXT));
            HPEN hOldPen = (HPEN)SelectObject(dis->hDC, hPen);
            Rectangle(dis->hDC, rcSwatch.left, rcSwatch.top,
                      rcSwatch.right, rcSwatch.bottom);
            SelectObject(dis->hDC, hOldPen);
            SelectObject(dis->hDC, hOldBrush);
            DeleteObject(hPen);
        }
        x += 22;
    }

    // 文字
    wchar_t text[64];
    if (item->type == OwnerDrawItem::COLOR) {
        if (item->isSet) {
            swprintf_s(text, L"颜色 %d  #%02X%02X%02X",
                       item->index + 1,
                       GetRValue(item->color),
                       GetGValue(item->color),
                       GetBValue(item->color));
        } else {
            swprintf_s(text, L"颜色 %d  （未设置）", item->index + 1);
        }
    } else if (item->type == OwnerDrawItem::EDIT_COLOR) {
        swprintf_s(text, L"编辑颜色 %d…", item->index + 1);
    } else {
        swprintf_s(text, L"%d%%", item->alphaPercent);
    }

    RECT rcText = dis->rcItem;
    rcText.left = x;
    DrawTextW(dis->hDC, text, -1, &rcText,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// ============================================================
//  窗口过程
// ============================================================
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT* mis = (MEASUREITEMSTRUCT*)lParam;
        if (mis && mis->CtlType == ODT_MENU && mis->itemData) {
            mis->itemHeight = 22;
            mis->itemWidth  = 200;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis && dis->CtlType == ODT_MENU && dis->itemData) {
            DrawOwnerMenuItem(dis);
            return TRUE;
        }
        break;
    }

    case WM_NCHITTEST: {
        if (g_locked) return HTCLIENT;
        int sx = GET_X_LPARAM(lParam);
        int sy = GET_Y_LPARAM(lParam);
        RECT rc;
        GetWindowRect(hwnd, &rc);
        BOOL L = sx <  rc.left   + BORDER_W;
        BOOL R = sx >= rc.right  - BORDER_W;
        BOOL T = sy <  rc.top    + BORDER_W;
        BOOL B = sy >= rc.bottom - BORDER_W;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        return HTCLIENT;
    }

    case WM_SETCURSOR: {
        if (LOWORD(lParam) == HTCLIENT) {
            SetCursor(LoadCursor(nullptr, g_locked ? IDC_ARROW : IDC_SIZEALL));
            return TRUE;
        }
        break;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = MIN_W;
        mmi->ptMinTrackSize.y = MIN_H;
        return 0;
    }

    case WM_ENTERSIZEMOVE:
        KillTimer(hwnd, TIMER_ID);
        g_showBorder = TRUE;
        EnterFastMode();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_EXITSIZEMOVE:
        g_showBorder = FALSE;
        ExitFastMode();
        if (g_engine == ENGINE_GAUSSIAN) CaptureAndBlur();
        SaveSettings();
        ApplyRefreshTimer();
        return 0;

    case WM_SIZE:
        if (g_engine == ENGINE_ACRYLIC) UpdateLayeredDisplay();
        if (g_showBorder) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_WINDOWPOSCHANGED:
        if (g_engine == ENGINE_ACRYLIC) UpdateLayeredDisplay();
        break;

    case WM_TIMER:
        if (wParam == TIMER_ID && !g_dragging && g_engine == ENGINE_GAUSSIAN) {
            CaptureAndBlur();
        }
        return 0;

    case WM_DELAYED_CAPTURE:
        if (g_engine == ENGINE_GAUSSIAN) CaptureAndBlur();
        return 0;

    case WM_LBUTTONDOWN: {
        if (!g_locked) {
            POINT pt;
            GetCursorPos(&pt);
            RECT rc;
            GetWindowRect(hwnd, &rc);
            g_dragOffX = pt.x - rc.left;
            g_dragOffY = pt.y - rc.top;
            g_dragging = TRUE;
            g_showBorder = TRUE;
            SetCapture(hwnd);
            KillTimer(hwnd, TIMER_ID);
            EnterFastMode();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (g_dragging) {
            POINT pt;
            GetCursorPos(&pt);
            SetWindowPos(hwnd, nullptr,
                         pt.x - g_dragOffX, pt.y - g_dragOffY,
                         0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }

    case WM_LBUTTONUP: {
        if (g_dragging) {
            g_dragging = FALSE;
            g_showBorder = FALSE;
            ReleaseCapture();
            ExitFastMode();
            if (g_engine == ENGINE_GAUSSIAN) CaptureAndBlur();
            SaveSettings();
            ApplyRefreshTimer();
        }
        return 0;
    }

    case WM_RBUTTONDOWN: {
        if (!g_locked) ShowContextMenu();
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        if (g_engine == ENGINE_GAUSSIAN) {
            if (g_hdcMem && g_captureW > 0 && g_captureH > 0) {
                if (g_showBorder) {
                    RECT rcClient;
                    GetClientRect(hwnd, &rcClient);
                    int cw = rcClient.right;
                    int ch = rcClient.bottom;

                    HBRUSH hBg = CreateSolidBrush(PADDING_COLOR);
                    FillRect(hdc, &rcClient, hBg);
                    DeleteObject(hBg);

                    int copyW = MIN(cw, g_captureW);
                    int copyH = MIN(ch, g_captureH);
                    if (copyW > 0 && copyH > 0) {
                        BitBlt(hdc, 0, 0, copyW, copyH,
                               g_hdcMem, 0, 0, SRCCOPY);
                    }

                    HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
                    HPEN hPenOuter = CreatePen(PS_SOLID, 1, BORDER_OUTER);
                    HPEN hOldPen = (HPEN)SelectObject(hdc, hPenOuter);
                    MoveToEx(hdc, 0, 0, nullptr);
                    LineTo(hdc, cw - 1, 0);
                    LineTo(hdc, cw - 1, ch - 1);
                    LineTo(hdc, 0, ch - 1);
                    LineTo(hdc, 0, 0);
                    HPEN hPenInner = CreatePen(PS_SOLID, 1, BORDER_INNER);
                    SelectObject(hdc, hPenInner);
                    MoveToEx(hdc, 1, 1, nullptr);
                    LineTo(hdc, cw - 2, 1);
                    LineTo(hdc, cw - 2, ch - 2);
                    LineTo(hdc, 1, ch - 2);
                    LineTo(hdc, 1, 1);
                    SelectObject(hdc, hOldPen);
                    SelectObject(hdc, hOldBrush);
                    DeleteObject(hPenOuter);
                    DeleteObject(hPenInner);
                } else {
                    BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top,
                           ps.rcPaint.right - ps.rcPaint.left,
                           ps.rcPaint.bottom - ps.rcPaint.top,
                           g_hdcMem, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
                }
            }
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_TRAY: {
        UINT evt = LOWORD(lParam);
        if (evt == WM_RBUTTONUP || evt == WM_CONTEXTMENU) ShowContextMenu();
        return 0;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);

        if (id >= IDM_RF_BASE && id < IDM_RF_BASE + (int)REFRESH_OPTION_COUNT) {
            int idx = id - IDM_RF_BASE;
            g_refreshMs = REFRESH_MS_OPTIONS[idx];
            ApplyRefreshTimer();
            SaveSettings();
            return 0;
        }

        if (id >= IDM_COLOR_BASE && id < IDM_COLOR_BASE + ACRYLIC_COLOR_SLOTS) {
            int idx = id - IDM_COLOR_BASE;
            if (g_acrylicColors[idx] != ACRYLIC_COLOR_UNSET) {
                g_acrylicColorCurrent = idx;
                if (g_engine == ENGINE_ACRYLIC) {
                    ApplyAcrylic();
                    UpdateLayeredDisplay();
                }
                SaveSettings();
            }
            return 0;
        }

        if (id >= IDM_COLOR_EDIT_BASE && id < IDM_COLOR_EDIT_BASE + ACRYLIC_COLOR_SLOTS) {
            int idx = id - IDM_COLOR_EDIT_BASE;
            EditColorSlot(hwnd, idx);
            return 0;
        }

        if (id >= IDM_ALPHA_BASE && id < IDM_ALPHA_BASE + (int)ACRYLIC_ALPHA_COUNT) {
            int idx = id - IDM_ALPHA_BASE;
            g_acrylicAlphaPercent = ACRYLIC_ALPHA_PERCENTS[idx];
            if (g_engine == ENGINE_ACRYLIC) {
                ApplyAcrylic();
                UpdateLayeredDisplay();
            }
            SaveSettings();
            return 0;
        }

        switch (id) {
        case IDM_ENGINE_GAUSSIAN:
            if (g_engine != ENGINE_GAUSSIAN) SwitchEngine(ENGINE_GAUSSIAN);
            break;
        case IDM_ENGINE_ACRYLIC:
            if (g_engine != ENGINE_ACRYLIC && g_wcaOK) SwitchEngine(ENGINE_ACRYLIC);
            break;

        case IDM_CAPTURABLE: {
            if (g_engine != ENGINE_GAUSSIAN) break;
            g_capturable = !g_capturable;
            ApplyWDAState();
            ApplyRefreshTimer();
            SaveSettings();
            if (g_capturable) {
                NOTIFYICONDATAW nid = {};
                nid.cbSize = sizeof(nid);
                nid.hWnd = g_hwnd;
                nid.uID = TRAY_ID;
                nid.uFlags = NIF_INFO;
                nid.dwInfoFlags = NIIF_INFO;
                lstrcpynW(nid.szInfoTitle, L"ScreenBlur", 64);
                lstrcpynW(nid.szInfo,
                          L"已允许截图/录屏，自动刷新已暂停。\r\n"
                          L"可手动点击\"刷新窗口\"更新画面。",
                          256);
                Shell_NotifyIconW(NIM_MODIFY, &nid);
            }
            break;
        }

        case IDM_BLUR_1:
        case IDM_BLUR_2:
        case IDM_BLUR_3:
        case IDM_BLUR_4:
            if (g_engine != ENGINE_GAUSSIAN) break;
            g_blurLevel = id - IDM_BLUR_1;
            CaptureAndBlur();
            SaveSettings();
            break;

        case IDM_LOCK: {
            g_locked = !g_locked;
            if (g_dragging) {
                g_dragging = FALSE;
                g_showBorder = FALSE;
                ReleaseCapture();
                ExitFastMode();
            }
            ApplyWindowStyles();
            SaveSettings();
            if (g_engine == ENGINE_ACRYLIC) UpdateLayeredDisplay();
            else InvalidateRect(hwnd, nullptr, TRUE);
            break;
        }

        case IDM_TOPMOST:
            g_topmost = !g_topmost;
            SetWindowPos(hwnd, g_topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SaveSettings();
            break;

        case IDM_REFRESH:
            if (g_engine == ENGINE_GAUSSIAN) CaptureAndBlur();
            break;

        case IDM_RESET:
            SetWindowPos(hwnd, nullptr, 100, 100, 600, 400,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (g_engine == ENGINE_GAUSSIAN) CaptureAndBlur();
            SaveSettings();
            break;

        case IDM_ABOUT: ShowAboutBox(hwnd); break;
        case IDM_EXIT:  DestroyWindow(hwnd); break;
        }
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_ID);
        CleanupCapture();
        FreeLayeredBmp();
        FreeAllOwnerDraw();
        RemoveTrayIcon();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ============================================================
//  入口
// ============================================================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    g_hInst = hInst;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        pSetProcessDpiAwarenessContext fnDpi =
            (pSetProcessDpiAwarenessContext)
            GetProcAddress(hUser32, "SetProcessDpiAwarenessContext");
        if (fnDpi) fnDpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        else       SetProcessDPIAware();

        g_pSetWDA = (pSetWindowDisplayAffinity)
                    GetProcAddress(hUser32, "SetWindowDisplayAffinity");
        g_pGetWDA = (pGetWindowDisplayAffinity)
                    GetProcAddress(hUser32, "GetWindowDisplayAffinity");
        g_pSetWCA = (pSetWindowCompositionAttribute)
                    GetProcAddress(hUser32, "SetWindowCompositionAttribute");
        g_wcaOK = (g_pSetWCA != nullptr);
    }

    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"ScreenBlur_SingleInstance_v1");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(hMutex);
        MessageBoxW(nullptr, L"ScreenBlur 已在运行（请查看系统托盘）",
                    L"ScreenBlur", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    LoadSettings();

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = nullptr;
    wc.hbrBackground = nullptr;
    wc.lpszClassName = WND_CLASS;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(1));

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(nullptr, L"注册窗口类失败", L"ScreenBlur", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_hIcon = wc.hIcon;

    DWORD exStyle = WS_EX_TOOLWINDOW;
    if (g_topmost) exStyle |= WS_EX_TOPMOST;
    if (g_locked)  exStyle |= WS_EX_TRANSPARENT;
    if (g_locked || g_engine == ENGINE_ACRYLIC) exStyle |= WS_EX_LAYERED;

    g_hwnd = CreateWindowExW(exStyle, WND_CLASS, APP_NAME, WS_POPUP,
                             g_winX, g_winY, g_winW, g_winH,
                             nullptr, nullptr, hInst, nullptr);

    if (!g_hwnd) {
        MessageBoxW(nullptr, L"创建窗口失败", L"ScreenBlur", MB_OK | MB_ICONERROR);
        return 1;
    }

    ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(g_hwnd);

    ApplyWDAState();
    ApplyWindowStyles();

    if (g_engine == ENGINE_ACRYLIC && g_wcaOK) {
        ApplyAcrylic();
        UpdateLayeredDisplay();
    } else {
        if (g_engine == ENGINE_ACRYLIC && !g_wcaOK) {
            g_engine = ENGINE_GAUSSIAN;
            ApplyWindowStyles();
        }
        CaptureAndBlur();
    }

    AddTrayIcon();
    ApplyRefreshTimer();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return (int)msg.wParam;
}