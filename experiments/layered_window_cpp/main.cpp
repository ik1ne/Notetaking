#define UNICODE
#include <windows.h>
#include <windowsx.h>
#include <wrl.h>
#include <WebView2.h>
#include <iostream>

#pragma comment(lib, "WebView2Loader.lib")

using namespace Microsoft::WRL;

// Globals
ComPtr<ICoreWebView2Controller> g_controller;
ComPtr<ICoreWebView2> g_webView;
ComPtr<ICoreWebView2Environment3> g_env3;
ComPtr<ICoreWebView2CompositionController> g_compController;
HWND g_overlay = nullptr;

// Forward declarations
LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK OverlayWndProc(HWND, UINT, WPARAM, LPARAM);

// Minimal HTML content
static const wchar_t* kHtml = LR"(
<!DOCTYPE html>
<html><body>
  <h1>Hello World</h1>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
  <p>Overlay covers center half; pointer events logged.</p>
</body></html>
)";

// Main window procedure: resize WebView and overlay
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        {
            if (g_controller)
            {
                RECT rc;
                GetClientRect(hwnd, &rc);
                g_controller->put_Bounds(rc);
            }
            // reposition overlay
            RECT mrc;
            GetClientRect(hwnd, &mrc);
            POINT p = {mrc.left, mrc.top};
            ClientToScreen(hwnd, &p);
            int w = mrc.right - mrc.left;
            int h = mrc.bottom - mrc.top;
            MoveWindow(
                g_overlay,
                p.x + w / 4, p.y + h / 4,
                w / 2, h / 2,
                TRUE
            );
            break;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Overlay window procedure: paint black, log + forward pointer events
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            HBRUSH brush = (HBRUSH)GetStockObject(BLACK_BRUSH);
            FillRect(dc, &rc, brush);
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_POINTERDOWN:
    case WM_POINTERUPDATE:
    case WM_POINTERUP:
        {
            UINT32 pid = GET_POINTERID_WPARAM(wp);
            POINTER_INPUT_TYPE type;
            if (GetPointerType(pid, &type))
            {
                const wchar_t* kind = L"Unknown";
                if (type == PT_PEN) kind = L"Stylus";
                else if (type == PT_MOUSE) kind = L"Mouse";
                else if (type == PT_TOUCH) kind = L"Touch";
                const wchar_t* phase =
                    (msg == WM_POINTERDOWN) ? L"Down" : (msg == WM_POINTERUP) ? L"Up" : L"Move";
                std::wcout << L"[" << kind << L"] " << phase << L" event" << std::endl;
                std::wcout.flush();

                if (type != PT_PEN && g_env3 && g_compController)
                {
                    ComPtr<ICoreWebView2PointerInfo> info;
                    if (SUCCEEDED(g_env3->CreateCoreWebView2PointerInfo(&info)))
                    {
                        POINTER_INFO pi;
                        if (GetPointerInfo(pid, &pi))
                        {
                            info->put_PointerKind(type);
                            info->put_PointerId(pid);
                            info->put_PointerFlags(pi.pointerFlags);
                            info->put_PixelLocation(pi.ptPixelLocation);
                            g_compController->SendPointerInput(
                                (COREWEBVIEW2_POINTER_EVENT_KIND)msg,
                                info.Get()
                            );
                        }
                    }
                }
            }
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    EnableMouseInPointer(TRUE);

    // Register main window
    const wchar_t MAIN_CLS[] = L"MainWebWnd";
    WNDCLASSW wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MAIN_CLS;
    RegisterClassW(&wc);

    // Register overlay window (black background)
    const wchar_t OVER_CLS[] = L"HalfOverlay";
    WNDCLASSW wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInst;
    wc2.lpszClassName = OVER_CLS;
    wc2.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassW(&wc2);

    // Create main window
    HWND hwnd = CreateWindowExW(
        0, MAIN_CLS, L"WebView2 + Half Overlay w/ Injection",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nCmdShow);

    // Create semi-transparent overlay covering center half
    RECT rc;
    GetClientRect(hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    POINT tl = {rc.left + w / 4, rc.top + h / 4};
    ClientToScreen(hwnd, &tl);
    g_overlay = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_NOACTIVATE,
        OVER_CLS, nullptr,
        WS_POPUP | WS_VISIBLE,
        tl.x, tl.y,
        w / 2, h / 2,
        hwnd, nullptr, hInst, nullptr);
    SetLayeredWindowAttributes(g_overlay, 0, 192, LWA_ALPHA);
    SetWindowPos(g_overlay, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    RegisterTouchWindow(g_overlay, 0);

    // Initialize WebView2 and load HTML
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT envRes, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(envRes) && env)
                {
                    env->QueryInterface(IID_PPV_ARGS(&g_env3));
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd](HRESULT ctrlRes, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                if (SUCCEEDED(ctrlRes) && controller)
                                {
                                    controller->QueryInterface(IID_PPV_ARGS(&g_compController));
                                    g_controller = controller;
                                    controller->get_CoreWebView2(&g_webView);
                                    controller->put_IsVisible(TRUE);
                                    RECT b;
                                    GetClientRect(hwnd, &b);
                                    controller->put_Bounds(b);
                                    g_webView->NavigateToString(kHtml);
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
