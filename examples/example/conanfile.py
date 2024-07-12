from conan import ConanFile, conan_version
from conan.tools.cmake import CMake, cmake_layout

class Example(ConanFile):

    def set_name(self):
        if self.name is None:
            self.name = 'example'

    def set_version(self):
        if self.version is None:
            self.version = '0.1.0'

    license = 'ISC'
    author = 'John Freeman <jfreeman08@gmail.com>'

    settings = 'os', 'compiler', 'build_type', 'arch'
    options = {'shared': [True, False], 'fPIC': [True, False]}
    default_options = {
        'shared': False,
        'fPIC': True,
        'xrpl:xrpld': False,
    }

    requires = ['xrpl/2.2.0-rc1@jfreeman/nodestore']
    generators = ['CMakeDeps', 'CMakeToolchain']

    exports_sources = [
        'CMakeLists.txt',
        'cmake/*',
        'external/*',
        'include/*',
        'src/*',
    ]

    # For out-of-source build.
    # https://docs.conan.io/en/latest/reference/build_helpers/cmake.html#configure
    no_copy_source = True

    def layout(self):
        cmake_layout(self)

    def config_options(self):
        if self.settings.os == 'Windows':
            del self.options.fPIC

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={'BUILD_TESTING': 'NO'})
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        path = f'{self.package_folder}/share/{self.name}/cpp_info.py'
        with open(path, 'r') as file:
            exec(file.read(), {}, {'self': self.cpp_info})
