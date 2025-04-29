use anyhow::Result;
use std::{ffi::OsStr, os::windows::ffi::OsStrExt, ptr::null_mut, thread};
use windows::Win32::Foundation::COLORREF;
use windows::Win32::Graphics::Gdi::{GetSysColorBrush, SYS_COLOR_INDEX, UpdateWindow};
use windows::Win32::UI::Input::Pointer::{EnableMouseInPointer, GetPointerType};
use windows::Win32::UI::WindowsAndMessaging::{PT_PEN, SetLayeredWindowAttributes};
use windows::{
    Win32::{
        Foundation::{HINSTANCE, HWND, LPARAM, LRESULT, WPARAM},
        System::LibraryLoader::GetModuleHandleW,
        UI::WindowsAndMessaging::{
            CS_HREDRAW, CS_VREDRAW, CreateWindowExW, DefWindowProcW, DispatchMessageW, GetMessageW,
            IDC_ARROW, LWA_ALPHA, LoadCursorW, PostQuitMessage, RegisterClassW, SW_SHOW,
            ShowWindow, TranslateMessage, WM_DESTROY, WM_POINTERDOWN, WM_POINTERUP,
            WM_POINTERUPDATE, WNDCLASSW, WS_EX_LAYERED, WS_EX_NOACTIVATE, WS_EX_TOPMOST, WS_POPUP,
            WS_VISIBLE,
        },
    },
    core::PCWSTR,
};
use winit::event::StartCause;
use winit::event_loop::ActiveEventLoop;
use winit::{
    application::ApplicationHandler,
    event::WindowEvent,
    event_loop::EventLoop,
    window::{Window, WindowId},
};
use wry::{ScrollBarStyle, WebView, WebViewBuilder, WebViewBuilderExtWindows};

/// Utility to convert a Rust &str into a null-terminated wide (UTF-16) string for Win32 APIs.
fn to_wide(s: &str) -> Vec<u16> {
    OsStr::new(s).encode_wide().chain(Some(0)).collect()
}

#[derive(Default)]
struct App {
    window: Option<Window>,
    webview: Option<WebView>,
}

impl ApplicationHandler for App {
    fn new_events(&mut self, event_loop: &ActiveEventLoop, cause: StartCause) {
        // Spawn the overlay thread once when the app starts
        thread::spawn(|| unsafe {
            // Enable mouse pointer as pointer input too
            let _ = EnableMouseInPointer(true);

            // Get module handle for class registration
            let hinst = GetModuleHandleW(None).unwrap();
            let class_name = to_wide("OverlayWindowClass");

            // Define and register the window class for the transparent overlay
            let wc = WNDCLASSW {
                lpfnWndProc: Some(overlay_wndproc),
                hInstance: HINSTANCE(hinst.0),
                lpszClassName: PCWSTR(class_name.as_ptr()),
                hCursor: LoadCursorW(None, IDC_ARROW).unwrap(),
                style: CS_HREDRAW | CS_VREDRAW,
                hbrBackground: GetSysColorBrush(SYS_COLOR_INDEX::default()),
                ..Default::default()
            };
            let _ = RegisterClassW(&wc);

            // Get full-screen dimensions
            let screen_w = windows::Win32::UI::WindowsAndMessaging::GetSystemMetrics(
                windows::Win32::UI::WindowsAndMessaging::SM_CXSCREEN,
            );
            let screen_h = windows::Win32::UI::WindowsAndMessaging::GetSystemMetrics(
                windows::Win32::UI::WindowsAndMessaging::SM_CYSCREEN,
            );

            // Create a layered, topmost, non-activating overlay window
            let hwnd = CreateWindowExW(
                WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                PCWSTR(class_name.as_ptr()),
                PCWSTR(null_mut()),
                WS_POPUP | WS_VISIBLE,
                0,
                0,
                screen_w,
                screen_h,
                None,
                None,
                Some(HINSTANCE(hinst.0)),
                None,
            )
            .unwrap();

            // Set 50% opacity on the overlay
            let opacity: u8 = 128; // 0 = fully transparent, 255 = opaque
            SetLayeredWindowAttributes(hwnd, COLORREF::default(), opacity, LWA_ALPHA).unwrap();

            ShowWindow(hwnd, SW_SHOW).unwrap();
            UpdateWindow(hwnd).unwrap();

            // Standard Win32 message loop for the overlay
            let mut msg = Default::default();
            while GetMessageW(&mut msg, None, 0, 0).0 != 0 {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        });
    }

    fn resumed(&mut self, event_loop: &ActiveEventLoop) {
        // Create the winit window for the back layer
        let window = event_loop
            .create_window(Window::default_attributes())
            .unwrap();

        // Initialize the Wry WebView with transparent background and overlay scrollbar style
        let webview = WebViewBuilder::new()
            .with_html("<html><body><h1>Hello, World</h1></body></html>")
            .with_scroll_bar_style(ScrollBarStyle::FluentOverlay)
            .with_transparent(true) // Windows-only setting
            .build(&window)
            .unwrap();

        self.window = Some(window);
        self.webview = Some(webview);
    }

    fn window_event(
        &mut self,
        _event_loop: &ActiveEventLoop,
        _window_id: WindowId,
        event: WindowEvent,
    ) {
        // In the future, you can forward non-stylus events to the webview here
        if let WindowEvent::CloseRequested = event {
            // Handle app shutdown if needed
        }
    }
}

fn main() -> Result<()> {
    let event_loop = EventLoop::new().unwrap();
    let mut app = App::default();
    event_loop.run_app(&mut app).unwrap();
    Ok(())
}

/// Window procedure for the overlay window:
/// - Captures WM_POINTER* events and logs pen (stylus) input
extern "system" fn overlay_wndproc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    unsafe {
        match msg {
            WM_DESTROY => {
                PostQuitMessage(0);
                return LRESULT(0);
            }
            WM_POINTERDOWN | WM_POINTERUPDATE | WM_POINTERUP => {
                let pointer_id = (wparam.0 as u32) & 0xFFFF;
                let mut ptype = PT_PEN;
                if GetPointerType(pointer_id, &mut ptype).is_ok() && ptype == PT_PEN {
                    // Detected stylus (pen) event; future drawing logic goes here
                    match msg {
                        WM_POINTERDOWN => println!("Stylus DOWN, id={}", pointer_id),
                        WM_POINTERUPDATE => println!("Stylus MOVE, id={}", pointer_id),
                        WM_POINTERUP => println!("Stylus UP, id={}", pointer_id),
                        _ => {}
                    }
                    return LRESULT(0); // Mark as handled
                }
            }
            _ => {}
        }
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}
