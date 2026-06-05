{ pkgs, ... }:
let
  commonPackages = with pkgs; [
    ccache
    cmake
    conan
    gcovr
    git
    gnumake
    llvmPackages_21.clang-tools
    ninja
    perl # needed for openssl
    pkg-config
    pre-commit
    python314
  ];

  # Supported compiler versions
  gccVersion = pkgs.lib.range 13 15;
  clangVersions = pkgs.lib.range 18 21;

  defaultCompiler = if pkgs.stdenv.isDarwin then "apple-clang" else "gcc";
  defaultGccVersion = pkgs.lib.last gccVersion;
  defaultClangVersion = pkgs.lib.last clangVersions;

  strToCompilerEnv =
    compiler: version:
    (
      if compiler == "gcc" then
        let
          gccPkg = pkgs."gcc${toString version}Stdenv" or null;
        in
        if gccPkg != null && builtins.elem version gccVersion then
          gccPkg
        else
          throw "Invalid GCC version: ${toString version}. Must be one of: ${toString gccVersion}"
      else if compiler == "clang" then
        let
          clangPkg = pkgs."llvmPackages_${toString version}".stdenv or null;
        in
        if clangPkg != null && builtins.elem version clangVersions then
          clangPkg
        else
          throw "Invalid Clang version: ${toString version}. Must be one of: ${toString clangVersions}"
      else if compiler == "apple-clang" || compiler == "none" then
        pkgs.stdenvNoCC
      else
        throw "Invalid compiler: ${compiler}. Must be one of: gcc, clang, apple-clang, none"
    );

  # Helper function to create a shell with a specific compiler
  makeShell =
    {
      compiler ? defaultCompiler,
      version ? (
        if compiler == "gcc" then
          defaultGccVersion
        else if compiler == "clang" then
          defaultClangVersion
        else
          null
      ),
    }:
    let
      compilerStdEnv = strToCompilerEnv compiler version;

      compilerName =
        if compiler == "apple-clang" then
          "clang"
        else if compiler == "none" then
          null
        else
          compiler;

      gccOnMacWarning =
        if pkgs.stdenv.isDarwin && compiler == "gcc" then
          ''
            echo "WARNING: Using GCC on macOS with Conan may not work."
            echo "         Consider using 'nix develop .#clang' or the default shell instead."
            echo ""
          ''
        else
          "";

      compilerVersion =
        if compilerName != null then
          ''
            echo "Compiler: "
            ${compilerName} --version
          ''
        else
          ''
            echo "No compiler specified - using system compiler"
          '';

      shellAttrs = {
        packages = commonPackages;

        shellHook = ''
          echo "Welcome to xrpld development shell";
          ${gccOnMacWarning}${compilerVersion}
        '';
      };
    in
    pkgs.mkShell.override { stdenv = compilerStdEnv; } shellAttrs;

  # Generate shells for each compiler version
  gccShells = builtins.listToAttrs (
    map (version: {
      name = "gcc${toString version}";
      value = makeShell {
        compiler = "gcc";
        version = version;
      };
    }) gccVersion
  );

  clangShells = builtins.listToAttrs (
    map (version: {
      name = "clang${toString version}";
      value = makeShell {
        compiler = "clang";
        version = version;
      };
    }) clangVersions
  );

in
gccShells
// clangShells
// {
  # Default shells
  default = makeShell { };
  gcc = makeShell { compiler = "gcc"; };
  clang = makeShell { compiler = "clang"; };

  # No compiler
  no-compiler = makeShell { compiler = "none"; };
  apple-clang = makeShell { compiler = "apple-clang"; };
}
