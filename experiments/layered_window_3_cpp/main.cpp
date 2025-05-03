#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <shellscalingapi.h>
#include <vector>
#include <mutex>
#include <stdio.h>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "WebView2Loader.lib")

using namespace Microsoft::WRL;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static HINSTANCE g_hInst = nullptr;
static HWND g_mainHwnd = nullptr;
static HWND g_hCommitted = nullptr;
static HWND g_hTransient = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;

// Transient stroke state
static POINT g_lastPoint = {0, 0};
static bool g_drawing = false;

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK StrokeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitializeWebView(HWND hwnd);
void ResizeOverlays();

// -----------------------------------------------------------------------------
// Initialize and embed WebView2 (Monaco) into the main window
// -----------------------------------------------------------------------------
void InitializeWebView(HWND hwnd)
{
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(hr) && env)
                {
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(hr2) && controller)
                                {
                                    g_controller = controller;
                                    controller->get_CoreWebView2(&g_webview);

                                    // Resize WebView to fill client area
                                    RECT rc;
                                    GetClientRect(hwnd, &rc);
                                    controller->put_Bounds(rc);
                                    controller->put_IsVisible(TRUE);

                                    // Navigate to local Monaco demo
                                    g_webview->Navigate(
                                        L"file:///C:/Users/ik1ne/Sources/Notetaking/experiments/layered_window_3_cpp/index.html"
                                    );
                                }
                                return S_OK;
                            }).Get());
                }
                return S_OK;
            }).Get());
}

// -----------------------------------------------------------------------------
// Resize and align overlay windows over the main client area
// -----------------------------------------------------------------------------
void ResizeOverlays()
{
    if (!g_mainHwnd) return;

    RECT rc;
    GetClientRect(g_mainHwnd, &rc);

    // Resize WebView2 control
    if (g_controller)
    {
        g_controller->put_Bounds(rc);
    }

    // Resize committed strokes layer
    if (g_hCommitted)
    {
        SetWindowPos(
            g_hCommitted,
            HWND_TOP,
            rc.left, rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            SWP_NOACTIVATE
        );
    }

    // Resize transient strokes layer
    if (g_hTransient)
    {
        SetWindowPos(
            g_hTransient,
            HWND_TOP,
            rc.left, rc.top,
            rc.right - rc.left,
            rc.bottom - rc.top,
            SWP_NOACTIVATE
        );
    }
}

// -----------------------------------------------------------------------------
// Window procedure for overlays (handles both committed and transient windows)
// -----------------------------------------------------------------------------
LRESULT CALLBACK StrokeWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static HPEN hRedPen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
    static HPEN hBlackPen = CreatePen(PS_SOLID, 3, RGB(0, 0, 0));

    switch (msg)
    {
    case WM_POINTERDOWN:
        {
            UINT32 pid;
            POINTER_INPUT_TYPE type;
            pid = GET_POINTERID_WPARAM(wParam);
            if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
            {
                // Pen down on transient layer only
                if (hwnd == g_hTransient)
                {
                    POINTER_INFO pi;
                    if (GetPointerInfo(pid, &pi))
                    {
                        g_drawing = true;
                        g_lastPoint.x = pi.ptPixelLocation.x;
                        g_lastPoint.y = pi.ptPixelLocation.y;
                        SetCapture(hwnd);
                    }
                }
            }
            return 0;
        }

    case WM_POINTERUPDATE:
        {
            if (!g_drawing || hwnd != g_hTransient)
                break;

            UINT32 pid;
            POINTER_INPUT_TYPE type;
            pid = GET_POINTERID_WPARAM(wParam);
            if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
            {
                POINTER_INFO pi;
                if (GetPointerInfo(pid, &pi))
                {
                    POINT pt = {(int)pi.ptPixelLocation.x, (int)pi.ptPixelLocation.y};

                    // Draw on transient layer immediately
                    HDC hdc = GetDC(hwnd);
                    SelectObject(hdc, hRedPen);
                    MoveToEx(hdc, g_lastPoint.x, g_lastPoint.y, nullptr);
                    LineTo(hdc, pt.x, pt.y);
                    ReleaseDC(hwnd, hdc);

                    g_lastPoint = pt;
                }
            }
            return 0;
        }

    case WM_POINTERUP:
        {
            if (!g_drawing || hwnd != g_hTransient)
                break;

            UINT32 pid;
            POINTER_INPUT_TYPE type;
            pid = GET_POINTERID_WPARAM(wParam);
            if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
            {
                POINTER_INFO pi;
                if (GetPointerInfo(pid, &pi))
                {
                    POINT pt = {(int)pi.ptPixelLocation.x, (int)pi.ptPixelLocation.y};

                    // Finish stroke on transient layer
                    HDC hdcTop = GetDC(hwnd);
                    SelectObject(hdcTop, hRedPen);
                    MoveToEx(hdcTop, g_lastPoint.x, g_lastPoint.y, nullptr);
                    LineTo(hdcTop, pt.x, pt.y);
                    ReleaseDC(hwnd, hdcTop);

                    // Commit stroke to committed layer
                    HDC hdcCommitted = GetDC(g_hCommitted);
                    SelectObject(hdcCommitted, hBlackPen);
                    MoveToEx(hdcCommitted, g_lastPoint.x, g_lastPoint.y, nullptr);
                    LineTo(hdcCommitted, pt.x, pt.y);
                    ReleaseDC(g_hCommitted, hdcCommitted);

                    // Clear transient layer (fill with transparency key)
                    RECT rc;
                    GetClientRect(hwnd, &rc);
                    HDC hdcClear = GetDC(hwnd);
                    HBRUSH hKeyBrush = CreateSolidBrush(RGB(1, 0, 0));
                    FillRect(hdcClear, &rc, hKeyBrush);
                    DeleteObject(hKeyBrush);
                    ReleaseDC(hwnd, hdcClear);

                    ReleaseCapture();
                    g_drawing = false;
                }
            }
            return 0;
        }

    case WM_ERASEBKGND:
        // Do nothing to prevent flicker
        return 1;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Main window procedure (hosts WebView2 and manages overlays)
// -----------------------------------------------------------------------------
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            g_mainHwnd = hwnd;

            // Initialize WebView2 (Monaco editor)
            InitializeWebView(hwnd);

            // Create committed-layer overlay
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g_hCommitted = CreateWindowEx(
                    WS_EX_LAYERED | WS_EX_NOACTIVATE,
                    L"StrokeWndClass",
                    nullptr,
                    WS_CHILD | WS_VISIBLE,
                    0, 0,
                    rc.right - rc.left,
                    rc.bottom - rc.top,
                    hwnd,
                    nullptr,
                    g_hInst,
                    nullptr
                );
                // Make key color transparent
                SetLayeredWindowAttributes(g_hCommitted, RGB(1, 0, 0), 0, LWA_COLORKEY);
            }

            // Create transient-layer overlay
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g_hTransient = CreateWindowEx(
                    WS_EX_LAYERED | WS_EX_NOACTIVATE,
                    L"StrokeWndClass",
                    nullptr,
                    WS_CHILD | WS_VISIBLE,
                    0, 0,
                    rc.right - rc.left,
                    rc.bottom - rc.top,
                    hwnd,
                    nullptr,
                    g_hInst,
                    nullptr
                );
                // Transparency key
                SetLayeredWindowAttributes(g_hTransient, RGB(1, 0, 0), 0, LWA_COLORKEY);

                // Register for pen input only
                RegisterPointerInputTarget(g_hTransient, PT_PEN);
            }

            break;
        }

    case WM_SIZE:
        ResizeOverlays();
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    g_hInst = hInstance;

    // High-DPI support
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnableMouseInPointer(TRUE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register main window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    // Register overlay window class
    WNDCLASS wc2 = {};
    wc2.lpfnWndProc = StrokeWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"StrokeWndClass";
    wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClass(&wc2);

    // Create main window
    HWND hwnd = CreateWindowEx(
        0,
        L"MainWindowClass",
        L"Three-Layer Note-Taking Demo",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr, nullptr,
        hInstance,
        nullptr
    );

    if (!hwnd)
        return -1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
