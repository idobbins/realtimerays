mod macos;
mod vulkan;

fn main() {
    let code = unsafe { real_main() };
    std::process::exit(code);
}

unsafe fn real_main() -> i32 {
    macos::rtrInitWindow(1280, 720, b"realtimerays\0".as_ptr().cast());
    if vulkan::init(macos::rtrWindowSurface()).is_err() {
        return 1;
    }

    while macos::rtrPumpEventsOnce() == 0 {
        vulkan::frame();
    }

    0
}
