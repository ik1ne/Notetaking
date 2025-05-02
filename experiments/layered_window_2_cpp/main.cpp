#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <shellscalingapi.h>  // for DPI awareness
#include <vector>
#include <mutex>
#include <stdio.h>

#pragma comment(lib, "Shcore.lib")
using namespace Microsoft::WRL;

// Globals
static HWND g_mainHwnd = nullptr;
static HWND g_overlayHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static std::vector<POINT> g_strokePoints;
static std::mutex g_mutex;

// Initialize WebView2
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
                                    RECT rc;
                                    GetClientRect(hwnd, &rc);
                                    controller->put_Bounds(rc);
                                    controller->put_IsVisible(TRUE);
                                    g_webview->Navigate(
                                        L"file:///C:/Users/ik1ne/Sources/Notetaking/experiments/layered_window_2_cpp/index.html"
                                    );
                                }
                                return S_OK;
                            }).Get());
                }
                return S_OK;
            }).Get());
}

// Resize WebView2 and overlay
void ResizeChildren(const RECT& rc)
{
    if (g_controller)
    {
        g_controller->put_Bounds(rc);
    }
    if (g_overlayHwnd && g_mainHwnd)
    {
        POINT pt{rc.left, rc.top};
        ClientToScreen(g_mainHwnd, &pt);
        SetWindowPos(
            g_overlayHwnd,
            HWND_TOPMOST,
            pt.x, pt.y,
            rc.right - rc.left,
            rc.bottom - rc.top,
            SWP_NOACTIVATE);
    }
}

// Main window procedure
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_SIZE:
    case WM_MOVE:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            ResizeChildren(rc);
            break;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Overlay window procedure: pen & mouse drawing
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP)
    {
        UINT32 pid = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE type;
        if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
        {
            POINTER_INFO pi;
            if (GetPointerInfo(pid, &pi))
            {
                POINT pt = pi.ptPixelLocation;
                ScreenToClient(hwnd, &pt);
                std::lock_guard<std::mutex> lock(g_mutex);
                if (msg == WM_POINTERDOWN)
                {
                    g_strokePoints.clear();
                    g_strokePoints.push_back(pt);
                    SetCapture(hwnd);
                }
                else if (msg == WM_POINTERUPDATE)
                {
                    g_strokePoints.push_back(pt);
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
                else if (msg == WM_POINTERUP)
                {
                    ReleaseCapture();
                }
            }
        }
        return 0;
    }
    // Fallback to mouse events
    if (msg == WM_LBUTTONDOWN || msg == WM_MOUSEMOVE || msg == WM_LBUTTONUP)
    {
        bool btnDown = (wParam & MK_LBUTTON) != 0;
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        std::lock_guard<std::mutex> lock(g_mutex);
        if (msg == WM_LBUTTONDOWN)
        {
            g_strokePoints.clear();
            g_strokePoints.push_back(pt);
            SetCapture(hwnd);
        }
        else if (msg == WM_MOUSEMOVE && btnDown && GetCapture() == hwnd)
        {
            g_strokePoints.push_back(pt);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (msg == WM_LBUTTONUP)
        {
            ReleaseCapture();
        }
        return 0;
    }
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HPEN pen = CreatePen(PS_SOLID, 3, RGB(255, 0, 0));
        HPEN old = (HPEN)SelectObject(hdc, pen);
        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 1; i < g_strokePoints.size(); ++i)
        {
            MoveToEx(hdc, g_strokePoints[i - 1].x, g_strokePoints[i - 1].y, nullptr);
            LineTo(hdc, g_strokePoints[i].x, g_strokePoints[i].y);
        }
        SelectObject(hdc, old);
        DeleteObject(pen);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
    {
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Enable per-monitor DPI awareness for correct coordinate mapping
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnableMouseInPointer(TRUE);

    // Register main window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // Register overlay window
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"OverlayWindowClass";
    wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc2);

    // Create main window
    g_mainHwnd = CreateWindowExW(
        0,
        L"MainWindowClass",
        L"Two-Layer Note-Taking",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_mainHwnd, nCmdShow);

    // Initialize WebView2
    InitializeWebView(g_mainHwnd);

    // Create overlay
    g_overlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"OverlayWindowClass",
        nullptr, WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);
    SetLayeredWindowAttributes(g_overlayHwnd, 0, 128, LWA_ALPHA);
    ShowWindow(g_overlayHwnd, nCmdShow);

    // Initial sizing
    RECT rc;
    GetClientRect(g_mainHwnd, &rc);
    ResizeChildren(rc);

    // Main loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
