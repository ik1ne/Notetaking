#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

using namespace Microsoft::WRL;

#define ID_OVERLAY 1001

// Main window procedure
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// Overlay window procedure: draws a red circle
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
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
        return 1; // prevent flicker
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow)
{
    // Register main window class
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    // Register overlay window class
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = L"OverlayWindowClass";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassW(&wc2);

    // Create main window
    HWND mainHwnd = CreateWindowExW(
        0,
        L"MainWindowClass",
        L"WebView2App - Main",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600,
        nullptr, nullptr, hInstance, nullptr);

    // Create transparent overlay child
    HWND overlayHwnd = CreateWindowExW(
        WS_EX_TRANSPARENT,
        L"OverlayWindowClass",
        nullptr,
        WS_CHILD | WS_VISIBLE,
        0, 0, 800, 600,
        mainHwnd,
        (HMENU)ID_OVERLAY,
        hInstance,
        nullptr);

    ShowWindow(mainHwnd, nCmdShow);

    // Initialize WebView2
    ComPtr<ICoreWebView2Environment> webViewEnvironment;
    ComPtr<ICoreWebView2Controller> webViewController;
    ComPtr<ICoreWebView2> webView;

    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [mainHwnd, &webViewController, &webView](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(result) && env)
                {
                    env->CreateCoreWebView2Controller(
                        mainHwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [mainHwnd, &webViewController, &webView](HRESULT result2,
                                                                     ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(result2) && controller)
                                {
                                    webViewController = controller;
                                    controller->get_CoreWebView2(&webView);
                                    RECT bounds;
                                    GetClientRect(mainHwnd, &bounds);
                                    webViewController->put_Bounds(bounds);
                                    webView->Navigate(
                                        L"C:\\Users\\ik1ne\\Sources\\Notetaking\\experiments\\layered_window_2_cpp\\index.html");
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
