#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <iostream>
#include <WebView2.h>

using namespace Microsoft::WRL;

HWND g_mainWindow = nullptr;
ICoreWebView2Controller* g_webViewController = nullptr;
ICoreWebView2* g_webView = nullptr;

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// COM handler for controller creation
class WebViewControllerCompletedHandler :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>, ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>
{
public:
    STDMETHOD(Invoke)(HRESULT result, ICoreWebView2Controller* controller)
    {
        if (FAILED(result) || !controller)
        {
            MessageBoxW(nullptr, L"Failed to create WebView2 controller", L"Error", MB_ICONERROR);
            return E_FAIL;
        }

        g_webViewController = controller;
        g_webViewController->get_CoreWebView2(&g_webView);

        // Resize to fit window
        RECT bounds;
        GetClientRect(g_mainWindow, &bounds);
        g_webViewController->put_Bounds(bounds);

        // Load HTML content
        g_webView->Navigate(L"data:text/html,<html><body><h1>Hello World</h1></body></html>");
        return S_OK;
    }
};

// COM handler for environment creation
class WebViewEnvironmentCompletedHandler :
    public RuntimeClass<RuntimeClassFlags<ClassicCom>, ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>
{
public:
    STDMETHOD(Invoke)(HRESULT result, ICoreWebView2Environment* env)
    {
        if (FAILED(result) || !env)
        {
            MessageBoxW(nullptr, L"Failed to create WebView2 environment", L"Error", MB_ICONERROR);
            return E_FAIL;
        }
        env->CreateCoreWebView2Controller(g_mainWindow, Make<WebViewControllerCompletedHandler>().Get());
        return S_OK;
    }
};

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // Register window class
    const wchar_t CLASS_NAME[] = L"WebView2WindowClass";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClassW(&wc);

    // Create main window
    g_mainWindow = CreateWindowExW(
        0, CLASS_NAME, L"WebView2 Stylus Filter",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr
    );

    if (!g_mainWindow)
        return 1;

    ShowWindow(g_mainWindow, nCmdShow);

    // Initialize WebView2
    HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr, Make<WebViewEnvironmentCompletedHandler>().Get());

    if (FAILED(hr))
    {
        MessageBoxW(nullptr, L"WebView2 environment creation failed", L"Error", MB_ICONERROR);
        return 1;
    }

    // Main message loop
    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_POINTERDOWN:
    case WM_POINTERUP:
    case WM_POINTERUPDATE:
        {
            UINT32 pointerId = GET_POINTERID_WPARAM(wParam);
            POINTER_INPUT_TYPE type;
            if (GetPointerType(pointerId, &type))
            {
                if (type == PT_PEN)
                {
                    std::wcout << L"Stylus input detected\n";
                }
                else if (type == PT_MOUSE)
                {
                    std::wcout << L"Mouse input\n";
                }
                else if (type == PT_TOUCH)
                {
                    std::wcout << L"Touch input\n";
                }
                else
                {
                    std::wcout << L"Other pointer input\n";
                }
            }
            break;
        }
    case WM_SIZE:
        {
            if (g_webViewController)
            {
                RECT bounds;
                GetClientRect(hwnd, &bounds);
                g_webViewController->put_Bounds(bounds);
            }
            break;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
