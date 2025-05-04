#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <mutex>
#include <vector>

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "WebView2Loader.lib")

using namespace Microsoft::WRL;

static HWND g_mainHwnd = nullptr;
static HWND g_midHwnd = nullptr;
static HWND g_topHwnd = nullptr;
static ComPtr<ICoreWebView2Controller> g_controller;
static ComPtr<ICoreWebView2> g_webview;
static std::vector<POINT> g_strokePoints;
static std::mutex g_mutex;


LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void InitializeWebView(HWND hwnd);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    EnableMouseInPointer(TRUE);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register main window
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    g_mainHwnd = CreateWindowExW(
        0, L"MainWindowClass", L"Two-Layer Note-Taking",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        1024, 768,
        nullptr, nullptr, hInstance, nullptr);
    ShowWindow(g_mainHwnd, nCmdShow);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}


LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        InitializeWebView(hwnd);
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}


// Create and size WebView2 inside main window
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
                                        L"file:///C:/Users/ik1ne/Sources/Notetaking/experiments/layered_window_manual_cpp/index.html"
                                    );
                                }
                                return S_OK;
                            }).Get());
                }
                return S_OK;
            }).Get());
}
