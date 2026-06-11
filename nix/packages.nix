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
    curlMinimal # needed for codecov/codecov-action
    doxygen
    file # needed for cpack in Clio
    gcovr
    gh
    git
    git-cliff
    gnumake
    gnupg # needed for signing commits & codecov/codecov-action
    llvmPackages_22.clang-tools
    less # needed for git diff
    mold
    nettools # provides netstat, used to debug failures in CI
    ninja
    patchelf
    perl # needed for openssl
    pkg-config
    pre-commit
    python3
    runClangTidy
    vim
  ];
}
