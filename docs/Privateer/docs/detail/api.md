# API

Privateer's API is designed to be a drop in replacement for mmap() and msync()

```cpp
#include<privateer.hpp>
// Constructs a new Privateer object
Privateer(int action, const char* base_path)

// Constructs a new Privateer object with a specified stash path
Privateer(int action, const char* base_path, const char* stash_base_path)

// Destroys the Privateer object
~Privateer()

// Create a Privateer datastore, returns the start address of a datastore region.
void* create(void* addr, const char* version_metadata_path, 
size_t region_size, bool allow_overwrite)

// Opens an existing Privateer datastore, returns the start address of a datastore region.
void* open(void* addr, const char* version_metadata_path)

// Opens an existing Privateer datastore with read-only permission,
// returns the start address of a datastore region.
void* open_read_only(void* addr, const char* version_metadata_path)

// Opens an existing Privateer datastore without modifying
// the original datastore. A copy is made similar to snapshot().
// Returns the start address of a datastore region.
void* open_immutable(void* addr, const char* version_metadata_path, 
const char* new_version_metadata_path)

// Commits changes to datastore
void msync()

// Commits changes and takes a snapshot of current changes made so far
bool snapshot(const char* version_metadata_path)

// Gets the block size
size_t get_block_size()

// Gets the start address of a datastore region.
void* data()

// Checks version metadata
bool version_exists(const char* version_metadata_path)

// Gets region size
size_t region_size()
  
// Gets version capacity
static size_t version_capacity(std::string version_path)
  
// Gets version block size
static size_t version_block_size(std::string version_path)
```

### Including Privateer

```cpp
#include<privateer.hpp>
```

### Creating and memory-mapping a new data store
```cpp
  Privateer privateer(Privateer::CREATE, "datastore");
  void* data = (size_t*) privateer.create(nullptr, "v0", size_bytes, true);
```

### Opening and memory-mapping an existing data store
```cpp
  Privateer privateer(Privateer::OPEN, "datastore");
  void* data = (size_t*) privateer.open(nullptr, "v0");
```

### Writeback
```cpp
  privateer.msync();
```
