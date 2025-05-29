// benchmark_app/src/main.rs

// mod native_renderer;
// mod wasm_renderer;

use std::{
    collections::HashMap,
    env,
    sync::{Arc, Mutex},
    time::Instant,
};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Direct2D::{
    D2D1_FACTORY_TYPE_SINGLE_THREADED, D2D1_HWND_RENDER_TARGET_PROPERTIES,
    D2D1_RENDER_TARGET_PROPERTIES, D2D1CreateFactory, ID2D1Factory, ID2D1HwndRenderTarget,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::{
    CW_USEDEFAULT, CreateWindowExW, DefWindowProcW, DispatchMessageW, GetMessageW, MSG,
    PostQuitMessage, RegisterClassW, SW_SHOW, ShowWindow, TranslateMessage, WM_CREATE, WM_DESTROY,
    WM_POINTERDOWN, WM_POINTERUP, WM_POINTERUPDATE, WNDCLASSW, WNDPROC, WS_OVERLAPPEDWINDOW,
};
use windows::core::{PCWSTR, w};

// Shared synthetic input timing map
static mut TIMESTAMPS: Option<Arc<Mutex<HashMap<u32, Instant>>>> = None;

// Modes
enum Mode {
    Native,
    Wasm,
}

fn main() {
    // Determine mode: any second arg => Native, else Wasm
    let mode = if env::args().nth(1).is_some() {
        Mode::Native
    } else {
        Mode::Wasm
    };

    unsafe {
        let hinstance = GetModuleHandleW(None).unwrap();
        let class_name: PCWSTR = PCWSTR::from_raw(w!("BenchmarkClass").as_ptr());
        let wc = WNDCLASSW {
            hInstance: hinstance.into(),
            lpfnWndProc: Some(wnd_proc),
            lpszClassName: class_name,
            ..Default::default()
        };
        RegisterClassW(&wc);
        let hwnd = CreateWindowExW(
            Default::default(),
            class_name,
            w!("WASM vs Native Benchmark"),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            600,
            None,
            None,
            Some(hinstance.into()),
            None,
        )
        .unwrap();
        ShowWindow(hwnd, SW_SHOW);

        // Initialize Direct2D factory & render target
        let factory: ID2D1Factory =
            D2D1CreateFactory::<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, None).unwrap();

        let rt: ID2D1HwndRenderTarget = factory
            .CreateHwndRenderTarget(
                &D2D1_RENDER_TARGET_PROPERTIES::default(),
                &D2D1_HWND_RENDER_TARGET_PROPERTIES {
                    hwnd,
                    pixelSize: Default::default(),
                    presentOptions: Default::default(),
                },
            )
            .unwrap();

        // Initialize timestamp map
        TIMESTAMPS = Some(Arc::new(Mutex::new(HashMap::new())));

        // Spawn injection thread on WM_CREATE
        // ... (will fill later)

        // Message loop
        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).into() {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}

extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    match msg {
        WM_CREATE => {
            // Spawn synthetic-pen injection thread
            LRESULT(0)
        }
        WM_POINTERDOWN => {
            /* call begin_stroke */
            LRESULT(0)
        }
        WM_POINTERUPDATE => {
            /* batch & flush 8ms */
            LRESULT(0)
        }
        WM_POINTERUP => {
            /* delete_stroke & flush */
            LRESULT(0)
        }
        WM_DESTROY => {
            unsafe {
                PostQuitMessage(0);
            }
            LRESULT(0)
        }
        _ => unsafe { DefWindowProcW(hwnd, msg, wparam, lparam) },
    }
}
