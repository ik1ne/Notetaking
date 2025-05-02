#include <windows.h>

#define ID_OVERLAY 1001

// Main window procedure
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Overlay window procedure: draws a red circle
LRESULT CALLBACK OverlayWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            HBRUSH brush = CreateSolidBrush(RGB(255, 0, 0));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            Ellipse(hdc, 50, 50, 150, 150);
            SelectObject(hdc, oldBrush);
            DeleteObject(brush);
            EndPaint(hwnd, &ps);
            return 0;
        }
    case WM_ERASEBKGND:
        return 1; // skip background erase
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    // Register main window class
    WNDCLASS wc = {};
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "MainWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClass(&wc);

    // Register overlay window class
    WNDCLASS wc2 = {};
    wc2.lpfnWndProc = OverlayWndProc;
    wc2.hInstance = hInstance;
    wc2.lpszClassName = "OverlayWindowClass";
    wc2.style = CS_HREDRAW | CS_VREDRAW;
    wc2.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wc2);

    // Create main window
    HWND mainHwnd = CreateWindowEx(
        0,
        "MainWindowClass",
        "WebView2App - Main",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
        NULL, NULL, hInstance, NULL);
    ShowWindow(mainHwnd, nCmdShow);

    // Create overlay child
    HWND overlayHwnd = CreateWindowEx(
        WS_EX_TRANSPARENT,
        "OverlayWindowClass",
        NULL,
        WS_CHILD | WS_VISIBLE,
        0, 0, 800, 600,
        mainHwnd,
        (HMENU)ID_OVERLAY,
        hInstance,
        NULL);

    // Message loop
    MSG msg;
    while (GetMessage(&msg,NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
