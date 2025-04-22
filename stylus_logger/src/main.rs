use windows::Win32::Foundation::*;
use windows::Win32::System::LibraryLoader::*;
use windows::Win32::UI::Input::Pointer::*;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::*;

const PT_TOUCH: POINTER_INPUT_TYPE = POINTER_INPUT_TYPE(0x00000002);
const PT_PEN: POINTER_INPUT_TYPE = POINTER_INPUT_TYPE(0x00000003);
const PT_MOUSE: POINTER_INPUT_TYPE = POINTER_INPUT_TYPE(0x00000004);
const PT_TOUCHPAD: POINTER_INPUT_TYPE = POINTER_INPUT_TYPE(0x00000005);

// Main entry point
fn main() -> Result<()> {
    unsafe {
        // Enable pointer messages for mouse devices (desktop apps default to disabled)&#8203;:contentReference[oaicite:3]{index=3}
        EnableMouseInPointer(true).ok();

        // Get HINSTANCE for this module (needed for registering window class)
        let hinstance = GetModuleHandleW(None)?;
        debug_assert!(!hinstance.0.is_null());

        // Define a window class name (wide string)
        let class_name = w!("PointerInputWindow");

        // Set up WNDCLASSW structure
        let wnd_class = WNDCLASSW {
            hInstance: hinstance.into(),
            lpszClassName: class_name,
            lpfnWndProc: Some(wnd_proc),
            style: CS_HREDRAW | CS_VREDRAW,
            hCursor: LoadCursorW(None, IDC_ARROW)?, // default arrow cursor
            hIcon: LoadIconW(None, IDI_APPLICATION)?, // default application icon
            ..Default::default()
        };
        // Register the window class
        let atom = RegisterClassW(&wnd_class);
        assert!(atom != 0, "Failed to register window class");

        // Create the window (WS_OVERLAPPEDWINDOW with WS_VISIBLE to show it immediately)
        let hwnd = CreateWindowExW(
            WINDOW_EX_STYLE::default(),
            class_name,
            w!("Pointer Input Example"), // window title
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            600, // position (default), size (800x600)
            None,
            None,
            Some(hinstance.into()),
            None,
        );
        assert!(hwnd.is_ok(), "Failed to create window");

        // Run the message loop
        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).into() {
            let _ = TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    Ok(())
}

// Window procedure to handle events
extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe {
        match msg {
            WM_POINTERDOWN | WM_POINTERUP | WM_POINTERUPDATE | WM_POINTERENTER
            | WM_POINTERLEAVE => {
                // Extract the pointer ID from wParam (low 16 bits)
                let pointer_id = (wparam.0 & 0xFFFF) as u32;
                // Determine pointer device type (touch, pen, mouse, etc.)
                let mut pointer_type = POINTER_INPUT_TYPE(0);
                let _ = GetPointerType(pointer_id, &mut pointer_type as *mut _);

                // Retrieve detailed pointer info based on type
                if pointer_type == PT_PEN {
                    let mut pen_info = POINTER_PEN_INFO::default();
                    if GetPointerPenInfo(pointer_id, &mut pen_info).is_ok() {
                        let POINT { x, y } = pen_info.pointerInfo.ptPixelLocation; // screen coordinates
                        let pressure = pen_info.pressure;
                        let in_contact = pen_info.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT
                            != POINTER_FLAGS(0);
                        if msg == WM_POINTERENTER && !in_contact {
                            println!("Pen hover entered at ({}, {})", x, y);
                        } else if msg == WM_POINTERLEAVE && !in_contact {
                            println!("Pen hover left at ({}, {})", x, y);
                        } else {
                            // Determine action label
                            let action = match msg {
                                WM_POINTERDOWN => "Pen DOWN",
                                WM_POINTERUP => "Pen UP",
                                WM_POINTERUPDATE => {
                                    if in_contact {
                                        "Pen MOVE"
                                    } else {
                                        "Pen hover"
                                    }
                                }
                                _ => "Pen", // for any other pointer message
                            };
                            println!("{} at ({}, {})  pressure={}", action, x, y, pressure);
                        }
                    }
                } else if pointer_type == PT_TOUCH {
                    let mut touch_info = POINTER_TOUCH_INFO::default();
                    if GetPointerTouchInfo(pointer_id, &mut touch_info).is_ok() {
                        let POINT { x, y } = touch_info.pointerInfo.ptPixelLocation; // screen coordinates
                        let flags = touch_info.pointerInfo.pointerFlags;
                        let is_primary = flags & POINTER_FLAG_PRIMARY != POINTER_FLAGS(0);
                        let has_confidence = flags & POINTER_FLAG_CONFIDENCE != POINTER_FLAGS(0);
                        let action = match msg {
                            WM_POINTERDOWN => "Touch DOWN",
                            WM_POINTERUP => "Touch UP",
                            WM_POINTERUPDATE => "Touch MOVE",
                            WM_POINTERENTER => "Touch ENTER",
                            WM_POINTERLEAVE => "Touch LEAVE",
                            _ => "Touch",
                        };
                        println!(
                            "{} at ({}, {})  primary={} confidence={}",
                            action, x, y, is_primary, has_confidence
                        );
                        // Note: confidence=false may indicate an unintentional touch (palm)&#8203;:contentReference[oaicite:4]{index=4}.
                    }
                } else {
                    // Mouse or other generic pointer
                    let mut info = POINTER_INFO::default();
                    if GetPointerInfo(pointer_id, &mut info).is_ok() {
                        let POINT { x, y } = info.ptPixelLocation;
                        let device = match pointer_type {
                            PT_MOUSE => "Mouse",
                            PT_TOUCHPAD => "Touchpad",
                            _ => "Pointer",
                        };
                        if msg == WM_POINTERENTER {
                            println!("{} pointer entered at ({}, {})", device, x, y);
                        } else if msg == WM_POINTERLEAVE {
                            println!("{} pointer left at ({}, {})", device, x, y);
                        } else {
                            // Pointer down/up/move for mouse or other
                            let action = match msg {
                                WM_POINTERDOWN => "DOWN",
                                WM_POINTERUP => "UP",
                                WM_POINTERUPDATE => "MOVE",
                                _ => "EVENT",
                            };
                            println!("{} {} at ({}, {})", device, action, x, y);
                        }
                    }
                }
                // Mark message handled by returning 0
                LRESULT(0)
            }
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            // Default handling for other messages
            _ => DefWindowProcW(hwnd, msg, wparam, lparam),
        }
    }
}
