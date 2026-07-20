# Privateer Library Refactoring Documentation

## Overview

This document describes the object-oriented refactoring of the Privateer library into a proper C++ library structure with abstract base classes, inheritance hierarchy, and factory patterns.

## Architecture

### 1. Abstract Base Classes

#### `virtual_memory_manager_base`
- **Location**: `include/privateer/virtual_memory_manager_base.hpp`
- **Purpose**: Defines the common interface for all virtual memory manager implementations
- **Key Methods**:
  - `get_region_start_address()`: Returns the start address of the managed region
  - `current_region_capacity()`: Returns current capacity
  - `get_block_size()`: Returns block size
  - `msync()`: Synchronize dirty pages to storage
  - `snapshot()`: Create a snapshot
  - `close()`: Close and cleanup
  - `update_metadata()`: Pure virtual method for updating metadata

#### `block_storage_base`
- **Location**: `include/privateer/block_storage_base.hpp`
- **Purpose**: Defines the common interface for all block storage backends
- **Key Methods**:
  - `store_block()`: Store a block to storage
  - `stash_block()`: Stash a block temporarily
  - `unstash_block()`: Remove from stash
  - `commit_stash_block()`: Commit stashed block
  - `get_block_full_path()`: Get block path
  - `get_block_granularity()`: Get block size
  - `is_multi_tiered()`: Pure virtual method for tier detection

### 2. Derived Classes

#### Virtual Memory Manager Implementations

##### `sigaction_virtual_memory_manager`
- **Location**: `include/privateer/sigaction_virtual_memory_manager.hpp`, `src/privateer/sigaction_virtual_memory_manager.cpp`
- **Purpose**: SIGACTION-based implementation using signal handlers for page faults
- **Specific Features**:
  - Single-threaded signal handler
  - LRU lists for clean and dirty pages
  - Stash set for pending writes
  - Present blocks tracking

##### `uffd_virtual_memory_manager`
- **Location**: `include/privateer/uffd_virtual_memory_manager.hpp`, `src/privateer/uffd_virtual_memory_manager.cpp`
- **Purpose**: Userfaultfd-based implementation with asynchronous page fault handling
- **Specific Features**:
  - Multi-threaded handler support
  - Sub-region partitioning
  - Event queues for fault handling
  - Background handler threads

#### Block Storage Implementations

##### `posix_block_storage`
- **Location**: `include/privateer/posix_block_storage.hpp`, `src/privateer/posix_block_storage.cpp`
- **Purpose**: Standard POSIX file I/O based block storage
- **Features**:
  - Direct file system operations
  - Multi-tier support (base + stash)
  - Optional compression support
  - Hash-based deduplication

##### `smartcache_block_storage`
- **Location**: `include/privateer/smartcache_block_storage.hpp`, `src/privateer/smartcache_block_storage.cpp`
- **Purpose**: SmartCache-optimized block storage (placeholder)
- **Status**: Placeholder implementation for future SmartCache integration
- **Note**: Currently returns errors - needs full implementation

### 3. Factory Pattern

#### `virtual_memory_manager_factory`
- **Location**: `include/privateer/virtual_memory_manager_factory.hpp`, `src/privateer/virtual_memory_manager_factory.cpp`
- **Purpose**: Creates appropriate VMM implementation based on compiler flags
- **Selection Criteria**:
  - If `USERFAULTFD` is defined → `uffd_virtual_memory_manager`
  - If `SIGACTION` is defined → `sigaction_virtual_memory_manager`
  - Error if neither is defined

#### `block_storage_factory`
- **Location**: `include/privateer/block_storage_factory.hpp`, `src/privateer/block_storage_factory.cpp`
- **Purpose**: Creates appropriate block storage backend
- **Selection Criteria**:
  - If `USE_SMARTCACHE` is defined → `smartcache_block_storage`
  - Otherwise → `posix_block_storage`

## Directory Structure

```
Privateer/
├── include/privateer/
│   ├── c_api/
│   │   └── privateer.h
│   ├── privateer.hpp                              # Main API header
│   ├── virtual_memory_manager_base.hpp            # VMM base class
│   ├── sigaction_virtual_memory_manager.hpp       # SIGACTION VMM
│   ├── uffd_virtual_memory_manager.hpp            # UFFD VMM
│   ├── virtual_memory_manager_factory.hpp         # VMM factory
│   ├── block_storage_base.hpp                     # Storage base class
│   ├── posix_block_storage.hpp                    # POSIX storage
│   ├── smartcache_block_storage.hpp               # SmartCache storage
│   └── block_storage_factory.hpp                  # Storage factory
├── src/privateer/
│   ├── virtual_memory_manager_base.cpp
│   ├── sigaction_virtual_memory_manager.cpp
│   ├── uffd_virtual_memory_manager.cpp
│   ├── virtual_memory_manager_factory.cpp
│   ├── block_storage_base.cpp
│   ├── posix_block_storage.cpp
│   ├── smartcache_block_storage.cpp
│   └── block_storage_factory.cpp
├── src/
│   └── privateer_c.cpp                            # C API implementation
└── CMakeLists.txt                                 # Updated build config
```

## Build Configuration

The `CMakeLists.txt` has been updated to compile all new source files:

```cmake
# Base classes and interfaces
set(privateer_base_headers ...)
set(privateer_base_src ...)

# Derived implementations
set(privateer_impl_headers ...)
set(privateer_impl_src ...)

# Factories
set(privateer_factory_headers ...)
set(privateer_factory_src ...)

# Combine all sources
set(privateer_src ${privateer_base_src} ${privateer_impl_src} ${privateer_factory_src} ...)
add_library(privateer STATIC ${privateer_src})
```

## Compiler Flags

The following flags control which implementations are selected:

- **`SIGACTION`**: Use SIGACTION-based virtual memory manager (default)
- **`USERFAULTFD`**: Use userfaultfd-based virtual memory manager
- **`USE_SMARTCACHE`**: Use SmartCache block storage (placeholder)
- **`USE_COMPRESSION`**: Enable compression in block storage

## Usage Example

The high-level `Privateer` class API remains unchanged:

```cpp
#include "privateer/privateer.hpp"

// Create new datastore
Privateer ds(Privateer::CREATE, "/path/to/datastore");
void* region = ds.create(nullptr, "version1", 1024*1024*1024, false);

// Use the region
// ... write data ...

// Snapshot
ds.snapshot("version2");

// Open existing datastore
Privateer ds2(Privateer::OPEN, "/path/to/datastore");
void* region2 = ds2.open(nullptr, "version2");
```

## Implementation Status

### Completed ✅
1. Abstract base classes for VMM and block storage
2. POSIX block storage full implementation
3. Factory pattern for both VMM and block storage
4. SmartCache block storage placeholder
5. VMM derived class headers (sigaction and uffd)
6. VMM derived class implementation skeletons
7. Updated CMakeLists.txt

### TODO 📋
1. **Complete VMM Implementations**: The sigaction and uffd implementation files contain placeholder logic. They need to be filled in with the full implementation from the original `virtual_memory_manager.hpp`:
   - Constructor logic (memory mapping, initialization)
   - `msync()` implementation (write dirty LRU, commit stash)
   - `handler()` implementation (page fault handling)
   - `update_metadata()` implementation
   - `evict_if_needed()` implementation
   - `snapshot()` implementation

2. **SmartCache Integration**: Implement actual SmartCache functionality in `smartcache_block_storage`

3. **Update Privateer.hpp**: Modify the main `Privateer` class to use the factory pattern:
   ```cpp
   // Instead of:
   vmm = new virtual_memory_manager(...);
   
   // Use:
   vmm = virtual_memory_manager_factory::create(...);
   ```

4. **Testing**: Ensure all existing tests pass with the new structure

5. **Documentation**: Update API documentation

## Migration Notes

### For Developers

When adding new functionality:

1. **Add new VMM implementation**:
   - Create new derived class from `virtual_memory_manager_base`
   - Implement all pure virtual methods
   - Add to factory with appropriate compiler flag

2. **Add new storage backend**:
   - Create new derived class from `block_storage_base`
   - Implement all pure virtual methods
   - Add to factory with appropriate compiler flag

### Benefits of This Architecture

1. **Separation of Concerns**: Each implementation is isolated in its own file
2. **Testability**: Each component can be tested independently
3. **Extensibility**: Easy to add new implementations
4. **Type Safety**: Virtual functions ensure interface compliance
5. **Factory Pattern**: Centralized creation logic based on build configuration
6. **Maintainability**: Smaller, focused files instead of one monolithic header

## Next Steps

To complete the refactoring:

1. Port the full implementation code from `virtual_memory_manager.hpp` to the derived class `.cpp` files
2. Update `privateer.hpp` to use the factory methods
3. Test with both SIGACTION and USERFAULTFD configurations
4. Implement SmartCache integration if needed
5. Update documentation and examples

## Contact

For questions about this refactoring, consult the implementation files and the original `virtual_memory_manager.hpp` for reference.
