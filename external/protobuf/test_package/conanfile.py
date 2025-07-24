from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import cmake_layout, CMake, CMakeToolchain
from conan.tools.scm import Version
import os


class TestPackageConan(ConanFile):
    settings = "os", "arch", "compiler", "build_type"
    generators = "CMakeDeps", "VirtualBuildEnv", "VirtualRunEnv"
    test_type = "explicit"

    def layout(self):
        cmake_layout(self)

    def requirements(self):
        # note `run=True` so that the runenv can find protoc
        self.requires(self.tested_reference_str, run=True)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.cache_variables["protobuf_LITE"] = self.dependencies[self.tested_reference_str].options.lite
        protobuf_version = Version(self.dependencies[self.tested_reference_str].ref.version)
        tc.cache_variables["CONAN_TEST_USE_CXXSTD_14"] = protobuf_version >= "3.22"
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            bin_path = os.path.join(self.cpp.build.bindirs[0], "test_package")
            self.run(bin_path, env="conanrun")

            # Invoke protoc in the same way CMake would
            self.run(f"protoc --proto_path={self.source_folder} --cpp_out={self.build_folder} {self.source_folder}/addressbook.proto", env="conanrun")
            assert os.path.exists(os.path.join(self.build_folder,"addressbook.pb.cc"))
            assert os.path.exists(os.path.join(self.build_folder,"addressbook.pb.h"))
