{ nixpkgs, nixpkgs-glibc231 }:
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
      pkgs = import nixpkgs { inherit system; };
      # glibc 2.31 — matches the system libc on Ubuntu 20.04 LTS. Sourced
      # from the nixpkgs snapshot pinned via the `nixpkgs-glibc231` flake
      # input, so the build uses the compiler from that snapshot
      # (gcc 9.3.0) along with the matching patches, configure flags, and
      # hardening defaults.
      glibc231 = (import nixpkgs-glibc231 { inherit system; }).glibc;
    }
  )
