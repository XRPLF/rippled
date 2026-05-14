{
  pkgs,
  glibc231,
  ...
}:
let
  inherit (import ./packages.nix { inherit pkgs; }) commonPackages;

  # binutils wrapped to emit binaries that reference glibc 2.31 (dynamic
  # linker path, library search path, RPATH).
  binutils231 = pkgs.wrapBintoolsWith {
    bintools = pkgs.binutils-unwrapped;
    libc = glibc231;
  };

  # Rebuild gcc 15 (specifically libstdc++ / libgcc_s) against glibc 2.31.
  # The override swaps gcc15.cc's bootstrap stdenv for one that uses the
  # existing gcc 15 binary but links against glibc 2.31, so the resulting
  # compiler ships runtime libraries that only reference symbols available
  # in glibc 2.31.
  gcc15CcWithGlibc231 = pkgs.gcc15.cc.override {
    stdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv (
      pkgs.wrapCCWith {
        cc = pkgs.gcc15.cc;
        libc = glibc231;
        bintools = binutils231;
      }
    );
  };

  # cc-wrapper around the rebuilt compiler, pointing at glibc 2.31 headers
  # and libraries. This is what we actually expose to users.
  gcc15WithGlibc231 = pkgs.wrapCCWith {
    cc = gcc15CcWithGlibc231;
    libc = glibc231;
    bintools = binutils231;
  };

in
{
  default = pkgs.buildEnv {
    name = "xrpld-ci-env";
    paths = commonPackages ++ [
      gcc15WithGlibc231
      binutils231
    ];
    pathsToLink = [
      "/bin"
      "/lib"
      "/include"
      "/share"
    ];
  };
}
