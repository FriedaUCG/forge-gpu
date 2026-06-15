# Forge GPU SDL_GPU WebGPU Demo

This repository is an altered distillation of the GPU lessons from
https://github.com/Nebulavenus/forge-gpu. It demonstrates the same lesson set
running through SDL_GPU on native backends and through an SDL WebGPU backend in
the browser.

The demo depends on the matching FriedaUCG forks of SDL and SDL_shadercross.
SDL_shadercross is used at build time to generate shader headers from the HLSL
sources in `shaders/source`; it is not a runtime dependency.

## Repository Layout

- `src/`: SDL_GPU demo code and lesson implementations.
- `shaders/source/`: HLSL sources used to generate native and WebGPU shader
  fixtures during the build.
- `assets/`: Runtime assets used by the lessons.
- `third_party/`: Small vendored runtime dependencies used by the demo.
- `external/`: SDL and SDL_shadercross dependencies.

## Local Build

Place matching SDL and SDL_shadercross checkouts under `external/SDL` and
`external/SDL_shadercross`. Published snapshots use submodules, so initialize
those two checkouts when `.gitmodules` is present:

```sh
test -f .gitmodules && git submodule update --init external/SDL external/SDL_shadercross
(cd external/SDL_shadercross && ./external/download.sh)
```

You need CMake, Ninja, Python 3, Git, a platform C/C++ toolchain, and Go for
SDL_shadercross's bundled Tint build. On Linux, install SDL's normal Linux
build dependencies for desktop window support. For headless/offscreen Vulkan
validation, configure SDL with `-DSDL_UNIX_CONSOLE_BUILD=ON -DSDL_X11=OFF
-DSDL_WAYLAND=OFF`.

Build SDL and SDL_shadercross first. One same-prefix native setup is:

```sh
cmake -S external/SDL -B build/SDL -G Ninja -DSDL_SHARED=ON -DSDL_STATIC=ON -DSDL_INSTALL=ON -DCMAKE_INSTALL_PREFIX=$PWD/build/deps
cmake --build build/SDL
cmake --install build/SDL

cmake -P external/SDL_shadercross/build-scripts/download-prebuilt-DirectXShaderCompiler.cmake
cmake -S external/SDL_shadercross -B external/SDL_shadercross/build -G Ninja -DCMAKE_PREFIX_PATH=$PWD/build/deps -DSDLSHADERCROSS_VENDORED=ON -DSDLSHADERCROSS_DXC_PROVIDER=package -DDirectXShaderCompiler_ROOT=$PWD/external/SDL_shadercross/external/DirectXShaderCompiler-binaries -DSDLSHADERCROSS_BUNDLED_TINT=ON -DSDLSHADERCROSS_CLI=ON
cmake --build external/SDL_shadercross/build
```

On macOS, Microsoft does not publish prebuilt DXC packages; skip the
`download-prebuilt-DirectXShaderCompiler.cmake` step and use
`-DSDLSHADERCROSS_DXC_PROVIDER=source` instead.

Then configure and build a native target:

```sh
cmake -S . -B build/native -G Ninja -DCMAKE_PREFIX_PATH=$PWD/build/deps
cmake --build build/native
```

If the shader tools are not in the default SDL_shadercross build or install
locations, pass `FORGE_GPU_SHADERCROSS_EXECUTABLE` and
`FORGE_GPU_TINT_EXECUTABLE` to CMake. If their runtime libraries are outside
the default `build/deps` prefix, pass `FORGE_GPU_SHADER_TOOL_RUNTIME_DIRS`.

Run from the repository root so the default `assets` path resolves:

```sh
./build/native/forge-gpu-demo --gpu metal --scene 01
```

Use `--gpu vulkan` on Linux and `--gpu direct3d12` on Windows. The command
examples use POSIX shell syntax; on Windows, run from a Visual Studio developer
shell and use the corresponding path syntax. Windows D3D12 presentation may
fail from service or SSH sessions; run the demo from an interactive desktop
session.

For browser builds, configure with Emscripten/Emdawn and an SDL WebGPU build
available through `external/SDL` or an Emscripten-built `SDL3_DIR`. The browser
build still uses host SDL_shadercross tools to generate shaders, so run the
SDL_shadercross setup above first or pass `FORGE_GPU_SHADERCROSS_EXECUTABLE`
and `FORGE_GPU_TINT_EXECUTABLE` explicitly.

For a polished browser package suitable for itch.io, use the release shell and
package target:

```sh
emcmake cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DFORGE_GPU_WEB_RELEASE=ON
cmake --build build/web-release --target forge-gpu-demo-itch-zip
```

The package target writes `build/web-release/forge-gpu-demo-itch.zip` with a
top-level `index.html` and a gzip-compressed `.data` file. itch.io documents
gzip detection for compressed files and requires HTML5 ZIP uploads to contain
`index.html` while staying within its extracted file limits:
https://itch.io/docs/creators/html5. If you serve the package outside itch.io,
the server must send `Content-Encoding: gzip` for the `.data` file.

## Shader Generation

Generated lesson shader headers are build artifacts. They are intentionally not
checked in. CMake runs `tools/generate-shader-header.py` with SDL_shadercross
and Tint to produce `generated/shaders/generated/*.h` inside the build
directory, plus the generated resource-layout sidecars used by the demo.

## Attribution

The original Forge GPU lesson work is from
https://github.com/Nebulavenus/forge-gpu. SDL and SDL_shadercross are separate
dependencies under their own licenses. See `THIRD_PARTY_NOTICES.md` and the
asset license files for third-party notices.
