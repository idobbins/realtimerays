mod macos;
mod vk;
mod vulkan;

use realtimerays::scene;

fn main() {
    let code = unsafe { entry() };
    std::process::exit(code);
}

unsafe fn entry() -> i32 {
    macos::rtrInitWindow(1280, 720, b"realtimerays\0".as_ptr().cast());
    if vulkan::init(macos::rtrWindowSurface()).is_err() {
        return 1;
    }

    while macos::rtrPumpEventsOnce() == 0 {
        vulkan::frame();
    }

    0
}
