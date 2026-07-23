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
      stdenv,
      compilerName,
      version ? null,
      versionedTools ? [ ],
      extraPackages ? [ ],
    }:
    let
      compilerVersion =
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
    (pkgs.mkShell.override { inherit stdenv; }) {
      packages = commonPackages ++ versionedLinks ++ extraPackages;
      shellHook = ''
        echo "Welcome to xrpld development shell";
        ${compilerVersion}
      '';
    };
in
rec {
  # macOS: Nix Clang. Linux: Nix GCC.
  default = if pkgs.stdenv.isDarwin then clang else gcc;

  # gcc/clang use the custom-glibc toolchain (matching CI); *-plain use stock nixpkgs.
  gcc = makeShell {
    stdenv = customGccStdenv;
    compilerName = "gcc";
    version = gccVersion;
    versionedTools = gccVersionedTools;
    extraPackages = [ customGccGcov ];
  };

  clang = makeShell {
    stdenv = customClangStdenv;
    compilerName = "clang";
    version = llvmVersion;
    versionedTools = clangVersionedTools;
  };

  gcc-plain = makeShell {
    stdenv = plainGccStdenv;
    compilerName = "gcc";
    version = gccVersion;
    versionedTools = gccVersionedTools;
    extraPackages = [ plainGcov ];
  };

  clang-plain = makeShell {
    stdenv = plainClangStdenv;
    compilerName = "clang";
    version = llvmVersion;
    versionedTools = clangVersionedTools;
  };

  # Nix provides no compiler; use the one from your system (e.g. Apple Clang).
  no-compiler = makeShell {
    stdenv = pkgs.stdenvNoCC;
    compilerName = null;
  };
  apple-clang = no-compiler;
}
