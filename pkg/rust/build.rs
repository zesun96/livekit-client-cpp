use std::{env, path::PathBuf};

fn main() {
    for name in [
        "LIVEKIT_CLIENT_CPP_INCLUDE_DIR",
        "LIVEKIT_CLIENT_CPP_LIB_DIR",
        "LIBCLANG_PATH",
    ] {
        println!("cargo:rerun-if-env-changed={name}");
    }
    let include = env::var_os("LIVEKIT_CLIENT_CPP_INCLUDE_DIR")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../include"));
    let header = include.join("livekit/capi/livekit.h");
    println!("cargo:rerun-if-changed={}", header.display());
    let target = env::var("TARGET").expect("Cargo TARGET");
    let bindings = bindgen::Builder::default()
        .header(header.to_str().expect("UTF-8 header path"))
        .clang_arg(format!("--target={target}"))
        .allowlist_function("lk_.*")
        .allowlist_type("lk_.*")
        .allowlist_var("LK_.*")
        .prepend_enum_name(false)
        .layout_tests(true)
        .generate_comments(true)
        .generate()
        .expect("Cannot generate C ABI bindings; install libclang and set LIBCLANG_PATH if needed");
    bindings
        .write_to_file(PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("bindings.rs"))
        .expect("write generated bindings");
    if let Some(dir) = env::var_os("LIVEKIT_CLIENT_CPP_LIB_DIR") {
        println!(
            "cargo:rustc-link-search=native={}",
            PathBuf::from(dir).display()
        );
    }
    println!("cargo:rustc-link-lib=dylib=livekitclient");
}
