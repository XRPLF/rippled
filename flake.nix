{
  description = "Nix related things for xrpld";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    # nixpkgs snapshot (the final nixos-22.05 commit, 2023-05-31) that
    # shipped glibc 2.34 as the primary version — matches the system libc on
    # RHEL 9. No Ubuntu LTS ships 2.34, so the oldest supported Ubuntu is
    # 22.04 LTS (glibc 2.35). 2.34 is the first release where an exception thrown from a
    # pthread_once / std::call_once initializer resets the once-flag instead
    # of hanging every later caller (glibc BZ #18435). Imported as a plain
    # source tree (flake = false): only `glibc` is taken from it, never its
    # flake outputs.
    nixpkgs-custom-glibc = {
      url = "github:NixOS/nixpkgs/380be19fbd2d9079f677978361792cb25e8a3635";
      flake = false;
    };
    # Pinned Rust toolchains, delivered from the Nix store. Lets the Nix CI
    # image and dev shell honour the single `rust-toolchain.toml` pin (shared
    # with the rustup-based non-Nix runners) while staying hermetic — the
    # toolchain lands in the image's Nix closure and is locked by flake.lock.
    rust-overlay = {
      url = "github:oxalica/rust-overlay";
      inputs.nixpkgs.follows = "nixpkgs";
    };
  };

  outputs =
    {
      nixpkgs,
      nixpkgs-custom-glibc,
      rust-overlay,
      ...
    }:
    let
      forEachSystem = import ./nix/utils.nix { inherit nixpkgs nixpkgs-custom-glibc rust-overlay; };
    in
    {
      devShells = forEachSystem (import ./nix/devshell.nix);
      packages = forEachSystem (import ./nix/ci-env.nix);
      formatter = forEachSystem ({ pkgs, ... }: pkgs.nixfmt);
    };
}
