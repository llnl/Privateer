#pragma once

#include <privateer/virtual_memory_manager_base.hpp>
#include <string>

/**
 * @brief Factory class for creating virtual memory manager instances
 * 
 * This factory creates the appropriate virtual memory manager implementation
 * based on compiler flags (SIGACTION vs USERFAULTFD).
 */
class virtual_memory_manager_factory {
public:
    /**
     * @brief Create a new virtual memory manager instance
     * 
     * @param start_address Starting address for the memory region
     * @param region_max_capacity Maximum capacity of the region
     * @param block_size Size of each block
     * @param version_metadata_path Path to version metadata
     * @param blocks_path Path to blocks storage
     * @param stash_path Path to stash storage
     * @param allow_overwrite Whether to allow overwriting existing data
     * @return Pointer to virtual memory manager instance
     */
    static virtual_memory_manager_base* create(void* start_address, 
                                               size_t region_max_capacity,
                                               size_t block_size, 
                                               std::string version_metadata_path,
                                               std::string blocks_path, 
                                               std::string stash_path,
                                               bool allow_overwrite);

    /**
     * @brief Open an existing virtual memory manager instance
     * 
     * @param addr Address to map the region
     * @param version_metadata_path Path to version metadata
     * @param stash_path Path to stash storage
     * @param read_only Whether to open in read-only mode
     * @return Pointer to virtual memory manager instance
     */
    static virtual_memory_manager_base* open(void* addr, 
                                            std::string version_metadata_path,
                                            std::string stash_path, 
                                            bool read_only);

private:
    virtual_memory_manager_factory() = delete;  // Prevent instantiation
};
