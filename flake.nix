{
  description = "Nix related things for xrpld";
  inputs = {
    nixpkgs.url = "nixpkgs/nixos-unstable";
  };

  outputs =
    { nixpkgs, ... }:
    let
      forEachSystem = (import ./nix/utils.nix { inherit nixpkgs; }).forEachSystem;
    in
    {
      devShells = forEachSystem (import ./nix/devshell.nix);
      formatter = forEachSystem ({ pkgs, ... }: pkgs.nixfmt);
    };
}
