use std::collections::HashMap;
use std::ops::DerefMut;
use std::slice;
use std::sync::{LazyLock, Mutex};

/// POD for a 2D point
#[repr(C)]
#[derive(Copy, Clone)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

/// Draw command emitted by the module
#[repr(C)]
#[derive(Copy, Clone)]
pub struct DrawCmd {
    pub stroke_id: u32,
    pub x1: f32,
    pub y1: f32,
    pub x2: f32,
    pub y2: f32,
}

struct Stroke {
    #[expect(unused)]
    id: u32,
    points: Vec<Point>,
}

struct Canvas {
    next_id: u32,
    strokes: HashMap<u32, Stroke>,
    delta_queue: Vec<DrawCmd>,
}

// Lazily initialized, thread-safe singleton canvas
static CANVAS: LazyLock<Mutex<Canvas>> = LazyLock::new(|| {
    Mutex::new(Canvas {
        next_id: 1,
        strokes: HashMap::new(),
        delta_queue: Vec::new(),
    })
});

/// Initialize or reset the canvas state
#[unsafe(no_mangle)]
pub extern "C" fn init_canvas() {
    let mut canvas = CANVAS.lock().unwrap();
    canvas.next_id = 1;
    canvas.strokes.clear();
    canvas.delta_queue.clear();
}

/// Begin a new stroke (ID managed internally)
#[unsafe(no_mangle)]
pub extern "C" fn begin_stroke() {
    let mut canvas = CANVAS.lock().unwrap();
    let id = canvas.next_id;
    canvas.next_id += 1;
    canvas.strokes.insert(
        id,
        Stroke {
            id,
            points: Vec::with_capacity(64),
        },
    );
}

/// Append a batch of points at `ptr`/`len` elements and enqueue draw deltas
#[unsafe(no_mangle)]
pub extern "C" fn append_points(ptr: u32, len: u32) {
    let mut canvas = CANVAS.lock().unwrap();

    let canvas = canvas.deref_mut();

    // Assume only one active stroke: the one with highest ID
    let &stroke_id = canvas.strokes.keys().max().unwrap();
    let stroke = canvas.strokes.get_mut(&stroke_id).unwrap();
    let base = ptr as usize as *const Point;
    let pts = unsafe { slice::from_raw_parts(base, len as usize) };
    for window in pts.windows(2) {
        let a = window[0];
        let b = window[1];
        stroke.points.push(b);
        canvas.delta_queue.push(DrawCmd {
            stroke_id,
            x1: a.x,
            y1: a.y,
            x2: b.x,
            y2: b.y,
        });
    }
}

/// Delete the most recent stroke and enqueue an erase command
#[unsafe(no_mangle)]
pub extern "C" fn delete_stroke() {
    let mut canvas = CANVAS.lock().unwrap();
    if let Some((&id, _)) = canvas.strokes.iter().max_by_key(|(id, _)| **id) {
        canvas.strokes.remove(&id);
        // Enqueue a "clear" command for this stroke
        canvas.delta_queue.push(DrawCmd {
            stroke_id: id,
            x1: 0.0,
            y1: 0.0,
            x2: 0.0,
            y2: 0.0,
        });
    }
}

/// Pull up to `max` draw commands into host memory at `cmd_ptr`
#[unsafe(no_mangle)]
pub extern "C" fn pull_draw_delta(cmd_ptr: u32, max: u32) -> u32 {
    let mut canvas = CANVAS.lock().unwrap();
    let count = canvas.delta_queue.len().min(max as usize);
    let dst = cmd_ptr as usize as *mut DrawCmd;
    for i in 0..count {
        unsafe {
            *dst.add(i) = canvas.delta_queue[i];
        }
    }
    canvas.delta_queue.drain(0..count);
    count as u32
}
