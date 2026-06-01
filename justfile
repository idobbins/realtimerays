cc := env("CC", "cc")
cflags := env("CFLAGS", "-std=c99 -Wall -Wextra -pedantic -O2")
ldflags := env("LDFLAGS", "-framework AppKit -framework QuartzCore")

build_dir := "build"
bin := build_dir / "realtimerays"
shader_src := "shaders/trace.comp"
spv := build_dir / "trace.comp.spv"
shader_header := build_dir / "trace_comp_spv.h"
main_obj := build_dir / "main.o"
macos_obj := build_dir / "macos.o"

default: run

build: _dirs _shader _compile-commands _objects
    {{ cc }} -o {{ bin }} {{ main_obj }} {{ macos_obj }} {{ ldflags }} $(just _vulkan-ldflags)

compile-commands: _dirs _shader
    @just _compile-commands

run: build
    @just _run

clean:
    rm -rf {{ build_dir }}

[private]
_dirs:
    mkdir -p {{ build_dir }}

[private]
_compile-commands:
    #!/usr/bin/env sh
    dir="$(pwd)"
    c_cmd="{{ cc }} {{ cflags }} $(just _vulkan-cflags) -I{{ build_dir }} -c main.c -o {{ main_obj }}"
    vulkan_cmd="{{ cc }} {{ cflags }} $(just _vulkan-cflags) -I{{ build_dir }} -c vulkan.c -o {{ build_dir }}/vulkan.o"
    objc_cmd="{{ cc }} -x objective-c {{ cflags }} -c macos.m -o {{ macos_obj }}"
    esc_dir="$(printf '%s' "$dir" | sed 's/\\/\\\\/g; s/"/\\"/g')"
    esc_c_cmd="$(printf '%s' "$c_cmd" | sed 's/\\/\\\\/g; s/"/\\"/g')"
    esc_vulkan_cmd="$(printf '%s' "$vulkan_cmd" | sed 's/\\/\\\\/g; s/"/\\"/g')"
    esc_objc_cmd="$(printf '%s' "$objc_cmd" | sed 's/\\/\\\\/g; s/"/\\"/g')"
    {
        printf '[\n'
        printf '  {\n'
        printf '    "directory": "%s",\n' "$esc_dir"
        printf '    "command": "%s",\n' "$esc_c_cmd"
        printf '    "file": "%s/main.c"\n' "$esc_dir"
        printf '  },\n'
        printf '  {\n'
        printf '    "directory": "%s",\n' "$esc_dir"
        printf '    "command": "%s",\n' "$esc_vulkan_cmd"
        printf '    "file": "%s/vulkan.c"\n' "$esc_dir"
        printf '  },\n'
        printf '  {\n'
        printf '    "directory": "%s",\n' "$esc_dir"
        printf '    "command": "%s",\n' "$esc_objc_cmd"
        printf '    "file": "%s/macos.m"\n' "$esc_dir"
        printf '  }\n'
        printf ']\n'
    } > compile_commands.json

[private]
_shader: _dirs
    "$(just _glslc)" {{ shader_src }} -o {{ spv }}
    xxd -i -n traceCompSpv {{ spv }} {{ shader_header }}

[private]
_objects: _shader
    {{ cc }} {{ cflags }} $(just _vulkan-cflags) -I{{ build_dir }} -c main.c -o {{ main_obj }}
    {{ cc }} -x objective-c {{ cflags }} -c macos.m -o {{ macos_obj }}

[private]
_run:
    #!/usr/bin/env sh
    icd="$(just _sdk-moltenvk-icd)"
    if [ -z "${VK_ICD_FILENAMES:-}" ] && [ -n "$icd" ]; then
        VK_ICD_FILENAMES="$icd" ./{{ bin }}
    else
        ./{{ bin }}
    fi

[private]
_glslc:
    #!/usr/bin/env sh
    if [ -n "${GLSLC:-}" ]; then
        printf '%s' "$GLSLC"
    elif command -v glslc >/dev/null 2>&1; then
        command -v glslc
    elif [ -n "${VULKAN_SDK:-}" ] && [ -x "${VULKAN_SDK}/bin/glslc" ]; then
        printf '%s/bin/glslc' "$VULKAN_SDK"
    else
        printf 'glslc'
    fi

[private]
_vulkan-cflags:
    #!/usr/bin/env sh
    if [ -n "${VULKAN_CFLAGS:-}" ]; then
        printf '%s' "$VULKAN_CFLAGS"
    elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
        pkg-config --cflags vulkan
    elif [ -n "${VULKAN_SDK:-}" ]; then
        printf '%s' "-I${VULKAN_SDK}/include"
    elif [ -d /opt/homebrew/opt/vulkan-headers/include ]; then
        printf '%s' "-I/opt/homebrew/opt/vulkan-headers/include"
    elif [ -d /usr/local/include/vulkan ]; then
        printf '%s' "-I/usr/local/include"
    fi

[private]
_vulkan-ldflags:
    #!/usr/bin/env sh
    if [ -n "${VULKAN_LDFLAGS:-}" ]; then
        printf '%s' "$VULKAN_LDFLAGS"
    elif command -v pkg-config >/dev/null 2>&1 && pkg-config --exists vulkan; then
        libdir="$(pkg-config --variable=libdir vulkan)"
        printf '%s' "$(pkg-config --libs vulkan) -Wl,-rpath,${libdir}"
    elif [ -n "${VULKAN_SDK:-}" ]; then
        printf '%s' "-L${VULKAN_SDK}/lib -Wl,-rpath,${VULKAN_SDK}/lib -lvulkan"
    elif [ -d /opt/homebrew/opt/vulkan-loader/lib ]; then
        printf '%s' "-L/opt/homebrew/opt/vulkan-loader/lib -Wl,-rpath,/opt/homebrew/opt/vulkan-loader/lib -lvulkan"
    elif [ -d /opt/homebrew/opt/molten-vk/lib ]; then
        printf '%s' "-L/opt/homebrew/opt/molten-vk/lib -Wl,-rpath,/opt/homebrew/opt/molten-vk/lib -lMoltenVK"
    elif [ -f /usr/local/lib/libvulkan.dylib ]; then
        printf '%s' "-L/usr/local/lib -Wl,-rpath,/usr/local/lib -lvulkan"
    else
        printf '%s' "-lvulkan"
    fi

[private]
_sdk-moltenvk-icd:
    #!/usr/bin/env sh
    if [ -n "${VULKAN_SDK:-}" ] && [ -f "${VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json" ]; then
        printf '%s' "${VULKAN_SDK}/share/vulkan/icd.d/MoltenVK_icd.json"
    fi
