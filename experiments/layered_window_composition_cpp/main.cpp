#define UNICODE
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <thread>

using namespace Microsoft::WRL;

// Globals
static ComPtr<ICoreWebView2Environment> g_env;
static ComPtr<ICoreWebView2CompositionController> g_compositionController;
static ComPtr<ICoreWebView2> g_webview;

// Custom message for pointer injection
constexpr UINT WM_INJECT_POINTER = WM_APP + 1;

// Window procedure: handles our injection message by calling SendPointerInput
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_INJECT_POINTER)
    {
        // lp is the raw ICoreWebView2PointerInfo*
        auto* pInfo = reinterpret_cast<ICoreWebView2PointerInfo*>(lp);
        if (g_compositionController && pInfo)
        {
            // Inject as a WM_POINTERUPDATE equivalent
            g_compositionController->SendPointerInput(
                COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE,
                pInfo);
        }
        pInfo->Release(); // balance the AddRef in the worker
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// Worker thread: creates a pointer-info, AddRef’s it, and posts it to the UI thread
void InputWorker(DWORD uiThreadId)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 1) QI for Environment3
    ComPtr<ICoreWebView2Environment3> env3;
    HRESULT hrEnv3 = g_env.As(&env3);
    if (FAILED(hrEnv3) || !env3)
    {
        // Environment3 (pointer injection) not supported by this runtime
        OutputDebugStringW(L"[InputWorker] ICoreWebView2Environment3 not available\n");
        CoUninitialize();
        return;
    }

    // 2) Create the pointer‐info correctly
    ComPtr<ICoreWebView2PointerInfo> pointerInfo;
    HRESULT hrPtr = env3->CreateCoreWebView2PointerInfo(pointerInfo.GetAddressOf());
    if (FAILED(hrPtr) || !pointerInfo)
    {
        // failed to create pointer info
        OutputDebugStringW(L"[InputWorker] CreateCoreWebView2PointerInfo failed\n");
        CoUninitialize();
        return;
    }

    // 3) Populate it
    pointerInfo->put_PointerKind(PT_PEN);
    POINT pt{150, 100};
    pointerInfo->put_PixelLocation(pt);
    pointerInfo->put_PenPressure(512);

    // 4) Marshal it into the UI thread
    pointerInfo->AddRef();
    PostThreadMessage(uiThreadId,
                      WM_INJECT_POINTER,
                      0,
                      reinterpret_cast<LPARAM>(pointerInfo.Get()));

    CoUninitialize();
}


// Entry point: sets up COM, window, WebView2 CompositionController, then spins a worker
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // 1) Register window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"WebView2Compose";
    RegisterClass(&wc);

    // 2) Create window
    HWND hwnd = CreateWindow(
        wc.lpszClassName, L"ComposeExample",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
        800, 600, nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, SW_SHOW);

    // 3) Initialize WebView2 environment
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT, ICoreWebView2Environment* envRaw) -> HRESULT
            {
                g_env = envRaw;

                // 4) Query for ICoreWebView2Environment3
                ComPtr<ICoreWebView2Environment3> env3;
                g_env.As(&env3);

                // 5) Create the CompositionController
                env3->CreateCoreWebView2CompositionController(
                    hwnd,
                    Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                        [hwnd](HRESULT, ICoreWebView2CompositionController* ctrlRaw) -> HRESULT
                        {
                            g_compositionController = ctrlRaw;
                            // — Build your DirectComposition/WinUI visuals here,
                            //   then call:
                            //   g_compositionController->put_RootVisualTarget(dcompVisual.Get());
                            //   dcompDevice->Commit();

                            return S_OK;
                        }).Get());

                return S_OK;
            }).Get());

    // 6) Launch the input worker
    DWORD uiThreadId = GetCurrentThreadId();
    std::thread(InputWorker, uiThreadId).detach();

    // 7) Run the message loop
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    CoUninitialize();
    return 0;
}
