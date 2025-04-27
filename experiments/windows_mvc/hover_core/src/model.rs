/// Represents the state of the pointer: released, hovering, or pressing.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PointerState {
    Released,
    Hovered { x: f32, y: f32 },
    Pressed { x: f32, y: f32 },
}

/// Model trait defines the behavior of the app's state.
pub trait Model {
    fn update_hover(&mut self, x: f32, y: f32);
    fn update_press(&mut self, x: f32, y: f32);
    fn release(&mut self);
    fn pointer_state(&self) -> PointerState;
}

/// Concrete implementation of the Model.
pub struct AppState {
    pointer_state: PointerState,
}

impl Default for AppState {
    fn default() -> Self {
        Self::new()
    }
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
