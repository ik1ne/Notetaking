#include <windows.h>

const char* MAIN_CLASS = "MainClass";
const char* MID_CLASS = "MidClass";
const char* TOP_CLASS = "TopClass";

static HWND g_midHwnd = nullptr;
static HWND g_topHwnd = nullptr;

// — Main window proc: create two transparent children on WM_CREATE
LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_CREATE:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int w = rc.right, h = rc.bottom;

            // mid: right 2/3
            g_midHwnd = CreateWindowEx(
                0, MID_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE,
                w / 3, 0, (w * 2) / 3, h,
                hwnd, nullptr,
                ((LPCREATESTRUCT)lp)->hInstance,
                nullptr
            );

            // top: left 2/3
            g_topHwnd = CreateWindowEx(
                0, TOP_CLASS, nullptr,
                WS_CHILD | WS_VISIBLE,
                0, 0, (w * 2) / 3, h,
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

// — Mid layer proc: fully transparent, hit-test only on painted pixels
LRESULT CALLBACK MidProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; // suppress background erase → visual transparency
    case WM_PAINT:
        BeginPaint(hwnd, nullptr);
        EndPaint(hwnd, nullptr);
        return 0;
    case WM_NCHITTEST:
        // no pixels drawn yet → treat entire window as transparent to input
        return HTTRANSPARENT;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

// — Top layer proc: fully transparent, always click-through
LRESULT CALLBACK TopProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1; // suppress background erase → visual transparency
    case WM_PAINT:
        BeginPaint(hwnd, nullptr);
        EndPaint(hwnd, nullptr);
        return 0;
    case WM_NCHITTEST:
        // no pixels drawn yet → treat entire window as transparent to input
        return HTTRANSPARENT;
    }
    return DefWindowProc(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow)
{
    WNDCLASS wc = {};
    wc.hInstance = hInst;
    wc.lpfnWndProc = MainProc;
    wc.lpszClassName = MAIN_CLASS;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    wc.lpfnWndProc = MidProc;
    wc.lpszClassName = MID_CLASS;
    wc.hbrBackground = nullptr;
    RegisterClass(&wc);

    wc.lpfnWndProc = TopProc;
    wc.lpszClassName = TOP_CLASS;
    wc.hbrBackground = nullptr;
    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, MAIN_CLASS, "Transparent Layers",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        nullptr, nullptr, hInst, nullptr
    );
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
