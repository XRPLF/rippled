import os

from conan import ConanFile
from conan.tools.files import chdir, copy


class XrplLean4(ConanFile):
    """The Lean 4 XRPL model packaged for linking into xrpld.

    Produces libXRPL_XRPLModel.a (the Model + FFI modules, with the
    lean_number_* exports). mathlib's compiled objects live in the separate
    xrpl-lean4-deps package, so editing the model never rebuilds mathlib;
    this recipe requires it and propagates it to the consumer's link line.
    lake runs inside the Conan cache, so a checkout never gets a .lake dir.

    Export with: conan export formal_verification
    """

    name = "xrpl-lean4"
    version = "0.1.0"
    license = "ISC"
    url = "https://github.com/commonprefix/rippled-formal-verification"
    description = "Lean 4 formal verification model of XRPL (FFI static library)"
    settings = "os", "arch"

    exports = ("lean-toolchain",)
    exports_sources = (
        "XRPL.lean",
        "XRPL/*",
        "lakefile.toml",
        "lake-manifest.json",
        "lean-toolchain",
    )

    def _lean_version(self):
        # lean-toolchain pins e.g. "leanprover/lean4:v4.28.0".
        path = os.path.join(self.recipe_folder, "lean-toolchain")
        with open(path, encoding="utf-8") as f:
            return f.read().strip().split(":v")[1]

    def requirements(self):
        # Consumers include lean.h and link the lean4 + deps libs, so propagate.
        self.requires(
            f"lean4/{self._lean_version()}",
            transitive_headers=True,
            transitive_libs=True,
        )
        self.requires("xrpl-lean4-deps/0.1.0", transitive_libs=True)
        self.requires("gmp/6.3.0", transitive_libs=True)

    def build_requirements(self):
        self.tool_requires(f"lean4/{self._lean_version()}")

    def build(self):
        with chdir(self, self.source_folder):
            self.run("lake exe cache get")  # mathlib .olean/.c, no native objects
            self.run("lake build XRPLModel:static")

    def package(self):
        src = os.path.join(self.source_folder, ".lake", "build", "lib")
        copy(
            self,
            "libXRPL_XRPLModel.a",
            src=src,
            dst=os.path.join(self.package_folder, "lib"),
        )

    def package_info(self):
        # Link order: model, then deps (libLeanDeps.a), then lean4 runtime, then gmp.
        self.cpp_info.includedirs = []
        self.cpp_info.libs = ["XRPL_XRPLModel"]
        self.cpp_info.requires = [
            "xrpl-lean4-deps::xrpl-lean4-deps",
            "lean4::lean4",
            "gmp::gmp",
        ]
