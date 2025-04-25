use crate::controller::{AppController, Controller};
use crate::model::{AppState, Model, PointerState};
use std::cell::RefCell;
use std::ptr::{null, null_mut};
use std::rc::Rc;
use windows::Win32::Graphics::Gdi::{
    BeginPaint, EndPaint, InvalidateRect, PAINTSTRUCT, ScreenToClient,
};
use windows::{
    Win32::{
        Foundation::*,
        Graphics::{Direct2D::Common::*, Direct2D::*, DirectWrite::*, Dxgi::Common::*},
        System::LibraryLoader::*,
        UI::Input::Pointer::*,
        UI::WindowsAndMessaging::*,
    },
    core::*,
};
use windows_numerics::Vector2;

// Shared state
thread_local! {
    static CONTROLLER: RefCell<Option<AppController<AppState>>> = RefCell::new(None);
    static D2D_CONTEXT: RefCell<Option<D2DContext>> = RefCell::new(None);
}

struct D2DContext {
    factory: ID2D1Factory,
    render_target: ID2D1HwndRenderTarget,
    red_brush: ID2D1SolidColorBrush,
    blue_brush: ID2D1SolidColorBrush,
}

pub unsafe fn run_message_loop() {
    let h_instance = GetModuleHandleW(None).unwrap();

    let class_name = w!("HoverDemoWindowClass");

    let wc = WNDCLASSW {
        hInstance: h_instance.into(),
        lpszClassName: class_name,
        lpfnWndProc: Some(wnd_proc),
        hCursor: LoadCursorW(None, IDC_ARROW).unwrap(),
        ..Default::default()
    };
    RegisterClassW(&wc);

    let hwnd = CreateWindowExW(
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
        Some(h_instance.into()),
        Some(null()),
    );

    ShowCursor(false);

    CONTROLLER.with(|c| {
        *c.borrow_mut() = Some(AppController::new(AppState::new()));
    });

    D2D_CONTEXT.with(|ctx| {
        let factory: ID2D1Factory =
            D2D1CreateFactory::<ID2D1Factory>(D2D1_FACTORY_TYPE_SINGLE_THREADED, Some(null()))
                .unwrap();

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

        let mut rect = RECT::default();
        GetClientRect(hwnd.clone().unwrap(), &mut rect);
        let hwnd_props = D2D1_HWND_RENDER_TARGET_PROPERTIES {
            hwnd: hwnd.unwrap(),
            pixelSize: D2D_SIZE_U {
                width: (rect.right - rect.left) as u32,
                height: (rect.bottom - rect.top) as u32,
            },
            presentOptions: D2D1_PRESENT_OPTIONS_NONE,
        };

        factory.CreateHwndRenderTarget(&props, &hwnd_props).unwrap();
        let render_target = factory.CreateHwndRenderTarget(&props, &hwnd_props).unwrap();

        let red_brush = render_target
            .CreateSolidColorBrush(
                &D2D1_COLOR_F {
                    r: 1.0,
                    g: 0.0,
                    b: 0.0,
                    a: 1.0,
                },
                None,
            )
            .unwrap();

        let blue_brush = render_target
            .CreateSolidColorBrush(
                &D2D1_COLOR_F {
                    r: 0.0,
                    g: 0.0,
                    b: 1.0,
                    a: 1.0,
                },
                None,
            )
            .unwrap();

        // Now store them directly
        *ctx.borrow_mut() = Some(D2DContext {
            factory,
            render_target,
            red_brush,
            blue_brush,
        });
    });

    let mut msg = MSG::default();
    while GetMessageW(&mut msg, None, 0, 0).into() {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

extern "system" fn wnd_proc(hwnd: HWND, msg: u32, wparam: WPARAM, lparam: LPARAM) -> LRESULT {
    unsafe {
        match msg {
            WM_POINTERUPDATE => {
                let pointer_id = (wparam.0 & 0xFFFF) as u32;
                let mut pen_info = POINTER_PEN_INFO::default();
                if GetPointerPenInfo(pointer_id, &mut pen_info).is_ok() {
                    let mut screen_point = pen_info.pointerInfo.ptPixelLocation;
                    ScreenToClient(hwnd, &mut screen_point);
                    let x = screen_point.x as f32;
                    let y = screen_point.y as f32;
                    let in_contact = pen_info.pointerInfo.pointerFlags & POINTER_FLAG_INCONTACT
                        != POINTER_FLAGS(0);
                    CONTROLLER.with(|c| {
                        if let Some(ctrl) = &mut *c.borrow_mut() {
                            ctrl.on_pointer_update(x as f32, y as f32, in_contact);
                        }
                    });
                    InvalidateRect(Some(hwnd), Some(null()), false);
                }
            }
            WM_POINTERLEAVE => {
                CONTROLLER.with(|c| {
                    if let Some(ctrl) = &mut *c.borrow_mut() {
                        ctrl.on_pointer_leave();
                    }
                });
                InvalidateRect(Some(hwnd), Some(null()), false);
            }
            WM_PAINT => {
                let mut ps = PAINTSTRUCT::default();
                let hdc = BeginPaint(hwnd, &mut ps);

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
                                match ctrl.model.pointer_state() {
                                    PointerState::Hovered { x, y } => {
                                        context.render_target.FillEllipse(
                                            &D2D1_ELLIPSE {
                                                point: Vector2 { X: x, Y: y },
                                                radiusX: 5.0,
                                                radiusY: 5.0,
                                            },
                                            &context.red_brush,
                                        );
                                    }
                                    PointerState::Pressed { x, y } => {
                                        context.render_target.FillEllipse(
                                            &D2D1_ELLIPSE {
                                                point: Vector2 { X: x, Y: y },
                                                radiusX: 5.0,
                                                radiusY: 5.0,
                                            },
                                            &context.blue_brush,
                                        );
                                    }
                                    _ => {}
                                }
                            }
                        });

                        context.render_target.EndDraw(None, None).ok();
                    }
                });

                EndPaint(hwnd, &ps);
                return LRESULT(0);
            }
            WM_DESTROY => {
                PostQuitMessage(0);
                return LRESULT(0);
            }
            _ => {}
        }
        DefWindowProcW(hwnd, msg, wparam, lparam)
    }
}
