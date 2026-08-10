# Custom-glibc compiler toolchain shared by the CI environment (ci-env.nix) and
# the Linux dev shell (devshell.nix): gcc / clang / binutils rebuilt to target
# the pinned custom glibc. Linux only — the pinned glibc snapshot does not build
# on darwin, so callers must not evaluate this on macOS.
{
  pkgs,
  customGlibc,
}:
let
  inherit (import ./packages.nix { inherit pkgs; })
    gccPackage
    llvmPackages
    llvmVersion
    mkGcov
    ;

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
  customGccCc = gccPackage.cc.override {
    stdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv (
      pkgs.wrapCCWith {
        cc = gccPackage.cc;
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

  # gcov matching the rebuilt gcc (linked against the custom glibc), so gcovr
  # can produce coverage reports both in CI and in the dev shell.
  customGcov = mkGcov {
    name = "custom";
    cc = customGccCc;
  };

  # stdenv built around the rebuilt gcc / custom glibc. Exported as the dev
  # shell's gcc stdenv, and used below to rebuild compiler-rt so its sanitizer
  # runtimes see the custom glibc headers.
  customStdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv customGcc;

  # Rebuild compiler-rt against the custom glibc so the sanitizer runtimes
  # don't use glibc symbols (or sysconf constants like _SC_SIGSTKSZ) that
  # only exist in newer glibc versions. scudo is dropped because its CMake
  # includes CheckAtomic with -nostdinc++ in CMAKE_REQUIRED_FLAGS, which
  # makes std::atomic unfindable in our stdenv; we don't use scudo (only
  # asan/ubsan/tsan etc.).
  customCompilerRt =
    (llvmPackages.compiler-rt.override {
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
    cc = llvmPackages.clang-unwrapped;
    libc = customGlibc;
    bintools = customBinutils;
    gccForLibs = customGccCc;
    extraPackages = [ customCompilerRt ];
    extraBuildCommands = ''
      rsrc="$out/resource-root"
      mkdir "$rsrc"
      ln -s "${llvmPackages.clang-unwrapped.lib}/lib/clang/${toString llvmVersion}/include" "$rsrc/include"
      ln -s "${customCompilerRt.out}/lib" "$rsrc/lib"
      ln -s "${customCompilerRt.out}/share" "$rsrc/share" || true
      echo "-resource-dir=$rsrc" >> $out/nix-support/cc-cflags
      # compiler-rt ships the sanitizer/profile/xray interface headers (e.g.
      # <sanitizer/lsan_interface.h>) in its `dev` output. In a normal Nix
      # build these reach the include path because compiler-rt is propagated
      # via depsTargetTargetPropagated and stdenv's setup hooks add its
      # dev/include. The CI image runs clang outside a Nix stdenv (binaries
      # on PATH, no setup hooks), so that never happens; add the headers
      # explicitly. gcc ships its own copy, which is why this is clang-only.
      echo "-isystem ${customCompilerRt.dev}/include" >> $out/nix-support/cc-cflags
    '';
  };
in
{
  inherit
    customGcc
    customClang
    customBinutils
    customStdenv
    customGcov
    ;

  customClangStdenv = pkgs.stdenvAdapters.overrideCC pkgs.stdenv customClang;
}
