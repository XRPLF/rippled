import os

from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout

from conan import ConanFile


class ValidatorKeysConanTest(ConanFile):
    name = "validator-keys-conan-test"
    license = "ISC"
    author = "John Freeman <jfreeman08@gmail.com>, Michael Legleux <mlegleux@ripple.com"

    settings = "os", "compiler", "build_type", "arch"

    default_options = {
        "xrpl/*:xrpld": False,
    }

    generators = ["CMakeDeps", "CMakeToolchain"]

    def set_version(self):
        if self.version is None:
            self.version = "0.1.0"

    def requirements(self):
        # Test whatever reference is being created/tested rather than a
        # hardcoded version, so this test_package works for any xrpl version.
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build(target="validator-keys")

    def test(self):
        if can_run(self):
            cmd = os.path.join(self.cpp.build.bindir, "validator-keys")
            self.run(f'"{cmd}" --unittest', env="conanrun")
