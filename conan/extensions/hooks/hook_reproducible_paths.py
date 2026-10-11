"""Make packages built from source give the same bytes in any Conan home.

Conan builds each package in a randomly named folder under the Conan home
(``<home>/p/b/<name><random>``), and a package built from source stays in such a
folder. Those paths reach ``__FILE__`` strings, debug information, rpath strings
and pkg-config files, and builds that stamp the clock add a date. This hook
removes each of them, so building from source reproduces the prebuilt binaries.
Everything here runs after the package ID is computed, so package IDs do not
change.
"""

import os

_CACHE_BUILD_DIR = os.sep + os.path.join("p", "b") + os.sep
_PREFIX_MAP_COMPILERS = ("gcc", "clang", "apple-clang")
# These builds record their compiler flags in the library, so prefix maps would
# put the paths back; their sources compile with relative paths without them.
_RECORDS_COMPILER_FLAGS = ("openssl",)


def pre_generate(conanfile):
    build_folder = conanfile.build_folder
    if not build_folder or _CACHE_BUILD_DIR not in build_folder:
        return  # A consumer project, not a package built in the Conan cache.

    # Builds that stamp a date (openssl, rocksdb's string(TIMESTAMP)) take the
    # recipe revision's time instead of the clock: the remote's upload time, the
    # same for everyone who downloads the recipe.
    if conanfile.ref.timestamp:
        conanfile.buildenv.define(
            "SOURCE_DATE_EPOCH", str(int(conanfile.ref.timestamp))
        )

    # Link with the install rpath, so no build-tree rpath is rewritten at install
    # and left behind in .dynstr.
    conanfile.conf.update(
        "tools.cmake.cmaketoolchain:extra_variables",
        {"CMAKE_BUILD_WITH_INSTALL_RPATH": "ON"},
    )

    if conanfile.settings.get_safe("compiler") not in _PREFIX_MAP_COMPILERS:
        return
    if conanfile.name in _RECORDS_COMPILER_FLAGS:
        return

    # GCC applies the last matching map: the Conan home first, then the
    # dependency package folders, then the package's own random cache folder
    # (<home>/p/b/<name><random>, holding its build and copied sources) last,
    # so it never shadows a dependency folder.
    conan_home, cache_path = build_folder.split(_CACHE_BUILD_DIR, 1)
    flags = [f"-ffile-prefix-map={conan_home}=/conan-home"]
    for dependency in conanfile.dependencies.values():
        if dependency.package_folder:
            flags.append(
                f"-ffile-prefix-map={dependency.package_folder}=/conan/{dependency.ref.name}"
            )
    package_cache_folder = os.path.join(
        conan_home, "p", "b", cache_path.split(os.sep, 1)[0]
    )
    flags.append(
        f"-ffile-prefix-map={package_cache_folder}=/conan-build/{conanfile.name}"
    )
    conanfile.conf.append("tools.build:cflags", flags)
    conanfile.conf.append("tools.build:cxxflags", flags)


def post_package(conanfile):
    # pkg-config files name the package folder; point them at their own location
    # instead, which pkg-config resolves through ${pcfiledir}.
    package_folder = conanfile.package_folder
    for root, _, files in os.walk(package_folder):
        for name in files:
            if not name.endswith(".pc"):
                continue
            path = os.path.join(root, name)
            relative = os.path.relpath(package_folder, root)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            replaced = text.replace(package_folder, "${pcfiledir}/" + relative)
            if replaced != text:
                with open(path, "w", encoding="utf-8") as f:
                    f.write(replaced)
