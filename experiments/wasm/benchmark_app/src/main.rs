use anyhow::Result;
use std::env;
use std::path::{Path, PathBuf};
use std::thread::sleep;
use std::time::Duration;
use windows::Win32::Foundation::*;
use windows::Win32::Graphics::Direct2D::Common::*;
use windows::Win32::Graphics::Direct2D::*;
use windows::Win32::Graphics::Dxgi::Common::DXGI_FORMAT_B8G8R8A8_UNORM;
use windows::Win32::System::LibraryLoader::GetModuleHandleW;
use windows::Win32::System::Performance::{QueryPerformanceCounter, QueryPerformanceFrequency};
use windows::Win32::UI::WindowsAndMessaging::*;
use windows::core::w;

use wasm_renderer::WasmRenderer;

mod native_renderer;
mod wasm_renderer;

// Configurable constants
const RUN_ITERATIONS: usize = 1_000;
const TICK_INTERVAL_MS: u64 = 8;

// Wasm linear-memory offsets (bytes)
const WASM_POINT_BUFFER_OFFSET: u32 = 64 * 1024;
const WASM_CMD_BUFFER_OFFSET: u32 = 128 * 1024;

// Batch sizes
const MAX_POINTS_PER_BATCH: usize = 2; // two points -> one line segment
const MAX_COMMANDS_PER_BATCH: u32 = 512;

// Circle transform constants
const SCALE: f32 = 200.0;
const OFFSET_X: f32 = 400.0;
const OFFSET_Y: f32 = 300.0;

#[repr(C)]
#[derive(Copy, Clone)]
struct Point {
    x: f32,
    y: f32,
}

unsafe extern "system" fn window_proc(
    hwnd: HWND,
    msg: u32,
    wparam: WPARAM,
    lparam: LPARAM,
) -> LRESULT {
    unsafe {
        match msg {
            WM_DESTROY => {
                PostQuitMessage(0);
                LRESULT(0)
            }
            _ => DefWindowProcW(hwnd, msg, wparam, lparam),
        }
    }
}

fn create_window_and_d2d() -> Result<(ID2D1HwndRenderTarget, ID2D1SolidColorBrush)> {
    unsafe {
        let h_instance: HINSTANCE = GetModuleHandleW(None)?.into();
        // Register window class
        let class_name = w!("BenchmarkWindow");
        let wc = WNDCLASSW {
            hCursor: LoadCursorW(None, IDC_ARROW)?,
            hInstance: h_instance,
            lpszClassName: class_name,
            lpfnWndProc: Some(window_proc),
            style: WNDCLASS_STYLES(0),
            ..Default::default()
        };
        RegisterClassW(&wc);

        // Create the window
        let hwnd = CreateWindowExW(
            Default::default(),
            class_name,
            w!("WASM vs Native Benchmark"),
            WS_OVERLAPPEDWINDOW | WS_VISIBLE,
            CW_USEDEFAULT,
            CW_USEDEFAULT,
            800,
            600,
            None,
            None,
            None,
            None,
        )?;

        // Initialize Direct2D factory
        let factory: ID2D1Factory = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, None)?;

        // Create HwndRenderTarget
        let rt_props = D2D1_RENDER_TARGET_PROPERTIES {
            pixelFormat: D2D1_PIXEL_FORMAT {
                format: DXGI_FORMAT_B8G8R8A8_UNORM,
                alphaMode: D2D1_ALPHA_MODE_IGNORE,
            },
            ..Default::default()
        };
        let hwnd_rt_props = D2D1_HWND_RENDER_TARGET_PROPERTIES {
            hwnd,
            pixelSize: Default::default(),
            presentOptions: Default::default(),
        };

        let rt = factory.CreateHwndRenderTarget(&rt_props, &hwnd_rt_props)?;
        let brush = rt.CreateSolidColorBrush(
            &D2D1_COLOR_F {
                r: 0.0,
                g: 0.0,
                b: 0.0,
                a: 1.0,
            },
            None,
        )?;

        Ok((rt, brush))
    }
}

fn main() -> Result<()> {
    // Parse args: if any arg provided -> native, else wasm
    let args: Vec<String> = env::args().collect();
    let is_native = args.len() <= 1;
    let wasm_path = if is_native {
        PathBuf::new()
    } else {
        Path::new(&args[1]).to_path_buf()
    };

    // Create window & D2D render target
    let (rt, brush) = create_window_and_d2d()?;

    // Prepare canvas / Wasm renderer
    let mut wasm_renderer = if is_native {
        None
    } else {
        Some(WasmRenderer::new(&wasm_path)?)
    };

    // Precompute transformed circle path
    let mut circle_path: Vec<Point> = Vec::new();
    for degree in 0..360 {
        let theta = degree as f32 * std::f32::consts::PI / 180.0;
        let x = theta.cos() * SCALE + OFFSET_X;
        let y = theta.sin() * SCALE + OFFSET_Y;
        circle_path.push(Point { x, y });
    }
    // Split into 2-point chunks
    let mut chunks: Vec<[Point; 2]> = Vec::new();
    for i in (0..circle_path.len()).step_by(2) {
        let a = circle_path[i];
        let b = if i + 1 < circle_path.len() {
            circle_path[i + 1]
        } else {
            circle_path[0]
        };
        chunks.push([a, b]);
    }

    // Initialize native or wasm canvas
    if is_native {
        native_renderer::init_canvas();
    }

    // Timing setup
    let mut freq: i64 = 0;
    unsafe { QueryPerformanceFrequency(&mut freq) };
    let mut latencies: Vec<f64> = Vec::with_capacity(RUN_ITERATIONS);

    // Main benchmark loop
    for _ in 0..RUN_ITERATIONS {
        // Throttle to 8ms
        sleep(Duration::from_millis(TICK_INTERVAL_MS));

        // Record t1
        let mut start: i64 = 0;
        unsafe { QueryPerformanceCounter(&mut start) };

        let renderer = &mut wasm_renderer;
        let mut idx = 0;
        // Draw full circle one chunk at a time
        // begin stroke
        if is_native {
            native_renderer::begin_stroke();
        } else if let Some(wr) = renderer.as_mut() {
            wr.begin_stroke()?;
        }

        while idx < chunks.len() {
            let pts = &chunks[idx];
            if is_native {
                native_renderer::append_points(pts);
            } else if let Some(wr) = renderer.as_mut() {
                wr.append_points(pts, WASM_POINT_BUFFER_OFFSET)?;
            }
            // pull and render
            if is_native {
                let cmds = native_renderer::pull_draw_delta(MAX_COMMANDS_PER_BATCH as usize);
                native_renderer::render_commands(&rt, &brush, &cmds);
            } else if let Some(wr) = renderer.as_mut() {
                let cmds = wr.pull_draw_delta(MAX_COMMANDS_PER_BATCH, WASM_CMD_BUFFER_OFFSET)?;
                wr.render_commands(&rt, &brush, &cmds);
            }
            idx += 1;
            sleep(Duration::from_millis(TICK_INTERVAL_MS));
        }

        // Delete and render erase
        if is_native {
            native_renderer::delete_stroke();
            let cmds = native_renderer::pull_draw_delta(MAX_COMMANDS_PER_BATCH as usize);
            native_renderer::render_commands(&rt, &brush, &cmds);
        } else if let Some(wr) = renderer.as_mut() {
            wr.delete_stroke()?;
            let cmds = wr.pull_draw_delta(MAX_COMMANDS_PER_BATCH, WASM_CMD_BUFFER_OFFSET)?;
            wr.render_commands(&rt, &brush, &cmds);
        }

        // Record t2
        let mut end: i64 = 0;
        unsafe { QueryPerformanceCounter(&mut end) };
        let delta = (end - start) as f64 * 1e3 / freq as f64;
        latencies.push(delta);
    }

    // Compute statistics
    latencies.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let count = latencies.len();
    let sum: f64 = latencies.iter().sum();
    let mean = sum / count as f64;
    let median = latencies[count / 2];
    let p1 = latencies[count / 100];
    let p99 = latencies[count * 99 / 100];
    let min = latencies[0];
    let max = latencies[count - 1];

    // Print results
    println!("iterations: {}", count);
    println!("mean:      {:.3} ms", mean);
    println!("median:    {:.3} ms", median);
    println!("p1:        {:.3} ms", p1);
    println!("p99:       {:.3} ms", p99);
    println!("min:       {:.3} ms", min);
    println!("max:       {:.3} ms", max);

    // Cleanup: exit message loop
    unsafe {
        PostQuitMessage(0);
        let mut msg = MSG::default();
        while GetMessageW(&mut msg, None, 0, 0).into() {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    Ok(())
}
