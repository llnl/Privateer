#include <privateer/block_storage_factory.hpp>
#include <privateer/posix_block_storage.hpp>
#include <privateer/smartcache_block_storage.hpp>
#include <spdlog/spdlog.h>

block_storage_base* block_storage_factory::create(std::string base_directory) {
#ifdef USE_SMARTCACHE
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating SmartCache block storage");
    return new smartcache_block_storage(base_directory);
#else
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating POSIX block storage");
    return new posix_block_storage(base_directory);
#endif
}

block_storage_base* block_storage_factory::create(std::string base_directory, 
                                                  std::string stash_directory) {
#ifdef USE_SMARTCACHE
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating SmartCache block storage with stash");
    return new smartcache_block_storage(base_directory, stash_directory);
#else
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating POSIX block storage with stash");
    return new posix_block_storage(base_directory, stash_directory);
#endif
}

block_storage_base* block_storage_factory::create(std::string base_directory, 
                                                  size_t block_granularity) {
#ifdef USE_SMARTCACHE
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating new SmartCache block storage");
    return new smartcache_block_storage(base_directory, block_granularity);
#else
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating new POSIX block storage");
    return new posix_block_storage(base_directory, block_granularity);
#endif
}

block_storage_base* block_storage_factory::create(std::string base_directory, 
                                                  std::string stash_directory,
                                                  size_t block_granularity) {
#ifdef USE_SMARTCACHE
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating new SmartCache block storage with stash");
    return new smartcache_block_storage(base_directory, stash_directory, block_granularity);
#else
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "block_storage_factory: Creating new POSIX block storage with stash");
    return new posix_block_storage(base_directory, stash_directory, block_granularity);
#endif
}
