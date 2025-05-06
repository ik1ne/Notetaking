#define UNICODE
#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <windows.ui.composition.interop.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <DispatcherQueue.h>
#include <windowsx.h>

using namespace Microsoft::WRL;
using namespace winrt;
using namespace winrt::Windows::UI::Composition;
using namespace winrt::Windows::UI::Composition::Desktop;


static wil::com_ptr<ICoreWebView2Controller> g_controller;
static wil::com_ptr<ICoreWebView2CompositionController> g_compController;
static wil::com_ptr<ICoreWebView2> g_webview;


// Constants
const wchar_t CLASS_NAME[] = L"WebView2CompositionWindow";

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
        if (g_compController)
        {
            POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            g_compController->SendMouseInput(
                static_cast<COREWEBVIEW2_MOUSE_EVENT_KIND>(msg),
                static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(wParam)),
                GET_WHEEL_DELTA_WPARAM(wParam), pt);
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void ShowErrorBox(const wchar_t* msg, HRESULT hr)
{
    wchar_t buffer[256];
    swprintf_s(buffer, L"%s (HRESULT 0x%08X)", msg, hr);
    MessageBoxW(nullptr, buffer, L"Error", MB_ICONERROR);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // Initialize COM

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    RegisterClass(&wc);

    // Create window
    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED, CLASS_NAME, L"WebView2 with Composition", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return -1;

    ShowWindow(hwnd, nCmdShow);

    // Create DispatcherQueue for Composition
    DispatcherQueueOptions options = {
        sizeof(DispatcherQueueOptions),
        DQTYPE_THREAD_CURRENT,
        DQTAT_COM_ASTA
    };
    winrt::Windows::System::DispatcherQueueController queueCtrl{nullptr};
    HRESULT hr = CreateDispatcherQueueController(
        options, reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(put_abi(queueCtrl)));
    if (FAILED(hr))
    {
        ShowErrorBox(L"CreateDispatcherQueueController failed", hr);
        return -1;
    }

    // Create Compositor and Composition Target
    Compositor compositor;
    auto interop = compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
    DesktopWindowTarget target{nullptr};
    hr = interop->CreateDesktopWindowTarget(hwnd, false,
                                            reinterpret_cast<
                                                ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(put_abi(
                                                target)));
    if (FAILED(hr))
    {
        ShowErrorBox(L"CreateDesktopWindowTarget failed", hr);
        return -1;
    }

    ContainerVisual root = compositor.CreateContainerVisual();
    root.RelativeSizeAdjustment({1.0f, 1.0f});
    root.Offset({0, 0, 0});
    target.Root(root);

    // Async WebView2 creation
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd, compositor, root](
            HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (FAILED(result) || !env)
                {
                    ShowErrorBox(L"WebView2 environment creation failed", result);
                    PostQuitMessage(-1);
                    return result;
                }

                wil::com_ptr<ICoreWebView2Environment3> env3;
                env->QueryInterface(IID_PPV_ARGS(&env3));

                env3->CreateCoreWebView2CompositionController(
                    hwnd,
                    Callback<
                        ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [hwnd, compositor, root](
                        HRESULT result,
                        ICoreWebView2CompositionController* controller) -> HRESULT
                        {
                            g_compController = controller;
                            EventRegistrationToken token;
                            g_compController->add_CursorChanged(
                                Callback<ICoreWebView2CursorChangedEventHandler>(
                                    [](ICoreWebView2CompositionController* sender,
                                       IUnknown*) -> HRESULT
                                    {
                                        HCURSOR cursor;
                                        sender->get_Cursor(&cursor);
                                        SetCursor(cursor);
                                        return S_OK;
                                    }).Get(),
                                &token);


                            if (FAILED(result) || !controller)
                            {
                                ShowErrorBox(
                                    L"Composition controller creation failed",
                                    result);
                                PostQuitMessage(-1);
                                return result;
                            }

                            // Query for base controller and webview
                            controller->QueryInterface(
                                IID_PPV_ARGS(&g_controller));

                            g_controller->get_CoreWebView2(&g_webview);

                            // Show, bind, navigate
                            g_controller->put_IsVisible(TRUE);

                            auto webviewVisual = compositor.
                                CreateContainerVisual();
                            root.Children().InsertAtTop(webviewVisual);
                            auto overlayVisual = compositor.CreateSpriteVisual();
                            overlayVisual.Brush(
                                compositor.CreateColorBrush(winrt::Windows::UI::Color{0x00, 0x00, 0x00, 0x00}));
                            // fully transparent
                            overlayVisual.RelativeSizeAdjustment({1.0f, 1.0f});
                            root.Children().InsertAtTop(overlayVisual); // place above webviewVisual

                            controller->put_RootVisualTarget(
                                webviewVisual.as<IUnknown>().get());

                            RECT bounds;
                            GetClientRect(hwnd, &bounds);
                            g_controller->put_Bounds(bounds);

                            g_webview->Navigate(L"https://www.bing.com");
                            PostMessage(hwnd, WM_SIZE, 0, 0);

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());

    // Main loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
