import os

from conan import ConanFile
from conan.tools.files import chdir, copy


class XrplLean4Deps(ConanFile):
    """mathlib and its dependencies, compiled and bundled into libLeanDeps.a.

    Split out from xrpl-lean4 and keyed only on the dependency pins
    (lakefile.toml, lake-manifest.json, lean-toolchain), never the XRPL
    sources, so it rebuilds only on a mathlib bump. `lake exe cache get` ships
    no native objects, so they are compiled here from each dependency's
    :static facet and merged by scripts/bundle_lean_deps.sh.

    Export with: conan export formal_verification/deps
    """

    name = "xrpl-lean4-deps"
    version = "0.1.0"
    license = "Apache-2.0"
    url = "https://github.com/commonprefix/rippled-formal-verification"
    description = (
        "Compiled mathlib + Lean dependency objects (libLeanDeps.a) for xrpl-lean4"
    )
    settings = "os", "arch"

    # The recipe lives in deps/, but its inputs are one level up and the
    # exports_* attributes can't use "..", so copy them in via these methods.
    def export(self):
        copy(
            self,
            "lean-toolchain",
            src=os.path.join(self.recipe_folder, ".."),
            dst=self.export_folder,
        )

    def export_sources(self):
        base = os.path.join(self.recipe_folder, "..")
        for name in ("lakefile.toml", "lake-manifest.json", "lean-toolchain"):
            copy(self, name, src=base, dst=self.export_sources_folder)
        copy(
            self,
            "bundle_lean_deps.sh",
            src=os.path.join(base, "scripts"),
            dst=os.path.join(self.export_sources_folder, "scripts"),
        )

    def _lean_version(self):
        path = os.path.join(self.recipe_folder, "lean-toolchain")
        with open(path, encoding="utf-8") as f:
            return f.read().strip().split(":v")[1]

    def requirements(self):
        # libLeanDeps.a references the Lean runtime and GMP at link time.
        self.requires(f"lean4/{self._lean_version()}", transitive_libs=True)
        self.requires("gmp/6.3.0", transitive_libs=True)

    def build_requirements(self):
        self.tool_requires(f"lean4/{self._lean_version()}")

    def build(self):
        with chdir(self, self.source_folder):
            self.run("lake exe cache get")
            self.run("sh scripts/bundle_lean_deps.sh")

    def package(self):
        src = os.path.join(self.source_folder, ".lake", "build", "lib")
        copy(
            self, "libLeanDeps.a", src=src, dst=os.path.join(self.package_folder, "lib")
        )

    def package_info(self):
        self.cpp_info.includedirs = []
        self.cpp_info.libs = ["LeanDeps"]
        self.cpp_info.requires = ["lean4::lean4", "gmp::gmp"]
