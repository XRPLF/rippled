from conan import ConanFile
from conan.tools.cmake import CMake, CMakeToolchain, cmake_layout
import re

class Xrpl(ConanFile):
    name = 'xrpl'

    license = 'ISC'
    author = 'John Freeman <jfreeman@ripple.com>'
    url = 'https://github.com/xrplf/rippled'
    description = 'The XRP Ledger'
    settings = 'os', 'compiler', 'build_type', 'arch'
    options = {
        'assertions': [True, False],
        'coverage': [True, False],
        'fPIC': [True, False],
        'jemalloc': [True, False],
        'reporting': [True, False],
        'rocksdb': [True, False],
        'shared': [True, False],
        'static': [True, False],
        'tests': [True, False],
        'unity': [True, False],
    }

    requires = [
        'date/3.0.1',
        'grpc/1.50.1',
        'libarchive/3.6.2',
        'nudb/2.0.8',
        'openssl/1.1.1u',
        'snappy/1.1.10',
        'soci/4.0.3',
        'zlib/1.2.13',
        'xxhash/0.8.2',
    ]

    tool_requires = [
        'protobuf/3.21.9',
    ]

    default_options = {
        'assertions': False,
        'coverage': False,
        'fPIC': True,
        'jemalloc': False,
        'reporting': False,
        'rocksdb': True,
        'shared': False,
        'static': True,
        'tests': True,
        'unity': False,

        'cassandra-cpp-driver/*:shared': False,
        'cassandra-cpp-driver/*:use_atomic': None,
        'date/*:header_only': True,
        'grpc/*:shared': False,
        'grpc/*:secure': True,
        'libarchive/*:shared': False,
        'libarchive/*:with_acl': False,
        'libarchive/*:with_bzip2': False,
        'libarchive/*:with_cng': False,
        'libarchive/*:with_expat': False,
        'libarchive/*:with_iconv': False,
        'libarchive/*:with_libxml2': False,
        'libarchive/*:with_lz4': True,
        'libarchive/*:with_lzma': False,
        'libarchive/*:with_lzo': False,
        'libarchive/*:with_nettle': False,
        'libarchive/*:with_openssl': False,
        'libarchive/*:with_pcreposix': False,
        'libarchive/*:with_xattr': False,
        'libarchive/*:with_zlib': False,
        'libpq/*:shared': False,
        'lz4/*:shared': False,
        'openssl/*:shared': False,
        'protobuf/*:shared': False,
        'protobuf/*:with_zlib': True,
        'rocksdb/*:enable_sse': False,
        'rocksdb/*:lite': False,
        'rocksdb/*:shared': False,
        'rocksdb/*:use_rtti': True,
        'rocksdb/*:with_jemalloc': False,
        'rocksdb/*:with_lz4': True,
        'rocksdb/*:with_snappy': True,
        'snappy/*:shared': False,
        'soci/*:shared': False,
        'soci/*:with_sqlite3': True,
        'soci/*:with_boost': True,
        'xxhash/*:shared': False,
    }

    def set_version(self):
        path = f'{self.recipe_folder}/src/ripple/protocol/impl/BuildInfo.cpp'
        regex = r'versionString\s?=\s?\"(.*)\"'
        with open(path, 'r') as file:
            matches = (re.search(regex, line) for line in file)
            match = next(m for m in matches if m)
            self.version = match.group(1)

    def configure(self):
        if self.settings.compiler == 'apple-clang':
            self.options['boost'].visibility = 'global'

    def requirements(self):
        self.requires('boost/1.82.0', force=True)
        self.requires('lz4/1.9.3', force=True)
        self.requires('protobuf/3.21.9', force=True)
        self.requires('sqlite3/3.42.0', force=True)
        if self.options.jemalloc:
            self.requires('jemalloc/5.3.0')
        if self.options.reporting:
            self.requires('cassandra-cpp-driver/2.15.3')
            self.requires('libpq/14.7')
        if self.options.rocksdb:
            self.requires('rocksdb/6.29.5')

    exports_sources = (
        'CMakeLists.txt',
        'Builds/*',
        'bin/getRippledInfo',
        'cfg/*',
        'external/*',
        'src/*',
    )

    def layout(self):
        cmake_layout(self)
        # Fix this setting to follow the default introduced in Conan 1.48
        # to align with our build instructions.
        self.folders.generators = 'build/generators'

    generators = 'CMakeDeps'
    def generate(self):
        tc = CMakeToolchain(self)
        tc.variables['tests'] = self.options.tests
        tc.variables['assert'] = self.options.assertions
        tc.variables['coverage'] = self.options.coverage
        tc.variables['jemalloc'] = self.options.jemalloc
        tc.variables['reporting'] = self.options.reporting
        tc.variables['rocksdb'] = self.options.rocksdb
        tc.variables['BUILD_SHARED_LIBS'] = self.options.shared
        tc.variables['static'] = self.options.static
        tc.variables['unity'] = self.options.unity
        tc.generate()

    def build(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.configure()
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.verbose = True
        cmake.install()

    def package_info(self):
        libxrpl = self.cpp_info.components['libxrpl']
        libxrpl.libs = [
            'xrpl_core',
            'xrpl.libpb',
            'ed25519',
            'secp256k1',
        ]
        # TODO: Fix the protobufs to include each other relative to
        # `include/`, not `include/ripple/proto/`.
        libxrpl.includedirs = ['include', 'include/ripple/proto']
        libxrpl.requires = [
            'boost::boost',
            'date::date',
            'grpc::grpc++',
            'libarchive::libarchive',
            'lz4::lz4',
            'nudb::nudb',
            'openssl::crypto',
            'protobuf::libprotobuf',
            'snappy::snappy',
            'soci::soci',
            'sqlite3::sqlite',
            'xxhash::xxhash',
            'zlib::zlib',
        ]
        if self.options.rocksdb:
            libxrpl.requires.append('rocksdb::librocksdb')
