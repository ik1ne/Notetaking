use std::cell::RefCell;
use std::ptr::null;

use anyhow::Result;
use hover_core::controller::{AppController, Controller};
use hover_core::model::{AppState, Model, PointerState};
use windows::Win32::Foundation::{HINSTANCE, HWND, LPARAM, LRESULT, RECT, WPARAM};
use windows::Win32::Graphics::Direct2D::Common::*;
use windows::Win32::Graphics::Direct2D::*;
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_UNKNOWN;
use windows::Win32::Graphics::Gdi::{
    BeginPaint, EndPaint, InvalidateRect, PAINTSTRUCT, ScreenToClient,
};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::UI::Input::Pointer::*;
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::*;
use windows_numerics::Vector2;

// Shared global states
thread_local! {
    static CONTROLLER: RefCell<Option<AppController<AppState>>> = const { RefCell::new(None) };
    static D2D_CONTEXT: RefCell<Option<D2DContext>> = const { RefCell::new(None) };
}

struct D2DContext {
    _factory: ID2D1Factory,
    render_target: ID2D1HwndRenderTarget,
    hover_brush: ID2D1SolidColorBrush,
    press_brush: ID2D1SolidColorBrush,
    dot_radius: f32,
}

/// Entry point
pub unsafe fn run_message_loop() -> Result<()> {
    unsafe {
        let h_instance = GetModuleHandleW(None)?;
        let hwnd = create_main_window(h_instance.into())?;

        ShowCursor(false);

        CONTROLLER.with(|c| {
            *c.borrow_mut() = Some(AppController::new(AppState::new()));
        });

        let context = create_d2d_context(hwnd)?;

        D2D_CONTEXT.with(|ctx| {
            *ctx.borrow_mut() = Some(context);
        });

        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).into() {
            if !TranslateMessage(&msg).as_bool() {
                // panic!("Failed to translate message: {:?}", msg);
            }
            DispatchMessageW(&msg);
        }
    }

    Ok(())
}

/// Create the main window
unsafe fn create_main_window(h_instance: HINSTANCE) -> Result<HWND> {
    let hwnd = unsafe {
        let class_name = w!("HoverDemoWindowClass");

        let wc = WNDCLASSW {
            hInstance: h_instance,
            lpszClassName: class_name,
            lpfnWndProc: Some(wnd_proc),
            hCursor: LoadCursorW(None, IDC_ARROW).unwrap(),
            ..Default::default()
        };
        RegisterClassW(&wc);

        CreateWindowExW(
            Default::default(),
            class_name,
            w!("Hover Demo"),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            600,
            None,
            None,
            Some(h_instance),
            Some(null()),
        )
    };

    Ok(hwnd?)
}

/// Initialize D2D drawing context
unsafe fn create_d2d_context(hwnd: HWND) -> Result<D2DContext> {
    unsafe {
        let factory = D2D1CreateFactory::<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;

        let mut rect = RECT::default();
        GetClientRect(hwnd, &mut rect)?;

        let props = D2D1_RENDER_TARGET_PROPERTIES {
            r#type: D2D1_RENDER_TARGET_TYPE_DEFAULT,
            pixelFormat: D2D1_PIXEL_FORMAT {
                format: DXGI_FORMAT_UNKNOWN,
                alphaMode: D2D1_ALPHA_MODE_IGNORE,
            },
            dpiX: 96.0,
            dpiY: 96.0,
            usage: D2D1_RENDER_TARGET_USAGE_NONE,
            minLevel: D2D1_FEATURE_LEVEL_DEFAULT,
        };
        let hwnd_props = D2D1_HWND_RENDER_TARGET_PROPERTIES {
            hwnd,
            pixelSize: D2D_SIZE_U {
                width: (rect.right - rect.left) as u32,
                height: (rect.bottom - rect.top) as u32,
            },
            presentOptions: D2D1_PRESENT_OPTIONS_NONE,
        };

        let render_target = factory.CreateHwndRenderTarget(&props, &hwnd_props).unwrap();

        let hover_brush = render_target.CreateSolidColorBrush(
            &D2D1_COLOR_F {
                r: 1.0,
                g: 0.0,
                b: 0.0,
                a: 1.0,
            },
            None,
        )?;

        let press_brush = render_target.CreateSolidColorBrush(
            &D2D1_COLOR_F {
                r: 0.0,
                g: 0.0,
                b: 1.0,
                a: 1.0,
            },
            None,
        )?;

        Ok(D2DContext {
            _factory: factory,
            render_target,
            hover_brush,
            press_brush,
            dot_radius: 5.0,
        })
    }
}

/// Windows WndProc
extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe {
        match msg {
            WM_POINTERUPDATE => {
                handle_pointer_update(hwnd, wparam);
                if !InvalidateRect(Some(hwnd), None, false).as_bool() {
                    panic!("Failed to invalidate rect");
                }
            }
            WM_POINTERLEAVE => {
                handle_pointer_leave();
                if !InvalidateRect(Some(hwnd), None, false).as_bool() {
                    panic!("Failed to invalidate rect");
                }
            }
            WM_PAINT => {
                handle_paint(hwnd);
            }
            WM_DESTROY => {
                PostQuitMessage(0);
            }
            _ => {}
        }
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}

/// Handle pointer move and press
unsafe fn handle_pointer_update(hwnd: HWND, wparam: WPARAM) {
    unsafe {
        let pointer_id = (wparam.0 & 0xFFFF) as u32;
        let mut pen_info = POINTER_PEN_INFO::default();
        if GetPointerPenInfo(pointer_id, &mut pen_info).is_ok() {
            let mut screen_point = pen_info.pointerInfo.ptPixelLocation;
            if !ScreenToClient(hwnd, &mut screen_point).as_bool() {
                panic!("Failed to convert screen point to client point");
            }

            let x = screen_point.x as f32;
            let y = screen_point.y as f32;
            let in_contact =
                pen_info.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT != POINTER_FLAGS(0);

            CONTROLLER.with(|c| {
                if let Some(ctrl) = &mut *c.borrow_mut() {
                    ctrl.on_pointer_update(x, y, in_contact);
                }
            });
        }
    }
}

/// Handle pointer leave
unsafe fn handle_pointer_leave() {
    CONTROLLER.with(|c| {
        if let Some(ctrl) = &mut *c.borrow_mut() {
            ctrl.on_pointer_leave();
        }
    });
}

/// Handle painting (WM_PAINT)
unsafe fn handle_paint(hwnd: HWND) {
    unsafe {
        let mut ps = PAINTSTRUCT::default();
        let _hdc = BeginPaint(hwnd, &mut ps);

        D2D_CONTEXT.with(|ctx| {
            if let Some(context) = &*ctx.borrow() {
                context.render_target.BeginDraw();
                context.render_target.Clear(Some(&D2D1_COLOR_F {
                    r: 1.0,
                    g: 1.0,
                    b: 1.0,
                    a: 1.0,
                }));

                CONTROLLER.with(|c| {
                    if let Some(ctrl) = &*c.borrow() {
                        draw_pointer_state(context, &ctrl.model);
                    }
                });

                context.render_target.EndDraw(None, None).ok();
            }
        });

        if !EndPaint(hwnd, &ps).as_bool() {
            panic!("Failed to end paint");
        }
    }
}

/// Draw the pointer hover/press state
fn draw_pointer_state(ctx: &D2DContext, model: &impl Model) {
    unsafe {
        match model.pointer_state() {
            PointerState::Hovered { x, y } => {
                ctx.render_target.FillEllipse(
                    &D2D1_ELLIPSE {
                        point: Vector2::new(x, y),
                        radiusX: ctx.dot_radius,
                        radiusY: ctx.dot_radius,
                    },
                    &ctx.hover_brush,
                );
            }
            PointerState::Pressed { x, y } => {
                ctx.render_target.FillEllipse(
                    &D2D1_ELLIPSE {
                        point: Vector2::new(x, y),
                        radiusX: ctx.dot_radius,
                        radiusY: ctx.dot_radius,
                    },
                    &ctx.press_brush,
                );
            }
            PointerState::Released => {}
        }
    }
}
