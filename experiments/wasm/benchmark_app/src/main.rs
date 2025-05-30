#![allow(unused)]
mod native_renderer;
// mod wasm_renderer;

use std::{collections::HashMap, time::Instant};
use windows::Win32::Foundation::{HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::Graphics::Direct2D::{
    D2D1_FACTORY_TYPE_SINGLE_THREADED, D2D1_HWND_RENDER_TARGET_PROPERTIES,
    D2D1_RENDER_TARGET_PROPERTIES, D2D1CreateFactory, ID2D1Factory, ID2D1HwndRenderTarget,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::w;

unsafe extern "system" fn wnd_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    unsafe {
        match msg {
            WM_CREATE => {
                // Retrieve our leaked Box<HashMap> pointer
                let cs = &*(lparam.0 as *const CREATESTRUCTW);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, cs.lpCreateParams as isize);
                // Spawn synthetic-pen injection thread here,
                // capturing `hwnd` and the map pointer.
                LRESULT(0)
            }
            WM_POINTERDOWN | WM_POINTERUPDATE => {
                // Access the timestamp map without a Mutex
                let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut HashMap<u32, Instant>;
                let map = &mut *ptr;
                // Batch and flush every 8 ms; record t0/t1 per batch
                // ... (implementation goes here)
                LRESULT(0)
            }
            WM_POINTERUP => {
                // delete_stroke, pull_draw_delta, render erase
                LRESULT(0)
            }
            WM_NCDESTROY => {
                // Clean up our leaked Box
                let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut HashMap<u32, Instant>;
                let _ = Box::from_raw(ptr);
                DefWindowProcW(hwnd, msg, wparam, lparam)
            }
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            _ => DefWindowProcW(hwnd, msg, wparam, lparam),
        }
    }
}

fn main() -> windows::core::Result<()> {
    unsafe {
        let hinstance = GetModuleHandleW(None)?;
        let class_name = w!("BenchmarkClass");

        let wc = WNDCLASSW {
            hInstance: hinstance.into(),
            lpfnWndProc: Some(wnd_proc),
            lpszClassName: class_name,
            ..Default::default()
        };
        RegisterClassW(&wc);

        // Box and leak our timestamp map to avoid Mutex overhead
        let map = Box::new(HashMap::<u32, Instant>::new());
        let map_ptr = Box::into_raw(map);

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
            Some(map_ptr as *mut _),
        )?;
        let _ = ShowWindow(hwnd, SW_SHOW);

        // Initialize Direct2D factory & render target
        let factory: ID2D1Factory =
            D2D1CreateFactory::<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;
        let rt: ID2D1HwndRenderTarget = factory.CreateHwndRenderTarget(
            &D2D1_RENDER_TARGET_PROPERTIES::default(),
            &D2D1_HWND_RENDER_TARGET_PROPERTIES {
                hwnd,
                pixelSize: Default::default(),
                presentOptions: Default::default(),
            },
        )?;

        // Main message loop
        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).into() {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    Ok(())
}
