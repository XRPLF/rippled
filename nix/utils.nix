{ nixpkgs }:
{
  forEachSystem =
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
          inherit system;
          pkgs = import nixpkgs { inherit system; };
        }
      );
}
