#include <windows.h>

const char* MAIN_CLASS = "MainClass";
const char* MID_CLASS = "MidClass";
const char* TOP_CLASS = "TopClass";

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right;
            int h = rc.bottom;

            // Mid layer: right 2/3
            CreateWindowEx(
                0,
                MID_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE,
                w / 3, 0, // x = one-third in
                (w * 2) / 3, h, // width = two-thirds
                hwnd, nullptr,
                ((LPCREATESTRUCT)lp)->hInstance,
                nullptr
            );

            // Top layer: left 2/3
            CreateWindowEx(
                0,
                TOP_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, // x = 0
                (w * 2) / 3, h, // width = two-thirds
                hwnd, nullptr,
                ((LPCREATESTRUCT)lp)->hInstance,
                nullptr
            );
            return 0;
        }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

LRESULT CALLBACK LayerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH brush = (HBRUSH)GetClassLongPtr(hwnd, GCLP_HBRBACKGROUND);
        FillRect(hdc, &ps.rcPaint, brush);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    // Register main window (white background)
    WNDCLASS wc = {};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hInst;
    wc.lpszClassName = MAIN_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    // Register mid layer (dark gray)
    wc.lpfnWndProc = LayerProc;
    wc.lpszClassName = MID_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    RegisterClass(&wc);

    // Register top layer (light gray)
    wc.lpszClassName = TOP_CLASS;
    wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, MAIN_CLASS, "Three-Layer Debug Example",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInst, nullptr
    );
    if (!hwnd) return 0;

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
