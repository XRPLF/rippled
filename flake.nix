{
  description = "Nix related things for xrpld";
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
    # nixpkgs snapshot (2020-06-30) that shipped glibc 2.31 as the primary
    # version — matches the system libc on Ubuntu 20.04 LTS. Imported
    # manually (flake = false) because this revision predates nixpkgs'
    # own flake.nix.
    nixpkgs-glibc231 = {
      url = "github:NixOS/nixpkgs/9cd98386a38891d1074fc18036b842dc4416f562";
      flake = false;
    };
  };

  outputs =
    { nixpkgs, nixpkgs-glibc231, ... }:
    let
      forEachSystem = import ./nix/utils.nix { inherit nixpkgs nixpkgs-glibc231; };
    in
    {
      devShells = forEachSystem (import ./nix/devshell.nix);
      packages = forEachSystem (import ./nix/ci-env.nix);
      formatter = forEachSystem ({ pkgs, ... }: pkgs.nixfmt);
    };
}
