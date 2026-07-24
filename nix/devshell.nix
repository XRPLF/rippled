{ pkgs, customGlibc, ... }:
let
  inherit (import ./packages.nix { inherit pkgs; })
    commonPackages
    gccPackage
    gccVersion
    llvmVersion
    llvmPackages
    mkVersionedToolLinks
    mkGcov
    ;

  # Plain nixpkgs stdenvs — no custom glibc.
  plainGccStdenv = pkgs."gcc${toString gccVersion}Stdenv";
  plainClangStdenv = llvmPackages.stdenv;

  # Custom-glibc stdenvs, matching the CI environment (see compilers.nix). The
  # pinned glibc snapshot only builds on Linux, so on darwin these fall back to
  # the plain stdenvs; the `if isLinux` guard keeps `customGlibc` from being
  # forced (and erroring) on macOS.
  customCompilers = import ./compilers.nix { inherit pkgs customGlibc; };
  customGccStdenv = if pkgs.stdenv.isLinux then customCompilers.customStdenv else plainGccStdenv;
  customClangStdenv =
    if pkgs.stdenv.isLinux then customCompilers.customClangStdenv else plainClangStdenv;

  # gcov matching each gcc shell, so `-Dcoverage=ON` builds work in the shell.
  plainGcov = mkGcov {
    name = "plain";
    cc = gccPackage.cc;
  };
  customGccGcov = if pkgs.stdenv.isLinux then customCompilers.customGcov else plainGcov;

  # Shown when entering a *-plain shell. These exist only on Linux (see below),
  # where the stock toolchain diverges from CI.
  plainWarningHook = ''
    echo "⚠️  WARNING: this is the stock nixpkgs toolchain and does not match CI's glibc. Prefer 'nix develop .#gcc' / '.#clang' unless you need to skip the custom-glibc build."
  '';

  # Tools to expose under version-suffixed names (see mkVersionedToolLinks).
  gccVersionedTools = [
    "gcc"
    "g++"
    "cpp"
  ];
  clangVersionedTools = [
    "clang"
    "clang++"
  ];

  # compilerName is the command used to print the version, or null for none.
  makeShell =
    {
      shellName,
      stdenv,
      compilerName,
      version ? null,
      versionedTools ? [ ],
      extraPackages ? [ ],
      warningHook ? "",
      # Opt out of PatchNixBinary.cmake retargeting binaries to the system
      # loader. The plain toolchain links a newer glibc, so it must not be
      # patched; the custom toolchain patches by default.
      noPatchNixBinary ? false,
    }:
    let
      compilerVersionHook =
        if compilerName == null then
          ''echo "No compiler specified - using system compiler"''
        else
          ''
            echo "Compiler: "
            ${compilerName} --version
          '';
      versionedLinks = pkgs.lib.optional (version != null) (mkVersionedToolLinks {
        name = compilerName;
        package = stdenv.cc;
        inherit version;
        tools = versionedTools;
      });
    in
    (pkgs.mkShell.override { inherit stdenv; }) (
      {
        packages = commonPackages ++ versionedLinks ++ extraPackages;
        # Marks a managed dev shell, so the build (XrplSanity.cmake) can tell an
        # intentional Nix toolchain from one leaked into a bare shell.
        XRPL_DEVSHELL = shellName;
        shellHook = ''
          echo "Welcome to xrpld development shell";
          ${compilerVersionHook}
          ${warningHook}
        '';
      }
      // pkgs.lib.optionalAttrs noPatchNixBinary { XRPLD_NO_PATCH_NIX_BINARY = "1"; }
    );
in
rec {
  # macOS: Nix Clang. Linux: Nix GCC.
  default = if pkgs.stdenv.isDarwin then clang else gcc;

  # gcc/clang use the custom-glibc toolchain, matching CI. On darwin there is no
  # custom glibc, so they fall back to the plain nixpkgs toolchain.
  gcc = makeShell {
    shellName = "gcc";
    stdenv = customGccStdenv;
    compilerName = "gcc";
    version = gccVersion;
    versionedTools = gccVersionedTools;
    extraPackages = [ customGccGcov ];
  };

  clang = makeShell {
    shellName = "clang";
    stdenv = customClangStdenv;
    compilerName = "clang";
    version = llvmVersion;
    versionedTools = clangVersionedTools;
  };

  # Nix provides no compiler; use the one from your system (e.g. Apple Clang).
  no-compiler = makeShell {
    shellName = "no-compiler";
    stdenv = pkgs.stdenvNoCC;
    compilerName = null;
  };
  apple-clang = no-compiler;
}
# The *-plain shells (stock nixpkgs toolchain) exist only on Linux: on darwin
# gcc/clang are already plain, so these would be redundant and are omitted, which
# makes `nix develop .#gcc-plain` fail there rather than silently aliasing gcc.
// pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
  gcc-plain = makeShell {
    shellName = "gcc-plain";
    stdenv = plainGccStdenv;
    compilerName = "gcc";
    version = gccVersion;
    versionedTools = gccVersionedTools;
    extraPackages = [ plainGcov ];
    warningHook = plainWarningHook;
    noPatchNixBinary = true;
  };

  clang-plain = makeShell {
    shellName = "clang-plain";
    stdenv = plainClangStdenv;
    compilerName = "clang";
    version = llvmVersion;
    versionedTools = clangVersionedTools;
    warningHook = plainWarningHook;
    noPatchNixBinary = true;
  };
}
