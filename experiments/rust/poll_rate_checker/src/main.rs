use std::collections::HashMap;
use std::time::Instant;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Input::Pointer::GetPointerType;
use windows::core::w;
use windows::{
    Win32::{
        Foundation::{HWND, LPARAM, LRESULT, WPARAM},
        UI::WindowsAndMessaging::*,
    },
    core::PCWSTR,
};

unsafe extern "system" fn wnd_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    unsafe {
        match msg {
            WM_CREATE => {
                // grab our leaked Box pointer from CREATESTRUCTW.lpCreateParams
                let cs = &*(lparam.0 as *const CREATESTRUCTW);
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, cs.lpCreateParams as isize);
            }
            WM_POINTERUPDATE | WM_POINTERDOWN => {
                let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut HashMap<u32, Instant>;
                let map: &mut HashMap<u32, Instant> = &mut *ptr;

                let raw = wparam.0 as u32;
                let pid = raw & 0xFFFF;
                // Determine device type
                let mut ptype = POINTER_INPUT_TYPE::default();
                if GetPointerType(pid, &mut ptype).is_ok() && ptype == PT_PEN {
                    let now = Instant::now();
                    if let Some(prev) = map.insert(pid, now) {
                        let delta = now.duration_since(prev);
                        let rate_hz = 1.0 / delta.as_secs_f64();
                        println!(
                            "PointerID {}: Δ = {:?}, Polling rate ≈ {:.2} Hz",
                            pid, delta, rate_hz
                        );
                    }
                }
            }
            WM_NCDESTROY => {
                let ptr = GetWindowLongPtrW(hwnd, GWLP_USERDATA) as *mut HashMap<u32, Instant>;
                let _ = Box::from_raw(ptr);
                return DefWindowProcW(hwnd, msg, wparam, lparam);
            }
            _ => return DefWindowProcW(hwnd, msg, wparam, lparam),
        }
        LRESULT(0)
    }
}

fn main() -> windows::core::Result<()> {
    unsafe {
        let h_instance = GetModuleHandleW(None)?;
        let class_name = w!("StylusPollChecker");

        let wc = WNDCLASSW {
            hInstance: h_instance.into(),
            lpszClassName: class_name,
            lpfnWndProc: Some(wnd_proc),
            hCursor: LoadCursorW(None, IDC_ARROW)?,
            ..Default::default()
        };
        RegisterClassW(&wc);

        let map = Box::new(HashMap::<u32, Instant>::new());
        let map_ptr = Box::into_raw(map);
        let hwnd = CreateWindowExW(
            Default::default(),
            class_name,
            PCWSTR::from_raw(
                "Stylus Poll Rate\0"
                    .encode_utf16()
                    .collect::<Vec<_>>()
                    .as_ptr(),
            ),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            400,
            200,
            None,
            None,
            Some(h_instance.into()),
            Some(map_ptr as *mut _),
        );

        let _ = ShowWindow(hwnd?, SW_SHOW);
        let mut msg = MSG::default();

        while GetMessageW(&mut msg, None, 0, 0).into() {
            let _ = TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    Ok(())
}
