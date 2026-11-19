{
  description = "Nix related things for xrpld";
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    # nixpkgs snapshot (final nixos-22.05 commit, 2023-05-31) whose primary
    # glibc is 2.34 — see nix/docker/README.md. Imported as a plain source
    # tree (flake = false): only `glibc` is taken from it.
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
