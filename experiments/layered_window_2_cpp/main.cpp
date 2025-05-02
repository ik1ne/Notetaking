#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <windowsx.h>

using namespace Microsoft::WRL;

#define ID_OVERLAY 1001

// Global handles and drawing state
static HWND g_mainHwnd = nullptr;
static HWND g_overlayHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_webViewController;
static bool g_isDrawing = false;
static POINT g_lastPoint = {};

// Resize overlay and WebView2 to match client area
void ResizeChildren(RECT const& rc)
{
    // Resize WebView2
    if (g_webViewController)
    {
        g_webViewController->put_Bounds(rc);
    }
    // Position overlay window over client area
    if (g_overlayHwnd && g_mainHwnd)
    {
        // Convert client coords to screen
        POINT pt = {rc.left, rc.top};
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
            return 0;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Overlay procedure: handles pen input and retains red circle
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_POINTERDOWN:
        {
            UINT32 pid = GET_POINTERID_WPARAM(wParam);
            POINTER_INPUT_TYPE type;
            if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
            {
                g_isDrawing = true;
                g_lastPoint.x = GET_X_LPARAM(lParam);
                g_lastPoint.y = GET_Y_LPARAM(lParam);
                SetCapture(hwnd);
            }
            return 0;
        }
    case WM_POINTERUPDATE:
        {
            if (g_isDrawing)
            {
                UINT32 pid = GET_POINTERID_WPARAM(wParam);
                POINTER_INPUT_TYPE type;
                if (SUCCEEDED(GetPointerType(pid, &type)) && type == PT_PEN)
                {
                    POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    HDC hdc = GetDC(hwnd);
                    MoveToEx(hdc, g_lastPoint.x, g_lastPoint.y, NULL);
                    LineTo(hdc, pt.x, pt.y);
                    ReleaseDC(hwnd, hdc);
                    g_lastPoint = pt;
                }
            }
            return 0;
        }
    case WM_POINTERUP:
        {
            if (g_isDrawing)
            {
                g_isDrawing = false;
                ReleaseCapture();
            }
            return 0;
        }
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            Ellipse(hdc, 50, 50, 150, 150);
            SelectObject(hdc, oldBrush);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_ERASEBKGND:
        return 1;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // Register window classes
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"OverlayWindowClass";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc2);

    // Create main window
    g_mainHwnd = CreateWindowExW(
        0,
        L"MainWindowClass",
        L"WebView2App - Main",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_mainHwnd, nCmdShow);

    // Create transparent overlay as topmost layered window
    g_overlayHwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST,
        L"OverlayWindowClass",
        nullptr,
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
            [](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(result) && env)
                {
                    env->CreateCoreWebView2Controller(
                        g_mainHwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [](HRESULT result2, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(result2) && controller)
                                {
                                    g_webViewController = controller;
                                    ComPtr<ICoreWebView2> webview;
                                    controller->get_CoreWebView2(&webview);
                                    RECT rc;
                                    GetClientRect(g_mainHwnd, &rc);
                                    ResizeChildren(rc);
                                    webview->Navigate(
                                        L"file:///C:/Users/ik1ne/Sources/Notetaking/experiments/layered_window_2_cpp/index.html");
                                }
                                return S_OK;
                            }
                        ).Get());
                }
                return S_OK;
            }
        ).Get());

    // Main message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
