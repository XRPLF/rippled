{
  nixpkgs,
  nixpkgs-custom-glibc,
  rust-overlay,
}:
function:
nixpkgs.lib.genAttrs
  [
    "x86_64-linux"
    "aarch64-linux"
    "x86_64-darwin"
    "aarch64-darwin"
  ]
  (
    system:
    function {
      # rust-overlay adds `pkgs.rust-bin`, from which we build the pinned Rust
      # toolchain (see packages.nix). Consumed by both the CI image and dev shell.
      pkgs = import nixpkgs {
        inherit system;
        overlays = [ (import rust-overlay) ];
      };
      # glibc 2.31 — matches the system libc on Ubuntu 20.04 LTS. Sourced
      # from the nixpkgs snapshot pinned via the `nixpkgs-custom-glibc`
      # flake input, so the build uses the compiler from that snapshot
      # (gcc 9.3.0) along with the matching patches, configure flags, and
      # hardening defaults.
      customGlibc = (import nixpkgs-custom-glibc { inherit system; }).glibc;
    }
  )
