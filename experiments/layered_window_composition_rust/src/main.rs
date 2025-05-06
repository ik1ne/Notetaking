use anyhow::{Result, anyhow, bail};
use std::cell::RefCell;
use std::sync::mpsc;
use webview2_com::Microsoft::Web::WebView2::Win32::{
    CreateCoreWebView2Environment, ICoreWebView2CompositionController, ICoreWebView2Controller,
    ICoreWebView2Environment3,
};
use webview2_com::{
    CreateCoreWebView2CompositionControllerCompletedHandler,
    CreateCoreWebView2EnvironmentCompletedHandler, CursorChangedEventHandler,
};
use windows::UI::Color;
use windows::UI::Composition::Compositor;
use windows::Win32::Foundation::{E_POINTER, HINSTANCE, HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::System::Com::{COINIT_APARTMENTTHREADED, CoInitializeEx};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::System::WinRT::Composition::ICompositorDesktopInterop;
use windows::Win32::System::WinRT::{
    CreateDispatcherQueueController, DQTAT_COM_ASTA, DQTYPE_THREAD_CURRENT, DispatcherQueueOptions,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CW_USEDEFAULT, CreateWindowExW, DefWindowProcW, DispatchMessageW, GetClientRect, GetMessageW,
    HCURSOR, MSG, PostMessageW, PostQuitMessage, RegisterClassW, SW_SHOW, SetCursor, ShowWindow,
    TranslateMessage, WINDOW_EX_STYLE, WM_DESTROY, WM_SIZE, WNDCLASSW, WS_OVERLAPPEDWINDOW,
};
use windows::core::{Interface, PCWSTR, w};
use windows_numerics::{Vector2, Vector3};

thread_local! {
    static COMPOSITION_CONTROLLER: RefCell<Option<ICoreWebView2CompositionController>> = RefCell::new(None);
}

const MAIN_CLASS_NAME: PCWSTR = w!("MainClass");

fn main() -> Result<()> {
    unsafe {
        let h_instance: HINSTANCE = GetModuleHandleW(None)?.into();

        CoInitializeEx(None, COINIT_APARTMENTTHREADED).ok()?;

        let wnd_class = WNDCLASSW {
            lpfnWndProc: Some(main_proc),
            hInstance: h_instance,
            lpszClassName: MAIN_CLASS_NAME,
            ..Default::default()
        };
        if RegisterClassW(&wnd_class) == 0 {
            bail!("RegisterClassW failed");
        }

        let hwnd = CreateWindowExW(
            WINDOW_EX_STYLE::default(),
            MAIN_CLASS_NAME,
            w!("Hello World!"),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            600,
            None,
            None,
            Some(h_instance),
            None,
        )?;

        // ShowWindow returns false if it was invisible
        let _ = ShowWindow(hwnd, SW_SHOW);

        let _dqueue = CreateDispatcherQueueController(DispatcherQueueOptions {
            dwSize: size_of::<DispatcherQueueOptions>() as u32,
            threadType: DQTYPE_THREAD_CURRENT,
            apartmentType: DQTAT_COM_ASTA,
        })?;

        let compositor = Compositor::new()?;
        let interop: ICompositorDesktopInterop = compositor.cast()?;
        let target = interop.CreateDesktopWindowTarget(hwnd, false)?;

        let root = compositor.CreateContainerVisual()?;
        root.SetRelativeSizeAdjustment(Vector2::one())?;
        root.SetOffset(Vector3::zero())?;
        target.SetRoot(&root)?;

        let environment = {
            let (tx, rx) = mpsc::channel();

            CreateCoreWebView2EnvironmentCompletedHandler::wait_for_async_operation(
                Box::new(|environmentcreatedhandler| {
                    CreateCoreWebView2Environment(&environmentcreatedhandler)
                        .map_err(webview2_com::Error::WindowsError)
                }),
                Box::new(move |error_code, environment| {
                    error_code?;
                    tx.send(environment.ok_or(windows::core::Error::from(E_POINTER)))
                        .expect("send over mpsc channel");
                    Ok(())
                }),
            )
            .map_err(|_| anyhow!("CreateCoreWebView2Environment failed"))?;

            rx.recv()?
        }?;

        let env3: ICoreWebView2Environment3 = environment.cast()?;

        let comp_controller = {
            let (tx, rx) = mpsc::channel();
            CreateCoreWebView2CompositionControllerCompletedHandler::wait_for_async_operation(
                Box::new(move |handler| {
                    env3.CreateCoreWebView2CompositionController(hwnd, &handler)
                        .map_err(webview2_com::Error::WindowsError)
                }),
                Box::new(move |error_code, controller| {
                    error_code?;
                    tx.send(controller.ok_or(windows::core::Error::from(E_POINTER)))
                        .unwrap();
                    Ok(())
                }),
            )
            .map_err(|_| anyhow!("CreateCoreWebView2CompositionController failed"))?;

            rx.recv().unwrap()?
        };

        COMPOSITION_CONTROLLER.replace(Some(comp_controller.clone()));

        let mut _token = 0;

        comp_controller.add_CursorChanged(
            &CursorChangedEventHandler::create(Box::new(move |sender, _unknown| {
                let Some(sender) = sender else { return Ok(()) };
                let mut cursor = HCURSOR::default();
                sender.Cursor(&mut cursor)?;

                SetCursor(Some(cursor));

                Ok(())
            })),
            &mut _token,
        )?;

        let webview_controller: ICoreWebView2Controller = comp_controller.cast()?;
        webview_controller.SetIsVisible(true)?;
        let webview_visual = compositor.CreateContainerVisual()?;
        root.Children()?.InsertAtTop(&webview_visual)?;
        let overlay_visual = compositor.CreateSpriteVisual()?;
        let brush = compositor.CreateColorBrush()?;
        brush.SetColor(Color {
            A: 0x00,
            R: 0x00,
            G: 0x00,
            B: 0x00,
        })?;
        overlay_visual.SetBrush(&brush)?;
        overlay_visual.SetRelativeSizeAdjustment(Vector2::one())?;
        root.Children()?.InsertAtTop(&overlay_visual)?;

        comp_controller.SetRootVisualTarget(&webview_visual)?;

        let mut bounds = Default::default();
        GetClientRect(hwnd, &mut bounds)?;
        webview_controller.SetBounds(bounds)?;
        webview_controller
            .CoreWebView2()?
            .Navigate(w!("https://www.google.com"))?;

        PostMessageW(Some(hwnd), WM_SIZE, WPARAM(0), LPARAM(0))?;

        let mut msg: MSG = Default::default();
        while GetMessageW(&mut msg, None, 0, 0).as_bool() {
            let _ = TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    };

    Ok(())
}

extern "system" fn main_proc(
    window: HWND,
    message: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    unsafe {
        match message {
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            _ => DefWindowProcW(window, message, wparam, lparam),
        }
    }
}
