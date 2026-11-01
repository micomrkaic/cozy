# Pinned wasm toolchain (quality-audit item 6): the PLAYBOOK's Ubuntu
# emscripten recipe, frozen in an image so container resets and distro
# drift (the t64 renames, node's externalized builtins) stop costing a
# re-derivation each time.
#   build the image:  docker build -t cozy-wasm -f tools/wasm.Dockerfile .
#   build the bundle: docker run --rm -u "$(id -u)" -v "$PWD":/src -w /src cozy-wasm make wasm-ubuntu
FROM ubuntu:24.04
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      make emscripten clang-15 lld-15 llvm-15 binaryen \
      nodejs node-acorn node-cjs-module-lexer node-undici node-busboy \
      ca-certificates \
  && rm -rf /var/lib/apt/lists/*
# sanity: the toolchain that cozy's wasm target expects
RUN emcc --version | head -1 && node --version
