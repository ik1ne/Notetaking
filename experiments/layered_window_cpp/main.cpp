#define UNICODE
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>

#pragma comment(lib, "WebView2Loader.lib")

using namespace Microsoft::WRL;

// Global pointers
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        {
            if (g_controller)
            {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                g_controller->put_Bounds(bounds);
            }
            break;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register main window
    const wchar_t MAIN_CLS[] = L"WebView2Main";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MAIN_CLS;
    RegisterClassW(&wc);

    // Register overlay window class
    const wchar_t OVER_CLS[] = L"Overlay";
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = DefWindowProcW;
    wc2.hInstance = hInst;
    wc2.lpszClassName = OVER_CLS;
    wc2.hbrBackground = CreateSolidBrush(RGB(0, 0, 0)); // black background
    RegisterClassW(&wc2);

    // Create main window
    HWND hwnd = CreateWindowExW(
        0, MAIN_CLS, L"WebView2 with Half Overlay",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmdShow);

    // Create semi-transparent half-size overlay
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    HWND overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE,
        OVER_CLS, nullptr,
        WS_POPUP | WS_VISIBLE,
        rc.left + w / 4, rc.top + h / 4, // centered quarter offset
        w / 2, h / 2,
        hwnd, nullptr, hInst, nullptr);
    // 50% opacity black overlay
    SetLayeredWindowAttributes(overlay, 0, 128, LWA_ALPHA);
    SetWindowPos(overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // HTML content
    const wchar_t* html = LR"(
        <!DOCTYPE html>
        <html><body>
          <h1>Hello World</h1>
          <p>Overlay covers center half.</p>
        </body></html>
    )";

    // Initialize WebView2
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd, html](HRESULT envRes, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(envRes) && env)
                {
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd, html](HRESULT ctrlRes, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(ctrlRes) && controller)
                                {
                                    g_controller = controller;
                                    controller->get_CoreWebView2(&g_webView);
                                    controller->put_IsVisible(TRUE);
                                    RECT b;
                                    GetClientRect(hwnd, &b);
                                    controller->put_Bounds(b);
                                    g_webView->NavigateToString(html);
                                }
                                return S_OK;
                            }).Get());
                }
                return S_OK;
            }).Get());

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    CoUninitialize();
    return 0;
}
