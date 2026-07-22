{ pkgs, ... }:
let
  inherit (import ./packages.nix { inherit pkgs; })
    commonPackages
    gccVersion
    llvmVersion
    llvmPackages
    mkVersionedToolLinks
    ;

  # Plain nixpkgs stdenvs — no custom glibc, unlike ci-env.nix.
  gccStdenv = pkgs."gcc${toString gccVersion}Stdenv";
  clangStdenv = llvmPackages.stdenv;

  # compilerName is the command used to print the version, or null for none.
  makeShell =
    {
      stdenv,
      compilerName,
      version ? null,
      versionedTools ? [ ],
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
      packages = commonPackages ++ versionedLinks;
      shellHook = ''
        echo "Welcome to xrpld development shell";
        ${compilerVersion}
      '';
    };
in
rec {
  # macOS: Nix Clang. Linux: Nix GCC.
  default = if pkgs.stdenv.isDarwin then clang else gcc;

  gcc = makeShell {
    stdenv = gccStdenv;
    compilerName = "gcc";
    version = gccVersion;
    versionedTools = [
      "gcc"
      "g++"
      "cpp"
    ];
  };

  clang = makeShell {
    stdenv = clangStdenv;
    compilerName = "clang";
    version = llvmVersion;
    versionedTools = [
      "clang"
      "clang++"
    ];
  };

  # Nix provides no compiler; use the one from your system (e.g. Apple Clang).
  no-compiler = makeShell {
    stdenv = pkgs.stdenvNoCC;
    compilerName = null;
  };
  apple-clang = no-compiler;
}
