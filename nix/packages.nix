{ pkgs }:
let
  # Compiler versions used across the dev shell and the CI environment.
  gccVersion = 15;
  llvmVersion = 22;

  gccPackage = pkgs."gcc${toString gccVersion}";
  llvmPackages = pkgs."llvmPackages_${toString llvmVersion}";

  # Bound explicitly so it tracks llvmPackages above, not the `with pkgs` default.
  clangTools = llvmPackages.clang-tools;

  # In LLVM 22, run-clang-tidy.py moved from share/clang/ to bin/, so nixpkgs
  # clang-tools no longer links it. Wrap it manually.
  runClangTidy = pkgs.writeShellScriptBin "run-clang-tidy" ''
    exec ${pkgs.python3}/bin/python3 ${llvmPackages.clang-unwrapped}/bin/run-clang-tidy "$@"
  '';

  rust = import ./rust.nix { inherit pkgs; };

  # Nix wraps its toolchain so that binaries are exposed only under unsuffixed
  # names (gcc, g++, clang-tidy, ...). Several tools probe for a
  # version-suffixed name first and fall back to a system binary on the PATH
  # when Nix doesn't provide it:
  #   - Conan's Boost recipe looks up `g++-<major>` before plain `g++`.
  #   - bin/pre-commit/clang_tidy_check.py looks up `run-clang-tidy-<v>` and
  #     `clang-apply-replacements-<v>` before the unsuffixed names.
  # On a host that also has the matching system binary (e.g. Ubuntu's
  # `/usr/bin/g++-15` or `clang-tidy-22`) the probe escapes Nix and mixes a
  # system tool into the Nix environment. Generate version-suffixed symlinks
  # next to a package's tools so those probes resolve to the Nix ones.
  #
  # Compiler links must point at whichever compiler is active in a given
  # environment (the plain stdenv compiler in the dev shell, the custom-glibc
  # wrappers in ci-env.nix), so those callers pass their own `package`; the
  # clang tooling is environment-independent and is linked in commonPackages.
  #
  # Exec wrappers, not symlinks: the nixpkgs clang-tools wrapper dispatches on
  # `$(basename $0)-unwrapped`, which a suffixed symlink turns into a dead path.
  mkVersionedToolLinks =
    {
      name,
      package,
      version,
      tools,
    }:
    pkgs.symlinkJoin {
      name = "${name}-${toString version}-versioned-links";
      paths = map (
        tool:
        pkgs.writeShellScriptBin "${tool}-${toString version}" ''
          exec "${package}/bin/${tool}" "$@"
        ''
      ) tools;
    };

  # The cc-wrapper doesn't re-export gcov, but coverage tooling (gcovr) needs a
  # gcov that exactly matches the compiler. Surface it from a gcc `cc` output.
  mkGcov =
    { name, cc }:
    pkgs.linkFarm "gcov-${name}" [
      {
        name = "bin/gcov";
        path = "${cc}/bin/gcov";
      }
    ];

  clangToolLinks = mkVersionedToolLinks {
    name = "clang-tools";
    package = clangTools;
    version = llvmVersion;
    tools = [
      "clang-tidy"
      "clang-apply-replacements"
      "clang-format"
    ];
  };
  runClangTidyLink = mkVersionedToolLinks {
    name = "run-clang-tidy";
    package = runClangTidy;
    version = llvmVersion;
    tools = [ "run-clang-tidy" ];
  };
in
{
  inherit
    gccVersion
    llvmVersion
    gccPackage
    llvmPackages
    mkVersionedToolLinks
    mkGcov
    ;

  commonPackages =
    (with pkgs; [
      clangToolLinks
      runClangTidyLink
      ccache
      clangbuildanalyzer
      clangTools
      cmake
      conan
      curlMinimal # needed for codecov/codecov-action
      doxygen
      file # needed for cpack in Clio
      gcovr
      gh
      git
      git-cliff
      git-lfs
      gnumake
      gnupg # needed for signing commits & codecov/codecov-action
      graphviz
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
      zip
    ])
    ++ rust.packages;
}
