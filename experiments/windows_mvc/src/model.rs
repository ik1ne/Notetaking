#[derive(Debug, Clone, Copy)]
pub enum PointerState {
    Released,
    Hovered { x: f32, y: f32 },
    Pressed { x: f32, y: f32 },
}

pub trait Model {
    fn update_hover(&mut self, x: f32, y: f32);
    fn update_press(&mut self, x: f32, y: f32);
    fn release(&mut self);
    fn pointer_state(&self) -> PointerState;
}

pub struct AppState {
    pointer_state: PointerState,
}

impl AppState {
    pub fn new() -> Self {
        Self {
            pointer_state: PointerState::Released,
        }
    }
}

impl Model for AppState {
    fn update_hover(&mut self, x: f32, y: f32) {
        self.pointer_state = PointerState::Hovered { x, y };
    }

    fn update_press(&mut self, x: f32, y: f32) {
        self.pointer_state = PointerState::Pressed { x, y };
    }

    fn release(&mut self) {
        self.pointer_state = PointerState::Released;
    }

    fn pointer_state(&self) -> PointerState {
        self.pointer_state
    }
}
