# Rippled Repository Folder Structure

This document outlines the folder structure of the rippled codebase, which implements the XRP Ledger server software. This guide helps developers (and AI assistants) understand the codebase organization and navigate the repository effectively.

## Root Level

```
rippled/
├── bin/                    # Scripts and utilities for Ripple integrators
├── cfg/                    # Example configuration files
├── cmake/                  # CMake build system configuration and modules
├── conan/                  # Conan package manager configuration
├── docs/                   # Documentation, design docs, and Doxygen config
├── external/               # Third-party dependencies (git subtrees)
├── include/                # Public headers for libxrpl
├── src/                    # Main source code directory
├── tests/                  # Additional test configurations
└── [build files]           # CMakeLists.txt, conanfile.py, README.md, etc.
```

## Source Code Structure (`src/`)

The `src/` directory contains the main implementation divided into three primary components:

### `src/libxrpl/` - Core Library

The core XRPL library containing fundamental components that can be used independently:

```
libxrpl/
├── basics/                 # Basic utilities and data structures
├── beast/                  # Boost.Beast-derived networking and utilities
├── crypto/                 # Cryptographic functions and utilities
├── json/                   # JSON parsing and manipulation
├── net/                    # Network communication utilities
├── protocol/               # Protocol definitions, serialization, transaction formats
├── resource/               # Resource management and rate limiting
└── server/                 # HTTP/WebSocket server implementation
```

### `src/xrpld/` - Server Application

The main rippled server application implementation:

```
xrpld/
├── app/                    # Application-level components
│   ├── consensus/          # Consensus algorithm implementation (RCL)
│   ├── ledger/             # Ledger management and history
│   ├── main/               # Application startup and configuration
│   ├── misc/               # Various utilities (amendments, fees, validation)
│   ├── paths/              # Payment path finding and AMM logic
│   ├── rdb/                # Relational database abstraction
│   └── tx/                 # Transaction processing and validation
├── conditions/             # Crypto-conditions support
├── consensus/              # Core consensus primitives
├── core/                   # Core server infrastructure
├── ledger/                 # Ledger view abstractions
├── nodestore/              # Persistent storage for ledger data
├── overlay/                # Peer-to-peer networking
├── peerfinder/             # Peer discovery and management
├── perflog/                # Performance logging
├── rpc/                    # RPC/API handling
├── shamap/                 # Shared hash map implementation
└── unity/                  # Unity build helpers
```

### `src/test/` - Test Framework

Comprehensive test suite with utilities and test cases:

```
test/
├── app/                    # Application-level tests
├── basics/                 # Basic utility tests
├── beast/                  # Beast library tests
├── consensus/              # Consensus algorithm tests
├── core/                   # Core infrastructure tests
├── csf/                    # Consensus Simulation Framework
├── jtx/                    # JSON Transaction test framework
├── ledger/                 # Ledger-related tests
├── nodestore/              # Storage tests
├── overlay/                # Network tests
├── peerfinder/             # Peer discovery tests
├── protocol/               # Protocol tests
├── resource/               # Resource management tests
├── rpc/                    # RPC/API tests
├── server/                 # Server tests
├── shamap/                 # SHAMap tests
└── unit_test/              # Unit test framework utilities
```

## Key Directories Explained

### Application Core (`src/xrpld/app/`)

- **`consensus/`**: Implements the XRP Ledger Consensus (RCL) algorithm
- **`ledger/`**: Manages ledger state, history, and synchronization
- **`main/`**: Application entry point, configuration, and startup logic
- **`tx/`**: Transaction validation, application, and all transaction types
- **`paths/`**: Payment pathfinding and Automated Market Maker (AMM) functionality
- **`misc/`**: Amendment system, fee voting, validator management

### Protocol Layer (`src/libxrpl/protocol/`)

- Core protocol definitions including transaction formats, ledger formats
- Serialization and deserialization logic
- Cryptographic primitives and signing
- Basic data types (amounts, accounts, assets)

### Networking (`src/xrpld/overlay/`)

- Peer-to-peer network protocol implementation
- Message compression and routing
- Cluster management and peer reservations

### Storage (`src/xrpld/nodestore/`)

- Persistent storage abstraction for ledger data
- Backend implementations (RocksDB, NuDB, etc.)
- Caching and rotation strategies

### Testing Infrastructure

- **`csf/`**: Consensus Simulation Framework for testing consensus behavior
- **`jtx/`**: JSON Transaction framework for integration testing
- **`unit_test/`**: Unit testing utilities and base classes

## External Dependencies (`external/`)

- **`secp256k1/`**: Elliptic curve cryptography library
- **`ed25519-donna/`**: Ed25519 signature implementation
- **`antithesis-sdk/`**: Antithesis testing framework integration

## Build System

- **`cmake/`**: CMake modules and build configuration
- **`conan/`**: Dependency management with Conan
- **Root CMakeLists.txt**: Main build configuration

## Documentation (`docs/`)

- Design documents and architectural diagrams
- Consensus algorithm documentation
- Build and development guides
- Doxygen configuration for API documentation

This structure reflects a modular design where:
- `libxrpl` provides reusable protocol and utility components
- `xrpld` implements the server application using libxrpl
- `test` ensures quality through comprehensive testing
- Clear separation between protocol, application, and infrastructure layers