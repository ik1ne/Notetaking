use std::collections::{HashMap, VecDeque};
use std::ops::DerefMut;
use std::sync::{LazyLock, Mutex};
use windows::Win32::Graphics::Direct2D::{ID2D1HwndRenderTarget, ID2D1SolidColorBrush};
use windows_numerics::Vector2;

/// A 2D point for rendering
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct Point {
    pub x: f32,
    pub y: f32,
}

/// A draw command (line segment)
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct DrawCmd {
    pub stroke_id: u32,
    pub x1: f32,
    pub y1: f32,
    pub x2: f32,
    pub y2: f32,
}

struct Stroke {
    id: u32,
    points: Vec<Point>,
}

struct Canvas {
    next_id: u32,
    strokes: HashMap<u32, Stroke>,
    delta_queue: VecDeque<DrawCmd>,
}

// Thread-safe, lazily initialized canvas state
static CANVAS: LazyLock<Mutex<Canvas>> = LazyLock::new(|| {
    Mutex::new(Canvas {
        next_id: 1,
        strokes: HashMap::new(),
        delta_queue: VecDeque::new(),
    })
});

/// Initialize or reset the canvas state for native rendering
pub fn init_canvas() {
    let mut canvas = CANVAS.lock().unwrap();
    *canvas = Canvas {
        next_id: 1,
        strokes: HashMap::new(),
        delta_queue: VecDeque::new(),
    };
}

/// Begin a new stroke (assigns an internal ID)
pub fn begin_stroke() {
    let mut canvas = CANVAS.lock().unwrap();
    let id = canvas.next_id;
    canvas.next_id += 1;
    canvas.strokes.insert(
        id,
        Stroke {
            id,
            points: Vec::new(),
        },
    );
}

/// Append a batch of points (one stroke at a time)
pub fn append_points(points: &[Point]) {
    let mut canvas = CANVAS.lock().unwrap();
    let mut canvas = canvas.deref_mut();
    if let Some((&id, _)) = canvas.strokes.iter().max_by_key(|(id, _)| **id) {
        let stroke = canvas.strokes.get_mut(&id).unwrap();
        for w in points.windows(2) {
            let a = w[0];
            let b = w[1];
            stroke.points.push(b);
            canvas.delta_queue.push_back(DrawCmd {
                stroke_id: id,
                x1: a.x,
                y1: a.y,
                x2: b.x,
                y2: b.y,
            });
        }
    }
}

/// Delete the most recent stroke
pub fn delete_stroke() {
    let mut canvas = CANVAS.lock().unwrap();
    if let Some((&id, _)) = canvas.strokes.iter().max_by_key(|(id, _)| **id) {
        canvas.strokes.remove(&id);
        // enqueue erase command
        canvas.delta_queue.push_back(DrawCmd {
            stroke_id: id,
            x1: 0.0,
            y1: 0.0,
            x2: 0.0,
            y2: 0.0,
        });
    }
}

/// Pull up to `max` draw commands for rendering
pub fn pull_draw_delta(max: usize) -> Vec<DrawCmd> {
    let mut cmds = Vec::new();
    let mut canvas = CANVAS.lock().unwrap();
    for _ in 0..max {
        if let Some(cmd) = canvas.delta_queue.pop_front() {
            cmds.push(cmd);
        } else {
            break;
        }
    }
    cmds
}

/// Render a batch of draw commands into the given render target
pub fn render_commands(rt: &ID2D1HwndRenderTarget, brush: &ID2D1SolidColorBrush, cmds: &[DrawCmd]) {
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
