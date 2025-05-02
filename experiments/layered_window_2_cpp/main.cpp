#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <vector>
#include <mutex>
#include <stdio.h>

using namespace Microsoft::WRL;

// Globals
static HWND g_mainHwnd = nullptr;
static HWND g_overlayHwnd = nullptr;
static HWND g_webViewHwnd = nullptr;
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
            [&](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(hr) && env)
                {
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [&](HRESULT hr2, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(hr2) && controller)
                                {
                                    g_controller = controller;
                                    g_controller->get_Hwnd(&g_webViewHwnd);
                                    g_controller->get_CoreWebView2(&g_webview);

                                    // Size to client area
                                    RECT rc;
                                    GetClientRect(hwnd, &rc);
                                    g_controller->put_Bounds(rc);

                                    // Navigate to your local page
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

// Resize WebView2 and overlay to match main window
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
            SWP_NOACTIVATE
        );
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
            return 0;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Overlay window procedure for pen drawing and mouse forwarding
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_POINTERDOWN || msg == WM_POINTERUPDATE || msg == WM_POINTERUP)
    {
        UINT32 pid = GET_POINTERID_WPARAM(wParam);
        POINTER_INPUT_TYPE type;
        if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
        {
            // Record stroke points
            POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
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
        else
        {
            // Forward other pointer events as mouse messages
            if (g_webViewHwnd)
            {
                POINTER_INFO pi;
                if (GetPointerInfo(pid, &pi))
                {
                    POINT pt = pi.ptPixelLocation;
                    ScreenToClient(g_webViewHwnd, &pt);
                    UINT mouseMsg = 0;
                    if (pi.pointerFlags & POINTER_FLAG_DOWN) mouseMsg = WM_LBUTTONDOWN;
                    if (pi.pointerFlags & POINTER_FLAG_UPDATE) mouseMsg = WM_MOUSEMOVE;
                    if (pi.pointerFlags & POINTER_FLAG_UP) mouseMsg = WM_LBUTTONUP;
                    if (mouseMsg)
                    {
                        SendMessage(g_webViewHwnd, mouseMsg, 0, MAKELPARAM(pt.x, pt.y));
                    }
                }
            }
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
        return 1; // prevent flicker
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Entry point
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Initialize COM for WebView2 and pointer input
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
        0, L"MainWindowClass", L"Two-Layer Note-Taking",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_mainHwnd, nCmdShow);

    // Kick off WebView2
    InitializeWebView(g_mainHwnd);

    // Create overlay (hidden from Alt+Tab)
    g_overlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"OverlayWindowClass", nullptr,
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);
    // Make nearly transparent
    SetLayeredWindowAttributes(g_overlayHwnd, 0, 1, LWA_ALPHA);
    ShowWindow(g_overlayHwnd, nCmdShow);

    // Initial resize
    RECT rc;
    GetClientRect(g_mainHwnd, &rc);
    ResizeChildren(rc);

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
