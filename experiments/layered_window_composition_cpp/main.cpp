#include <windows.h>
#include <wrl.h>
#include <wil/com.h>
#include <dcomp.h>
#include <webview2.h>
#pragma comment(lib, "dcomp")
#pragma comment(lib, "WebView2Loader")

using namespace Microsoft::WRL;

// Global pointer to the WebView2 controller
static wil::com_ptr<ICoreWebView2Controller> webViewController;

// Window procedure to handle resizing
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (webViewController)
        {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            webViewController->put_Bounds(bounds);
            webViewController->NotifyParentWindowPositionChanged();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED); // Initialize COM

    // Register a basic window class
    WNDCLASSEXW wc = {
        sizeof(WNDCLASSEXW), CS_HREDRAW | CS_VREDRAW, WndProc, 0, 0,
        hInstance, NULL, NULL, NULL, NULL,
        L"WebView2CompositionExample", NULL
    };
    RegisterClassExW(&wc);

    // Create the main window
    HWND hwnd = CreateWindowW(
        wc.lpszClassName, L"WebView2 Composition Example",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Create the WebView2 environment and composition controller
    CreateCoreWebView2EnvironmentWithOptions(
        nullptr, nullptr, nullptr,
        Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
            [hwnd](HRESULT result, ICoreWebView2Environment* env) -> HRESULT
            {
                if (SUCCEEDED(result) && env)
                {
                    wil::com_ptr<ICoreWebView2Environment3> env3;
                    if (SUCCEEDED(env->QueryInterface(IID_PPV_ARGS(&env3))) && env3)
                    {
                        // Create the composition controller asynchronously
                        env3->CreateCoreWebView2CompositionController(
                            hwnd,
                            Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
                                [hwnd](HRESULT compResult,
                                       ICoreWebView2CompositionController* compController) -> HRESULT
                                {
                                    if (SUCCEEDED(compResult) && compController)
                                    {
                                        // Convert to the basic controller to navigate and size
                                        compController->QueryInterface(IID_PPV_ARGS(&webViewController));

                                        // Create DirectComposition visuals
                                        wil::com_ptr<IDCompositionDevice> dcompDevice;
                                        DCompositionCreateDevice3(nullptr, __uuidof(IDCompositionDevice),
                                                                  (void**)dcompDevice.put());
                                        wil::com_ptr<IDCompositionTarget> dcompTarget;
                                        dcompDevice->CreateTargetForHwnd(hwnd, TRUE, dcompTarget.put());
                                        wil::com_ptr<IDCompositionVisual> dcompRoot;
                                        dcompDevice->CreateVisual(dcompRoot.put());
                                        dcompTarget->SetRoot(dcompRoot.get());
                                        wil::com_ptr<IDCompositionVisual> dcompWebViewVisual;
                                        dcompDevice->CreateVisual(dcompWebViewVisual.put());
                                        dcompRoot->AddVisual(dcompWebViewVisual.get(), TRUE, nullptr);

                                        // Attach the WebView to the composition visual and commit
                                        compController->put_RootVisualTarget(dcompWebViewVisual.get());
                                        auto hr2 = dcompDevice->Commit();
                                        if (FAILED(hr2))
                                        {
                                            OutputDebugStringW(L"[ERROR] Commit failed\n");
                                        }

                                        wil::com_ptr<IDCompositionVisual> attachedVisual;
                                        HRESULT hr = compController->get_RootVisualTarget(
                                            reinterpret_cast<IUnknown**>(attachedVisual.put())
                                        );
                                        if (FAILED(hr) || !attachedVisual)
                                        {
                                            OutputDebugStringW(L"[ERROR] RootVisualTarget is NULL or get failed\n");
                                        }
                                        else if (attachedVisual.get() != dcompWebViewVisual.get())
                                        {
                                            OutputDebugStringW(L"[WARN] Attached visual ≠ your webview visual\n");
                                        }
                                        else
                                        {
                                            OutputDebugStringW(
                                                L"[OK] Composition controller has your WebView visual\n");
                                        }

                                        // Navigate to Bing
                                        wil::com_ptr<ICoreWebView2> coreWebView;
                                        webViewController->get_CoreWebView2(&coreWebView);
                                        EventRegistrationToken navToken;
                                        coreWebView->add_NavigationCompleted(
                                            Callback<ICoreWebView2NavigationCompletedEventHandler>(
                                                [](ICoreWebView2* /*sender*/,
                                                   ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT
                                                {
                                                    BOOL isSuccess = FALSE;
                                                    args->get_IsSuccess(&isSuccess);
                                                    if (!isSuccess)
                                                    {
                                                        COREWEBVIEW2_WEB_ERROR_STATUS status;
                                                        args->get_WebErrorStatus(&status);
                                                        wchar_t buf[128];
                                                        swprintf_s(buf, L"Navigation failed: error %d\n", (int)status);
                                                        OutputDebugStringW(buf);
                                                    }
                                                    else
                                                    {
                                                        OutputDebugStringW(L"NavigationCompleted: success\n");
                                                    }
                                                    return S_OK;
                                                }).Get(), &navToken);
                                        coreWebView->Navigate(L"https://www.bing.com");

                                        // Resize the WebView to fill the window
                                        RECT bounds;
                                        GetClientRect(hwnd, &bounds);
                                        webViewController->put_Bounds(bounds);
                                        webViewController->put_IsVisible(TRUE);
                                        webViewController->NotifyParentWindowPositionChanged();
                                    }
                                    return S_OK;
                                }).Get());
                    }
                }
                return S_OK;
            }).Get());

    // Standard message loop
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return 0;
}
