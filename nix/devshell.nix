{ pkgs, ... }:
let
  common_packages = with pkgs; [
    cmake
    ninja
    python311
    conan
    pre-commit
    cspell
    gcovr
    ccache
    pkg-config
    llvmPackages_18.clang-tools
  ];
  default_compiler = if pkgs.stdenv.isDarwin then "apple-clang" else "gcc";
  strToCompilerEnv =
    compiler:
    (
      if compiler == "gcc" then
        pkgs.gcc14Stdenv
      else if compiler == "clang" then
        pkgs.llvmPackages_18.stdenv
      else if compiler == "apple-clang" then
        pkgs.stdenvNoCC
      else
        throw "Invalid compiler: ${compiler}. Must be one of: gcc, clang, none"
    );

  # Helper function to create a shell with a specific compiler
  makeShell =
    {
      compiler ? default_compiler,
    }:
    let
      compilerStdEnv = strToCompilerEnv compiler;

      compilerName = if compiler == "apple-clang" then "clang" else compiler;

      shellAttrs = {
        packages = common_packages;

        shellHook = ''
          echo "Welcome to xrpld development shell";
          echo "Compiler: "
          ${compilerName} --version
        '';
      };
    in
    pkgs.mkShell.override { stdenv = compilerStdEnv; } shellAttrs;

in
{
  default = makeShell { };
  gcc = makeShell { compiler = "gcc"; };
  clang = makeShell { compiler = "clang"; };
}
