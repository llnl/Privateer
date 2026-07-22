#pragma once

#include <string>
#include <cstddef>
#include <cstdint>
#include <map>

/**
 * @brief Abstract base class for block storage implementations
 * 
 * This class defines the interface for all block storage backends.
 * Concrete implementations should inherit from this class and implement
 * the pure virtual methods.
 */
class block_storage_base {
public:
    /**
     * @brief Virtual destructor
     */
    virtual ~block_storage_base() = default;

    /**
     * @brief Store a block to storage
     * @param buffer Pointer to block data
     * @param write_to_file Whether to write immediately
     * @param block_index Index of the block
     * @return std::string Hash of the stored block
     */
    virtual std::string store_block(void* buffer, bool write_to_file, uint64_t block_index) = 0;

    /**
     * @brief Stash a block to temporary storage
     * @param block_start Pointer to block data
     * @param block_index Index of the block
     * @return bool Success status
     */
    virtual bool stash_block(void* block_start, uint64_t block_index) = 0;

    /**
     * @brief Remove a block from stash
     * @param block_index Index of the block
     * @return bool Success status
     */
    virtual bool unstash_block(uint64_t block_index) = 0;

    /**
     * @brief Commit a stashed block to permanent storage
     * @param block_index Index of the block
     * @return std::string Hash of the committed block
     */
    virtual std::string commit_stash_block(uint64_t block_index) = 0;

    /**
     * @brief Get the full path to a block
     * @param block_index Index of the block
     * @param block_hash Hash of the block
     * @return std::string Full path to the block
     */
    virtual std::string get_block_full_path(uint64_t block_index, std::string block_hash) = 0;

    /**
     * @brief Get the block granularity (size)
     * @return size_t Block size in bytes
     */
    virtual size_t get_block_granularity() = 0;

    /**
     * @brief Get the stash path for a block
     * @param block_index Index of the block
     * @return std::string Path to stashed block
     */
    virtual std::string get_block_stash_path(size_t block_index) = 0;

    /**
     * @brief Get the blocks storage path
     * @return std::string Base path for blocks
     */
    virtual std::string get_blocks_path() = 0;

    /**
     * @brief Copy a block to stash tier
     * @param base_block Path to base block
     * @param stash_block Path to stash block
     * @return bool Success status
     */
    virtual bool copy_to_stash(std::string base_block, std::string stash_block) = 0;

    /**
     * @brief Get version block granularity from storage path
     * @param blocks_path Path to blocks storage
     * @return size_t Block granularity
     */
    static size_t get_version_block_granularity(std::string blocks_path);

protected:
    std::string base_directory;
    std::string stash_directory;
    size_t block_granularity;
    std::map<uint64_t, std::string> stash_block_ids;
    std::map<uint64_t, std::string> stash_committed_block_ids;

    static const size_t NUM_SUBDIRS = 1024;
    static const size_t HASH_PREFIX_LENGTH = 6;

    /**
     * @brief Check if storage is multi-tiered
     */
    virtual bool is_multi_tiered() = 0;
};
