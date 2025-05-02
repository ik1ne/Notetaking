#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <windowsx.h>
#include <debugapi.h>  // for OutputDebugStringA

using namespace Microsoft::WRL;

// IDs
#define ID_OVERLAY 1001

// Globals
static HWND g_mainHwnd = nullptr;
static HWND g_overlayHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_webViewController;
static ComPtr<ICoreWebView2CompositionController> g_compController;
static ComPtr<ICoreWebView2Environment3> g_env3;
static bool g_isDrawing = false;
static POINT g_lastPoint = {};

// Forward declaration
void ResizeChildren(RECT const& rc);

//–– Resize WebView2 & overlay to client area ––
void ResizeChildren(RECT const& rc)
{
    if (g_webViewController)
    {
        g_webViewController->put_Bounds(rc);
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

//–– Main window proc (resize children, quit) ––
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

//–– Overlay proc: debug‐draw & inject pen input into WebView2 ––
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        {
            // guard COM pointers
            if (!g_env3)
            {
                OutputDebugStringA("ERROR: g_env3 is null!\n");
                return 0;
            }
            if (!g_compController)
            {
                OutputDebugStringA("ERROR: g_compController is null!\n");
                return 0;
            }

            UINT32 pid = GET_POINTERID_WPARAM(wParam);
            POINTER_INPUT_TYPE type;
            if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
            {
                // debug stroke
                if (msg == WM_POINTERDOWN)
                {
                    g_isDrawing = true;
                    g_lastPoint.x = GET_X_LPARAM(lParam);
                    g_lastPoint.y = GET_Y_LPARAM(lParam);
                    SetCapture(hwnd);
                }
                else if (msg == WM_POINTERUPDATE && g_isDrawing)
                {
                    POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    HDC hdc = GetDC(hwnd);
                    MoveToEx(hdc, g_lastPoint.x, g_lastPoint.y, nullptr);
                    LineTo(hdc, pt.x, pt.y);
                    ReleaseDC(hwnd, hdc);
                    g_lastPoint = pt;
                }
                else if (msg == WM_POINTERUP && g_isDrawing)
                {
                    g_isDrawing = false;
                    ReleaseCapture();
                }

                // build & send WebView2 pointer event
                ComPtr<ICoreWebView2PointerInfo> info;
                HRESULT hr = g_env3->CreateCoreWebView2PointerInfo(&info);
                if (FAILED(hr) || !info)
                {
                    OutputDebugStringA("ERROR: CreateCoreWebView2PointerInfo failed\n");
                    return 0;
                }

                info->put_PointerKind(PT_PEN);
                info->put_PointerId(pid);
                info->put_PixelLocation({GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)});

                UINT32 flags =
                    (msg == WM_POINTERDOWN
                         ? POINTER_FLAG_DOWN
                         : msg == WM_POINTERUP
                         ? POINTER_FLAG_UP
                         : POINTER_FLAG_UPDATE)
                    | POINTER_FLAG_INRANGE
                    | (msg != WM_POINTERUP ? POINTER_FLAG_INCONTACT : 0);
                info->put_PointerFlags(flags);
                info->put_Time(GetMessageTime());

                COREWEBVIEW2_POINTER_EVENT_KIND kind =
                (msg == WM_POINTERDOWN
                     ? COREWEBVIEW2_POINTER_EVENT_KIND_DOWN
                     : msg == WM_POINTERUPDATE
                     ? COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE
                     : COREWEBVIEW2_POINTER_EVENT_KIND_UP);

                g_compController->SendPointerInput(kind, info.Get());
            }

            return 0;
        }

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
            HBRUSH old = (HBRUSH)SelectObject(hdc, brush);
            Ellipse(hdc, 50, 50, 150, 150);
            SelectObject(hdc, old);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            return 0;
        }

    case WM_ERASEBKGND:
        return 1; // prevent flicker
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

//–– Entry point: register windows, create overlay & WebView2 ––
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // Main window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // Overlay window class
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"OverlayWindowClass";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc2);

    // Create main window
    g_mainHwnd = CreateWindowExW(
        0, L"MainWindowClass", L"WebView2App - Main",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_mainHwnd, nCmdShow);

    // Create overlay (must NOT use WS_EX_TRANSPARENT)
    g_overlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST,
        L"OverlayWindowClass", nullptr,
        WS_POPUP,
        0, 0, 0, 0,
        nullptr, nullptr, hInstance, nullptr);
    SetLayeredWindowAttributes(g_overlayHwnd, 0, 255, LWA_ALPHA);
    ShowWindow(g_overlayHwnd, nCmdShow);

    EnableMouseInPointer(TRUE);

    // Initialize WebView2
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [&](HRESULT envRes, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(envRes) && env)
                {
                    // QI for ICoreWebView2Environment3
                    HRESULT hrEnv3 = env->QueryInterface(IID_PPV_ARGS(&g_env3));
                    if (FAILED(hrEnv3) || !g_env3)
                    {
                        OutputDebugStringA("ERROR: QI for Environment3 failed\n");
                    }

                    env->CreateCoreWebView2Controller(
                        g_mainHwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [&](HRESULT ctlRes, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(ctlRes) && controller)
                                {
                                    g_webViewController = controller;
                                    // QI for CompositionController
                                    HRESULT hrComp = controller->QueryInterface(IID_PPV_ARGS(&g_compController));
                                    if (FAILED(hrComp) || !g_compController)
                                    {
                                        OutputDebugStringA("ERROR: QI for CompositionController failed\n");
                                    }

                                    ComPtr<ICoreWebView2> webview;
                                    controller->get_CoreWebView2(&webview);

                                    RECT rc;
                                    GetClientRect(g_mainHwnd, &rc);
                                    ResizeChildren(rc);

                                    webview->Navigate(
                                        L"file:///C:/Users/ik1ne/Sources/Notetaking/experiments/layered_window_2_cpp/index.html");
                                }
                                else
                                {
                                    OutputDebugStringA("ERROR: CreateCoreWebView2Controller failed\n");
                                }
                                return S_OK;
                            }).Get());
                }
                else
                {
                    OutputDebugStringA("ERROR: CreateCoreWebView2Environment failed\n");
                }
                return S_OK;
            }).Get());

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
