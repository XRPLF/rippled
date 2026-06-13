import glob
import os

from conan import ConanFile
from conan.tools.files import copy, download

ELAN_INIT_URL = "https://elan.lean-lang.org/elan-init.sh"


class Lean(ConanFile):
    """The Lean 4 toolchain (lean, lake, headers, runtime), installed via elan.

    Used both as a tool requirement (lake/lean on PATH) and a host requirement
    (lean.h, libLake.a, libleanshared for linking into xrpld). The version
    defaults to the one pinned in formal_verification/lean-toolchain; pass
    --version when exporting outside the repo layout (e.g. the CI image).
    """

    name = "lean4"
    license = "Apache-2.0"
    url = "https://github.com/leanprover/lean4"
    description = "The Lean 4 theorem prover and toolchain"
    settings = "os", "arch"

    def set_version(self):
        if self.version is None:
            toolchain = os.path.join(
                self.recipe_folder, "..", "..", "formal_verification", "lean-toolchain"
            )
            with open(toolchain, encoding="utf-8") as f:
                self.version = f.read().strip().split(":v")[1]  # "leanprover/lean4:vX" -> "X"

    def build(self):
        elan_home = os.path.join(self.build_folder, "elan")
        script = os.path.join(self.build_folder, "elan-init.sh")
        download(self, ELAN_INIT_URL, script)
        # elan-init only records the default toolchain (install is lazy), so do it.
        self.run(f'ELAN_HOME="{elan_home}" sh "{script}" -y --no-modify-path --default-toolchain none')
        elan = os.path.join(elan_home, "bin", "elan")
        self.run(f'ELAN_HOME="{elan_home}" "{elan}" toolchain install leanprover/lean4:v{self.version}')

    def package(self):
        toolchains = glob.glob(os.path.join(self.build_folder, "elan", "toolchains", "*"))
        if len(toolchains) != 1:
            raise RuntimeError(f"expected exactly one toolchain, got {toolchains}")
        copy(self, "*", src=toolchains[0], dst=self.package_folder)

    def package_info(self):
        self.cpp_info.includedirs = ["include"]
        self.cpp_info.libdirs = [os.path.join("lib", "lean")]
        self.cpp_info.libs = ["Lake", "leanshared"]  # order matters: Lake before the runtime
        self.cpp_info.bindirs = ["bin"]
        self.buildenv_info.prepend_path("PATH", os.path.join(self.package_folder, "bin"))
