use crate::model::Model;

/// Controller trait defines how input events modify the model.
pub trait Controller {
    fn on_pointer_update(&mut self, x: f32, y: f32, in_contact: bool);
    fn on_pointer_leave(&mut self);
}

/// Concrete Controller tied to a Model.
pub struct AppController<M: Model> {
    pub model: M,
}

impl<M: Model> AppController<M> {
    pub fn new(model: M) -> Self {
        Self { model }
    }
}

impl<M: Model> Controller for AppController<M> {
    fn on_pointer_update(&mut self, x: f32, y: f32, in_contact: bool) {
        if in_contact {
            self.model.update_press(x, y);
        } else {
            self.model.update_hover(x, y);
        }
    }

    fn on_pointer_leave(&mut self) {
        self.model.release();
    }
}
