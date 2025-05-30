use std::path::Path;
use wasmtime::{Caller, Engine, Linker, Memory, Module, Store, TypedFunc};
use windows::Win32::Graphics::Direct2D::{ID2D1HwndRenderTarget, ID2D1SolidColorBrush};
use windows_numerics::Vector2;

/// A 2D point for rendering (copy of host's Point)
#[repr(C)]
#[derive(Copy, Clone)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

/// Draw command struct as returned by WASM
#[repr(C)]
#[derive(Copy, Clone)]
pub struct DrawCmd {
    pub stroke_id: u32,
    pub x1: f32,
    pub y1: f32,
    pub x2: f32,
    pub y2: f32,
}

pub struct WasmRenderer {
    store: Store<()>,
    memory: Memory,
    begin_fn: TypedFunc<(), ()>,
    append_fn: TypedFunc<(u32, u32), ()>,
    delete_fn: TypedFunc<(), ()>,
    pull_fn: TypedFunc<(u32, u32), u32>,
    wasm_mem_size: usize,
}

impl WasmRenderer {
    /// Load the wasm module from `wasm_path` and bind imports
    pub fn new(wasm_path: &Path) -> anyhow::Result<Self> {
        // Initialize engine & module
        let engine = Engine::default();
        let module = Module::from_file(&engine, wasm_path)?;
        let mut linker = Linker::new(&engine);

        // Provide host draw_line import that does nothing (we batch pull deltas)
        linker.func_wrap(
            "env",
            "draw_line",
            |_caller: Caller<'_, ()>, _: f32, _: f32, _: f32, _: f32| Ok(()),
        )?;

        // Create store and instantiate without WASI
        let mut store = Store::new(&engine, ());
        let instance = linker.instantiate(&mut store, &module)?;

        // Extract exports
        let memory = instance
            .get_memory(&mut store, "memory")
            .expect("memory export");
        let begin_fn = instance.get_typed_func::<(), ()>(&mut store, "begin_stroke")?;
        let append_fn = instance.get_typed_func::<(u32, u32), ()>(&mut store, "append_points")?;
        let delete_fn = instance.get_typed_func::<(), ()>(&mut store, "delete_stroke")?;
        let pull_fn = instance.get_typed_func::<(u32, u32), u32>(&mut store, "pull_draw_delta")?;

        let wasm_mem_size = memory.data_size(&store);
        Ok(Self {
            store,
            memory,
            begin_fn,
            append_fn,
            delete_fn,
            pull_fn,
            wasm_mem_size,
        })
    }

    /// Begin a new stroke in WASM
    pub fn begin_stroke(&mut self) -> anyhow::Result<()> {
        self.begin_fn.call(&mut self.store, ())?;
        Ok(())
    }

    /// Append batched points into wasm memory and notify module
    pub fn append_points(&mut self, pts: &[Point], wasm_offset: u32) -> anyhow::Result<()> {
        let byte_offset = wasm_offset as usize * size_of::<Point>();
        let bytes =
            unsafe { core::slice::from_raw_parts(pts.as_ptr() as *const u8, size_of_val(pts)) };
        let mem = self.memory.data_mut(&mut self.store);
        mem[byte_offset..byte_offset + bytes.len()].copy_from_slice(bytes);
        self.append_fn
            .call(&mut self.store, (wasm_offset, pts.len() as u32))?;
        Ok(())
    }

    /// Delete stroke in WASM
    pub fn delete_stroke(&mut self) -> anyhow::Result<()> {
        self.delete_fn.call(&mut self.store, ())?;
        Ok(())
    }

    /// Pull delta draw commands from WASM
    pub fn pull_draw_delta(&mut self, max: u32, wasm_cmd_ptr: u32) -> anyhow::Result<Vec<DrawCmd>> {
        let count = self.pull_fn.call(&mut self.store, (wasm_cmd_ptr, max))?;
        let data = self.memory.data(&self.store);
        let base = unsafe { data.as_ptr().add(wasm_cmd_ptr as usize) as *const DrawCmd };
        let cmds = unsafe { std::slice::from_raw_parts(base, count as usize) }.to_vec();
        Ok(cmds)
    }

    /// Render commands via host D2D
    pub fn render_commands(
        &self,
        rt: &ID2D1HwndRenderTarget,
        brush: &ID2D1SolidColorBrush,
        cmds: &[DrawCmd],
    ) {
        unsafe {
            rt.BeginDraw();
            for cmd in cmds {
                rt.DrawLine(
                    Vector2::new(cmd.x1, cmd.y1),
                    Vector2::new(cmd.x2, cmd.y2),
                    brush,
                    1.0,
                    None,
                );
            }
            rt.EndDraw(None, None).unwrap();
        }
    }
}
