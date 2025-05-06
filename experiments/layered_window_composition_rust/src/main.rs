use anyhow::{Result, anyhow, bail};
use std::sync::mpsc;
use webview2_com::CreateCoreWebView2EnvironmentCompletedHandler;
use webview2_com::Microsoft::Web::WebView2::Win32::{
    CreateCoreWebView2Environment, CreateCoreWebView2EnvironmentWithOptions,
    ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler,
    ICoreWebView2Environment3,
};
use windows::UI::Composition::Compositor;
use windows::UI::Composition::Desktop::DesktopWindowTarget;
use windows::Win32::Foundation::{E_POINTER, HINSTANCE, HWND, LPARAM, LRESULT, WPARAM};
use windows::Win32::System::Com::{COINIT_APARTMENTTHREADED, CoInitializeEx};
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::System::WinRT::Composition::ICompositorDesktopInterop;
use windows::Win32::System::WinRT::{
    CreateDispatcherQueueController, DQTAT_COM_ASTA, DQTYPE_THREAD_CURRENT, DispatcherQueueOptions,
};
use windows::Win32::UI::WindowsAndMessaging::{
    CW_USEDEFAULT, CreateWindowExW, DefWindowProcW, DispatchMessageW, GetMessageW, MSG,
    PostQuitMessage, RegisterClassW, SW_SHOW, ShowWindow, TranslateMessage, WINDOW_EX_STYLE,
    WM_DESTROY, WM_QUIT, WNDCLASSW, WS_OVERLAPPEDWINDOW,
};
use windows::core::{Interface, PCWSTR, w};
use windows_numerics::{Vector2, Vector3};

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

        // TODO

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
