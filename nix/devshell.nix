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
  strToCompilerEnv =
    compiler:
    (
      if compiler == "gcc" then
        pkgs.gcc14Stdenv
      else if compiler == "clang" then
        pkgs.llvmPackages_18.stdenv
      else if compiler == "none" then
        pkgs.stdenvNoCC
      else
        throw "Invalid compiler: ${compiler}. Must be one of: gcc, clang, none"
    );

  # strToCompilerPackages =
  #   compiler:
  #   (
  #     if compiler == "gcc" then
  #       # Add libstdc++ for C++ stdlib (C stdlib comes from stdenv's glibc)
  #       [ pkgs.gcc14.cc.lib ]
  #     else if compiler == "clang" then
  #       # Add libc++ for C++ stdlib and libcxxabi for C++ ABI
  #       [
  #         pkgs.llvmPackages_16.libcxx
  #         pkgs.llvmPackages_16.libcxxabi
  #       ]
  #     else
  #       [ ]
  #   );

  # Helper function to create a shell with a specific compiler
  makeShell =
    {
      compiler ? default_compiler,
    }:
    let
      compilerStdEnv = strToCompilerEnv compiler;
      # compilerPackages = strToCompilerPackages compiler;

      compilerName =
        if compiler == "none" then
          (if pkgs.stdenv.isDarwin then "clang" else "system default")
        else
          compiler;

      # Use stdenvNoCC when no compiler is specified to avoid pulling in nixpkgs compiler
      # mkShellFn = if compiler == "none" then pkgs.stdenvNoCC.mkDerivation else pkgs.mkShell;

      shellAttrs = {
        packages = common_packages; # ++ compilerPackages;

        shellHook = ''
          echo "Welcome to xrpld development shell"
          echo "Compiler: "
          ${compilerName} --version
        ''
        + pkgs.lib.optionalString (compiler == "gcc" && pkgs.stdenv.isDarwin) ''

          # GCC on macOS needs help finding system headers
          echo "Warning: GCC on macOS may have issues with system headers."
          echo "Consider using 'nix develop .#clang' instead for better macOS compatibility."

          # Try to set up paths for macOS SDK
          if [ -n "$NIX_CC" ]; then
            export CPATH="$NIX_CC/include:''${CPATH:-}"
          fi
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
