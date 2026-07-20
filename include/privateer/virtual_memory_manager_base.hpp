#pragma once

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <cstddef>
#include <signal.h>

#include <privateer/block_storage_base.hpp>
#include <privateer/utility/file_util.hpp>
#include <privateer/utility/system.hpp>
#include <spdlog/spdlog.h>

/**
 * @brief Abstract base class for virtual memory managers
 * 
 * This class defines the interface for all virtual memory manager implementations.
 * Concrete implementations should inherit from this class and implement the
 * pure virtual methods.
 */
class virtual_memory_manager_base {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~virtual_memory_manager_base() = default;

    /**
     * @brief Get the start address of the managed region
     * @return void* Start address
     */
    virtual void* get_region_start_address() = 0;

    /**
     * @brief Get the current region capacity
     * @return size_t Current capacity in bytes
     */
    virtual size_t current_region_capacity() = 0;

    /**
     * @brief Get the block size
     * @return size_t Block size in bytes
     */
    virtual size_t get_block_size() = 0;

    /**
     * @brief Synchronize dirty pages to storage
     */
    virtual void msync() = 0;

    /**
     * @brief Create a snapshot of the current state
     * @param version_metadata_path Path to store snapshot metadata
     * @return bool Success status
     */
    virtual bool snapshot(const char* version_metadata_path) = 0;

    /**
     * @brief Close the virtual memory manager
     * @return int Status code
     */
    virtual int close() = 0;

    /**
     * @brief Handle page fault (SIGACTION-based implementations)
     * @param sig Signal number
     * @param si Signal info structure
     * @param ctx_void_ptr Context pointer
     */
    virtual void handler(int sig, siginfo_t* si, void* ctx_void_ptr) {}

    /**
     * @brief Get version capacity from metadata path
     * @param version_path Path to version metadata
     * @return size_t Version capacity
     */
    static size_t version_capacity(std::string version_path);

    /**
     * @brief Get version block size from metadata path
     * @param version_path Path to version metadata
     * @return size_t Block size
     */
    static size_t version_block_size(std::string version_path);

protected:
    // Common data members
    void* m_region_start_address;
    size_t m_block_size;
    size_t m_region_max_capacity;
    size_t m_max_mem_size;
    std::string m_version_metadata_path;
    bool m_read_only;
    int metadata_fd;
    std::string* blocks_ids;
    block_storage_base* m_block_storage;

    static const size_t HASH_SIZE = 64;
    static const std::string EMPTY_BLOCK_HASH;

    /**
     * @brief Create version metadata directory and files
     */
    void create_version_metadata(const char* version_metadata_dir_path, 
                                 const char* block_storage_dir_path, 
                                 size_t version_capacity, 
                                 bool allow_overwrite);

    /**
     * @brief Update metadata file with current block information
     */
    virtual void update_metadata(int sub_region_index = 0) = 0;
};
