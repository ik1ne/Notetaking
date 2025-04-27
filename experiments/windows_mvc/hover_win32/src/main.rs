mod win;

fn main() -> anyhow::Result<()> {
    unsafe { win::run_message_loop() }
}
