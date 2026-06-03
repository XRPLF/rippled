{
  pkgs,
  customGlibc,
  ...
}:
let
  inherit (import ./packages.nix { inherit pkgs; }) commonPackages;
  inherit (pkgs) lib;

  # Underlying compiler toolchains to wrap. Bump these in one place to
  # roll the whole environment forward.
  customGccPackage = pkgs.gcc15;
  customLlvmPackages = pkgs.llvmPackages_22;
  customClangMajor = lib.versions.major (lib.getVersion customLlvmPackages.clang-unwrapped);

  # binutils wrapped to emit binaries that reference the custom glibc
  # (dynamic linker path, library search path, RPATH).
  customBinutils = pkgs.wrapBintoolsWith {
    bintools = pkgs.binutils-unwrapped;
    libc = customGlibc;
  };

  # Rebuild gcc (specifically libstdc++ / libgcc_s) against the custom
  # glibc. The override swaps gcc.cc's bootstrap stdenv for one that uses
  # the existing gcc binary but links against the custom glibc, so the
  # resulting compiler ships runtime libraries that only reference symbols
  # available in that glibc.
  customGccCc = customGccPackage.cc.override {
    stdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv (
      pkgs.wrapCCWith {
        cc = customGccPackage.cc;
        libc = customGlibc;
        bintools = customBinutils;
      }
    );
  };

  # cc-wrapper around the rebuilt compiler, pointing at the custom glibc
  # headers and libraries. This is what we actually expose to users.
  customGcc = pkgs.wrapCCWith {
    cc = customGccCc;
    libc = customGlibc;
    bintools = customBinutils;
  };

  # gcov ships in gcc's `cc` output, but the cc-wrapper doesn't expose it.
  # Surface the gcov from our rebuilt gcc (linked against the custom glibc, so
  # it runs under the loader installed in the image) and matching the exact
  # compiler version, so gcovr can produce coverage reports in the CI env.
  customGcov = pkgs.runCommand "gcov-custom-for-ci-env" { } ''
    mkdir -p "$out/bin"
    ln -s "${customGccCc}/bin/gcov" "$out/bin/gcov"
  '';

  # stdenv built around the rebuilt gcc / custom glibc. Used to rebuild
  # compiler-rt below so its sanitizer runtimes see the custom glibc
  # headers.
  customStdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv customGcc;

  # Rebuild compiler-rt against the custom glibc so the sanitizer runtimes
  # don't use glibc symbols (or sysconf constants like _SC_SIGSTKSZ) that
  # only exist in newer glibc versions. scudo is dropped because its CMake
  # includes CheckAtomic with -nostdinc++ in CMAKE_REQUIRED_FLAGS, which
  # makes std::atomic unfindable in our stdenv; we don't use scudo (only
  # asan/ubsan/tsan etc.).
  customCompilerRt =
    (customLlvmPackages.compiler-rt.override {
      stdenv = customStdenv;
    }).overrideAttrs
      (old: {
        postPatch = (old.postPatch or "") + ''
          substituteInPlace lib/CMakeLists.txt \
            --replace-quiet 'add_subdirectory(scudo/standalone)' \
                            '# scudo/standalone disabled in xrpld ci-env'
        '';
      });

  # cc-wrapper around clang, pointing at the custom glibc headers and
  # libraries. Reuses the rebuilt gcc for libstdc++ / libgcc_s so that
  # C++ binaries produced by clang also only reference symbols available
  # in the custom glibc. compiler-rt is wired into a resource-root so
  # sanitizer runtimes (libclang_rt.*.a) are found at link time; this
  # mirrors what nixpkgs does internally when building llvmPackages.clang.
  customClang = pkgs.wrapCCWith {
    cc = customLlvmPackages.clang-unwrapped;
    libc = customGlibc;
    bintools = customBinutils;
    gccForLibs = customGccCc;
    extraPackages = [ customCompilerRt ];
    extraBuildCommands = ''
      rsrc="$out/resource-root"
      mkdir "$rsrc"
      ln -s "${customLlvmPackages.clang-unwrapped.lib}/lib/clang/${customClangMajor}/include" "$rsrc/include"
      ln -s "${customCompilerRt.out}/lib" "$rsrc/lib"
      ln -s "${customCompilerRt.out}/share" "$rsrc/share" || true
      echo "-resource-dir=$rsrc" >> $out/nix-support/cc-cflags
    '';
  };

  # Strip the generic cc/c++/cpp symlinks from the clang wrapper so it can
  # coexist with the gcc wrapper in buildEnv. gcc remains the default
  # compiler (cc/c++/cpp); clang is invoked explicitly as clang/clang++.
  customClangForCiEnv = pkgs.symlinkJoin {
    name = "clang-wrapper-custom-for-ci-env";
    paths = [ customClang ];
    postBuild = ''
      rm -f $out/bin/cc $out/bin/c++ $out/bin/cpp
    '';
  };

in
{
  default = pkgs.buildEnv {
    name = "xrpld-ci-env";
    paths = commonPackages ++ [
      customGcc
      customGcov
      customClangForCiEnv
      customBinutils
      # CA certificate bundle so HTTPS clients (git, curl, conan) can verify
      # TLS connections without ca-certificates being installed in the system.
      pkgs.cacert
    ];
    pathsToLink = [
      "/bin"
      "/etc/ssl/certs"
      "/lib"
      "/include"
      "/share"
    ];
  };
}
