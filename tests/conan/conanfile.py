from pathlib import Path

from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout

from conan import ConanFile


class Example(ConanFile):
    name = "example"
    license = "ISC"
    author = "John Freeman <jfreeman08@gmail.com>, Michael Legleux <mlegleux@ripple.com"

    settings = "os", "compiler", "build_type", "arch"

    requires = ["xrpl/head"]

    default_options = {
        "xrpl/*:xrpld": False,
    }

    generators = ["CMakeDeps", "CMakeToolchain"]

    def set_version(self):
        if self.version is None:
            self.version = "0.1.0"

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def test(self):
        if can_run(self):
            cmd_path = Path(self.build_folder) / self.cpp.build.bindir / "example"
            self.run(cmd_path, env="conanrun")
