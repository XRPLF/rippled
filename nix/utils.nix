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
      # glibc 2.34 — matches the system libc on RHEL 9; the oldest supported
      # Ubuntu is 22.04 LTS (glibc 2.35). Sourced from the
      # nixpkgs snapshot pinned via the `nixpkgs-custom-glibc` flake input,
      # so the build uses the compiler from that snapshot (gcc 11.3.0) along
      # with the matching patches, configure flags, and hardening defaults.
      customGlibc = (import nixpkgs-custom-glibc { inherit system; }).glibc;
    }
  )
