#pragma once

#include <privateer/block_storage_base.hpp>
#include <memory>
#include <string>

/**
 * @brief Factory class for creating block storage instances
 * 
 * This factory creates the appropriate block storage implementation
 * based on compiler flags and configuration.
 */
class block_storage_factory {
public:
    /**
     * @brief Create a block storage instance (open existing)
     * @param base_directory Path to the base storage directory
     * @return Pointer to block storage instance
     */
    static block_storage_base* create(std::string base_directory);

    /**
     * @brief Create a block storage instance with stash (open existing)
     * @param base_directory Path to the base storage directory
     * @param stash_directory Path to the stash directory
     * @return Pointer to block storage instance
     */
    static block_storage_base* create(std::string base_directory, std::string stash_directory);

    /**
     * @brief Create a new block storage instance
     * @param base_directory Path to the base storage directory
     * @param block_granularity Size of each block
     * @return Pointer to block storage instance
     */
    static block_storage_base* create(std::string base_directory, size_t block_granularity);

    /**
     * @brief Create a new block storage instance with stash
     * @param base_directory Path to the base storage directory
     * @param stash_directory Path to the stash directory
     * @param block_granularity Size of each block
     * @return Pointer to block storage instance
     */
    static block_storage_base* create(std::string base_directory, std::string stash_directory, 
                                     size_t block_granularity);

private:
    block_storage_factory() = delete;  // Prevent instantiation
};
