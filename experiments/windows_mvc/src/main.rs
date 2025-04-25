mod controller;
mod model;
mod win;

fn main() {
    unsafe {
        win::run_message_loop();
    }
}
