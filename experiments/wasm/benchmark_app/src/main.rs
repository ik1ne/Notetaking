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
const RUN_ITERATIONS: usize = 1000;
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
    match msg {
        WM_DESTROY => {
            PostQuitMessage(0);
            LRESULT(0)
        }
        WM_SIZE => {
            // Handle resize if needed
            LRESULT(0)
        }
        _ => DefWindowProcW(hwnd, msg, wparam, lparam),
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
    // Parse args: if no extra args -> native, else wasm
    let args: Vec<String> = env::args().collect();
    let is_native = args.len() <= 1;
    let wasm_path = if is_native {
        PathBuf::new()
    } else {
        PathBuf::from(
            r"C:\Users\ik1ne\Sources\Notetaking\experiments\wasm\stroke_renderer\target\wasm32-unknown-unknown\release\stroke_renderer.wasm",
        )
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
        circle_path.push(Point {
            x: theta.cos() * SCALE + OFFSET_X,
            y: theta.sin() * SCALE + OFFSET_Y,
        });
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
        // Pump Windows messages
        let mut msg = MSG::default();
        unsafe {
            while PeekMessageW(&mut msg, None, 0, 0, PM_REMOVE).as_bool() {
                if msg.message == WM_QUIT {
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }

        // Record iteration start
        let mut iter_start: i64 = 0;
        unsafe { QueryPerformanceCounter(&mut iter_start) };

        // Track total sleep time this iter
        let mut total_sleep_ms: f64 = 0.0;

        // Begin stroke
        if is_native {
            native_renderer::begin_stroke();
        } else if let Some(wr) = wasm_renderer.as_mut() {
            wr.begin_stroke()?;
        }

        // Draw each chunk, throttled to ≥ 8 ms
        for chunk in &chunks {
            // Chunk timing start
            let mut chunk_t1: i64 = 0;
            unsafe { QueryPerformanceCounter(&mut chunk_t1) };

            // Append & render chunk
            if is_native {
                native_renderer::append_points(chunk);
                let cmds = native_renderer::pull_draw_delta(MAX_COMMANDS_PER_BATCH as usize);
                native_renderer::render_commands(&rt, &brush, &cmds);
            } else if let Some(wr) = wasm_renderer.as_mut() {
                wr.append_points(chunk, WASM_POINT_BUFFER_OFFSET)?;
                let cmds = wr.pull_draw_delta(MAX_COMMANDS_PER_BATCH, WASM_CMD_BUFFER_OFFSET)?;
                wr.render_commands(&rt, &brush, &cmds);
            }

            // Chunk timing end
            let mut chunk_t2: i64 = 0;
            unsafe { QueryPerformanceCounter(&mut chunk_t2) };
            let elapsed = (chunk_t2 - chunk_t1) as f64 * 1e3 / freq as f64;

            // Sleep if work < 8 ms, and accumulate
            if elapsed < TICK_INTERVAL_MS as f64 {
                let to_sleep_ms = (TICK_INTERVAL_MS as f64 - elapsed).max(0.0);
                total_sleep_ms += to_sleep_ms;
                sleep(Duration::from_secs_f64(to_sleep_ms / 1000.0));
            }
        }

        // Delete stroke and render erase
        if is_native {
            native_renderer::delete_stroke();
            let cmds = native_renderer::pull_draw_delta(MAX_COMMANDS_PER_BATCH as usize);
            native_renderer::render_commands(&rt, &brush, &cmds);
        } else if let Some(wr) = wasm_renderer.as_mut() {
            wr.delete_stroke()?;
            let cmds = wr.pull_draw_delta(MAX_COMMANDS_PER_BATCH, WASM_CMD_BUFFER_OFFSET)?;
            wr.render_commands(&rt, &brush, &cmds);
        }

        // Record iteration end
        let mut iter_end: i64 = 0;
        unsafe { QueryPerformanceCounter(&mut iter_end) };
        let raw_duration = (iter_end - iter_start) as f64 * 1e3 / freq as f64;
        let effective_duration = raw_duration - total_sleep_ms;
        latencies.push(effective_duration);
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
    println!("mode: {}", if is_native { "native" } else { "wasm" });
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
