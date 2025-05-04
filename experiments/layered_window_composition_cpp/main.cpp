#define UNICODE
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <dcomp.h>
#include <thread>

#pragma comment(lib, "dcomp.lib")

using namespace Microsoft::WRL;

// Globals
static ComPtr<ICoreWebView2Environment> g_env;
static ComPtr<ICoreWebView2CompositionController> g_compositionController;
static ComPtr<ICoreWebView2> g_webview;

// Custom message for pointer injection
constexpr UINT WM_INJECT_POINTER = WM_APP + 1;

// Window procedure: handles injection message by calling SendPointerInput
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INJECT_POINTER)
    {
        auto* pInfo = reinterpret_cast<ICoreWebView2PointerInfo*>(lp);
        if (g_compositionController && pInfo)
        {
            g_compositionController->SendPointerInput(
                COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE,
                pInfo);
        }
        pInfo->Release();
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Worker thread: creates pointer-info and posts to UI thread
void InputWorker(DWORD uiThreadId)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    ComPtr<ICoreWebView2Environment3> env3;
    if (SUCCEEDED(g_env.As(&env3)))
    {
        ComPtr<ICoreWebView2PointerInfo> pointerInfo;
        if (SUCCEEDED(env3->CreateCoreWebView2PointerInfo(pointerInfo.GetAddressOf())) && pointerInfo)
        {
            pointerInfo->put_PointerKind(PT_PEN);
            POINT pt{150, 100};
            pointerInfo->put_PixelLocation(pt);
            pointerInfo->put_PenPressure(512);

            pointerInfo->AddRef();
            PostThreadMessage(uiThreadId, WM_INJECT_POINTER, 0,
                              reinterpret_cast<LPARAM>(pointerInfo.Get()));
        }
    }

    CoUninitialize();
}

// Entry point: initialize COM, window, WebView2 env & composition controller,
// start worker, run message loop
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WebView2Compose";
    RegisterClass(&wc);

    // Create window
    HWND hwnd = CreateWindow(wc.lpszClassName, L"ComposeExample",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
                             nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // Initialize WebView2 environment
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT, ICoreWebView2Environment* envRaw) -> HRESULT
            {
                g_env = envRaw;

                // Query for Environment3
                ComPtr<ICoreWebView2Environment3> env3;
                if (FAILED(g_env.As(&env3)))
                {
                    OutputDebugStringW(L"[ComposeExample] Environment3 unavailable\n");
                    return S_OK;
                }

                // Create CompositionController
                env3->CreateCoreWebView2CompositionController(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [hwnd](HRESULT, ICoreWebView2CompositionController* ctrlRaw) -> HRESULT
                        {
                            g_compositionController = ctrlRaw;

                            // QI to ICoreWebView2Controller
                            ComPtr<ICoreWebView2Controller> controller;
                            if (FAILED(ctrlRaw->QueryInterface(IID_PPV_ARGS(&controller))))
                            {
                                OutputDebugStringW(L"[ComposeExample] QI for Controller failed\n");
                                return S_OK;
                            }

                            // Set bounds to fill the window client area
                            RECT bounds;
                            GetClientRect(hwnd, &bounds);
                            controller->put_Bounds(bounds);
                            controller->put_IsVisible(TRUE);

                            // Obtain ICoreWebView2 and navigate
                            if (SUCCEEDED(controller->get_CoreWebView2(&g_webview)) && g_webview)
                            {
                                // Optionally, add a NavigationCompleted handler
                                g_webview->add_NavigationCompleted(
                                    Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                        [](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                                        {
                                            BOOL success;
                                            args->get_IsSuccess(&success);
                                            OutputDebugStringW(
                                                success ? L"Navigation Succeeded\n" : L"Navigation Failed\n");
                                            return S_OK;
                                        }).Get(), nullptr);

                                g_webview->Navigate(
                                    L"file:///C:/Users/ik1ne/Sources/Notetaking/"
                                    L"experiments/layered_window_composition_cpp/index.html");
                            }

                            // Build and attach composition tree
                            ComPtr<IDCompositionDevice> dcompDevice;
                            DCompositionCreateDevice(nullptr, IID_PPV_ARGS(&dcompDevice));
                            ComPtr<IDCompositionTarget> dcompTarget;
                            dcompDevice->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget);
                            ComPtr<IDCompositionVisual> dcompVisual;
                            dcompDevice->CreateVisual(&dcompVisual);

                            g_compositionController->put_RootVisualTarget(dcompVisual.Get());
                            dcompTarget->SetRoot(dcompVisual.Get());
                            dcompDevice->Commit();

                            return S_OK;
                        }).Get());
                return S_OK;
            }).Get());

    // Launch worker thread
    DWORD uiThreadId = GetCurrentThreadId();
    std::thread(InputWorker, uiThreadId).detach();

    // Message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
