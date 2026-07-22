#pragma once

#include <privateer/block_storage_base.hpp>
#include <filesystem>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <spdlog/spdlog.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <atomic>
#include <string>
#include <mutex>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>

#include <privateer/utility/sha256_hash.hpp>
#include <privateer/utility/file_util.hpp>
#include <privateer/utility/system.hpp>
#ifdef USE_COMPRESSION
#include <privateer/utility/compression.hpp>
#endif

/**
 * @brief POSIX file-based block storage implementation
 * 
 * This implementation uses standard POSIX file I/O operations to store
 * blocks in the filesystem.
 */
class posix_block_storage : public block_storage_base {
public:
    // Open
    posix_block_storage(std::string base_directory);

    // Open with Stash
    posix_block_storage(std::string base_directory, std::string stash_directory);

    // Create
    posix_block_storage(std::string base_directory, size_t block_granularity);

    // Create with Stash
    posix_block_storage(std::string base_directory, std::string stash_directory, 
                        size_t block_granularity_arg);

    // Copy constructor
    posix_block_storage(const posix_block_storage& other);

    virtual ~posix_block_storage() = default;

    // Implement pure virtual methods from base class
    std::string store_block(void* buffer, bool write_to_file, uint64_t block_index) override;
    bool stash_block(void* block_start, uint64_t block_index) override;
    bool unstash_block(uint64_t block_index) override;
    std::string commit_stash_block(uint64_t block_index) override;
    std::string get_block_full_path(uint64_t block_index, std::string block_hash) override;
    size_t get_block_granularity() override;
    std::string get_block_stash_path(size_t block_index) override;
    std::string get_blocks_path() override;
    bool copy_to_stash(std::string base_block, std::string stash_block) override;

protected:
    bool is_multi_tiered() override;

private:
    void create(std::string base_directory_path, std::string stash_directory_path, 
                size_t block_granularity_arg);
    void open(std::string base_directory_path, std::string stash_directory_path);
    
    std::string get_blocks_subdirectory(std::string block_hash, bool on_stash);
    
    std::string store_block(void* buffer, bool write_to_file, uint64_t block_index, 
                           bool on_stash, std::string pre_computed_hash);
    
    std::pair<int, std::string> create_temporary_unique_block(std::string prefix, 
                                                               char* name_template, 
                                                               uint64_t block_index, 
                                                               bool on_stash);

    std::mutex create_block_directory_mutex;
    size_t num_subdirs = 1024;
    size_t hash_prefix_length = 6;
};
