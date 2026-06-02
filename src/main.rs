mod macos;
mod vk;
mod vulkan;

use std::io;

fn main() {
    let code = unsafe { entry() };
    std::process::exit(code);
}

fn u32_arg(args: &[String], index: usize, fallback: u32) -> u32 {
    args.get(index)
        .and_then(|value| value.parse::<u32>().ok())
        .filter(|value| *value != 0)
        .unwrap_or(fallback)
}

unsafe fn entry() -> i32 {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(String::as_str) == Some("--xpost-raw") {
        let width = u32_arg(&args, 2, 1920);
        let height = u32_arg(&args, 3, 1080);
        let frames = u32_arg(&args, 4, 900);
        let fps = u32_arg(&args, 5, 30);
        let stdout = io::stdout();
        let mut stdout = stdout.lock();
        return match vulkan::write_frames(&mut stdout, width, height, frames, fps) {
            Ok(_) => 0,
            Err(err) => {
                eprintln!("xpost render failed: {:?}", err);
                1
            }
        };
    }

    macos::rtrInitWindow(1920, 1080, b"realtimerays\0".as_ptr().cast());
    if vulkan::init(macos::rtrWindowSurface()).is_err() {
        return 1;
    }

    while macos::rtrPumpEventsOnce() == 0 {
        vulkan::frame();
    }

    0
}
