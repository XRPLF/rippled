{ pkgs }:
let
  # In LLVM 22, run-clang-tidy.py moved from share/clang/ to bin/, so nixpkgs
  # clang-tools no longer links it. Wrap it manually.
  runClangTidy = pkgs.writeShellScriptBin "run-clang-tidy" ''
    exec ${pkgs.python3}/bin/python3 ${pkgs.llvmPackages_22.clang-unwrapped}/bin/run-clang-tidy "$@"
  '';
in
{
  commonPackages = with pkgs; [
    ccache
    cmake
    conan
    gcovr
    git
    gnumake
    llvmPackages_22.clang-tools
    mold
    ninja
    perl # needed for openssl
    pkg-config
    pre-commit
    python3
    runClangTidy
    vim
  ];
}
