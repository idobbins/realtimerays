use std::ffi::{c_char, c_void};

unsafe extern "C" {
    pub fn rtrInitWindow(width: u32, height: u32, title: *const c_char) -> i32;
    pub fn rtrWindowSurface() -> *mut c_void;
    pub fn rtrPumpEventsOnce() -> i32;
    pub fn rtrWindowMouse(x: *mut f32, y: *mut f32);
}
