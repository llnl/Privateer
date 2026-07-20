#pragma once

#include <privateer/virtual_memory_manager_base.hpp>
#include <list>
#include <set>
#include <mutex>

/**
 * @brief SIGACTION-based virtual memory manager implementation
 * 
 * This implementation uses SIGACTION to handle page faults and manage
 * virtual memory through signal handlers.
 */
class sigaction_virtual_memory_manager : public virtual_memory_manager_base {
public:
    /**
     * @brief Constructor for creating a new virtual memory region
     */
    sigaction_virtual_memory_manager(void* start_address, size_t region_max_capacity, 
                                    size_t block_size, std::string version_metadata_path, 
                                    std::string blocks_path, std::string stash_path, 
                                    bool allow_overwrite);

    /**
     * @brief Constructor for opening an existing virtual memory region
     */
    sigaction_virtual_memory_manager(void* addr, std::string version_metadata_path, 
                                    std::string stash_path, bool read_only);

    virtual ~sigaction_virtual_memory_manager();

    // Implement pure virtual methods
    void* get_region_start_address() override;
    size_t current_region_capacity() override;
    size_t get_block_size() override;
    void msync() override;
    bool snapshot(const char* version_metadata_path) override;
    int close() override;

    /**
     * @brief SIGSEGV handler for page faults
     */
    void handler(int sig, siginfo_t* si, void* ctx_void_ptr);

protected:
    void update_metadata(int sub_region_index = 0) override;

private:
    void evict_if_needed();

    // SIGACTION-specific members
    std::list<uint64_t> clean_lru;
    std::list<uint64_t> dirty_lru;
    std::set<uint64_t> stash_set;
    std::set<uint64_t> present_blocks;
    
    static const size_t MAX_MEM_DEFAULT_BLOCKS = 16384;
    std::mutex sig_handler_mutex;
};
