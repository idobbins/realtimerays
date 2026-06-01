use std::env;
use std::fs;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").unwrap());
    let spv = out_dir.join("trace.comp.spv");
    let rs = out_dir.join("trace_comp_spv.rs");
    let macos_o = out_dir.join("macos.o");

    println!("cargo:rerun-if-changed=shaders/trace.comp");
    println!("cargo:rerun-if-changed=src/macos.m");
    println!("cargo:rerun-if-changed=build.rs");

    let status = Command::new("glslc")
        .arg("shaders/trace.comp")
        .arg("-o")
        .arg(&spv)
        .status()
        .unwrap();
    if !status.success() {
        panic!("glslc failed");
    }

    let bytes = fs::read(&spv).unwrap();
    if bytes.len() % 4 != 0 {
        panic!("SPIR-V byte length is not u32 aligned");
    }

    let mut out = String::new();
    out.push_str("pub static TRACE_COMP_SPV: [u32; ");
    out.push_str(&(bytes.len() / 4).to_string());
    out.push_str("] = [");
    for chunk in bytes.chunks_exact(4) {
        let word = u32::from_le_bytes([chunk[0], chunk[1], chunk[2], chunk[3]]);
        out.push_str(&format!("0x{word:08x},"));
    }
    out.push_str("];\n");
    fs::write(rs, out).unwrap();

    let status = Command::new("cc")
        .arg("-x")
        .arg("objective-c")
        .arg("-std=c99")
        .arg("-Wall")
        .arg("-Wextra")
        .arg("-pedantic")
        .arg("-O2")
        .arg("-c")
        .arg("src/macos.m")
        .arg("-o")
        .arg(&macos_o)
        .status()
        .unwrap();
    if !status.success() {
        panic!("Objective-C compile failed");
    }

    println!("cargo:rustc-link-arg={}", macos_o.display());
    println!("cargo:rustc-link-search=native=/usr/local/lib");
    println!("cargo:rustc-link-arg=-Wl,-rpath,/usr/local/lib");
    println!("cargo:rustc-link-lib=dylib=vulkan");
    println!("cargo:rustc-link-lib=framework=AppKit");
    println!("cargo:rustc-link-lib=framework=QuartzCore");
}
