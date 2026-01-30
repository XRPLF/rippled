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
  default_compiler = if pkgs.stdenv.isDarwin then "none" else "gcc";
  strToCompiler =
    compiler:
    (
      if compiler == "gcc" then
        [ pkgs.gcc12Stdenv ]
      else if compiler == "clang" then
        [ pkgs.clang16Stdenv ]
      else if compiler == "none" then
        [ ]
      else
        builtins.throw "Invalid compiler: ${compiler}. Must be one of: gcc, clang, none"
    );

  # Helper function to create a shell with a specific compiler
  makeShell =
    {
      compiler ? default_compiler,
    }:
    let
      compilerPackages = strToCompiler default_compiler;

      compilerName =
        if compiler == "none" then
          (if pkgs.stdenv.isDarwin then "apple-clang" else "system default")
        else
          compiler;
    in
    pkgs.mkShell {
      packages = common_packages ++ compilerPackages;

      shellHook = ''
        echo "Welcome to xrpld development shell"
        echo "Compiler: " ''$(${compilerName} --version)
      '';
    };

in
{
  default = makeShell { };
  gcc = makeShell { compiler = "gcc"; };
  clang = makeShell { compiler = "clang"; };
}
