#pragma once

#include <privateer/virtual_memory_manager_base.hpp>

#ifdef USERFAULTFD
#define _GNU_SOURCE
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/ioctl.h>

#include <queue>
#include <chrono>
#include <vector>
#include <atomic>

#include <privateer/utility/fault_event.hpp>
#include <privateer/utility/event_queue.hpp>
#endif

/**
 * @brief Userfaultfd-based virtual memory manager implementation
 * 
 * This implementation uses the Linux userfaultfd mechanism to handle
 * page faults asynchronously with dedicated handler threads.
 */
class uffd_virtual_memory_manager : public virtual_memory_manager_base {
public:
    /**
     * @brief Constructor for creating a new virtual memory region
     */
    uffd_virtual_memory_manager(void* start_address, size_t region_max_capacity, 
                                size_t block_size, std::string version_metadata_path, 
                                std::string blocks_path, std::string stash_path, 
                                bool allow_overwrite);

    /**
     * @brief Constructor for opening an existing virtual memory region
     */
    uffd_virtual_memory_manager(void* addr, std::string version_metadata_path, 
                                std::string stash_path, bool read_only);

    virtual ~uffd_virtual_memory_manager();

    // Implement pure virtual methods
    void* get_region_start_address() override;
    size_t current_region_capacity() override;
    size_t get_block_size() override;
    void msync() override;
    void msync(int sub_region_index);
    bool snapshot(const char* version_metadata_path) override;
    int close() override;

#ifdef USERFAULTFD
    /**
     * @brief Userfaultfd handler for a specific sub-region
     */
    void* handler(uint64_t sub_region_index);

    /**
     * @brief Set the userfaultfd file descriptor
     */
    void set_uffd(uint64_t uffd);

    /**
     * @brief Static helper for handler thread
     */
    static void* handler_helper(void* context);

    /**
     * @brief Get block address from fault address
     */
    uint64_t get_block_address(uint64_t fault_address);

    /**
     * @brief Add a page fault event to the queue
     */
    void add_page_fault_event(utility::fault_event fevent);

    /**
     * @brief Add a page fault event to all queues
     */
    void add_page_fault_event_all(utility::fault_event fevent);

    /**
     * @brief Start handler threads
     */
    void start_handler_thread();

    /**
     * @brief Stop handler threads
     */
    void stop_handler_thread();

    /**
     * @brief Deactivate userfaultfd thread
     */
    void deactivate_uffd_thread();

    /**
     * @brief Get next sub-region index
     */
    int get_next_sub_region();
#endif

protected:
    void update_metadata(int sub_region_index = 0) override;

private:
#ifdef USERFAULTFD
    void evict_if_needed(int sub_region_index);
    bool is_valid_uffd(int uffd);

    // UFFD-specific members
    int prot;
    std::vector<std::list<uint64_t>> clean_lru;
    std::vector<std::list<uint64_t>> dirty_lru;
    std::vector<std::set<uint64_t>> stash_set;
    std::vector<std::set<uint64_t>> present_blocks;
    std::vector<std::mutex> sub_regions_mutex_list;

    void* zero_page;
    void* temp_buffer;

    std::vector<long> dequeue_ts;
    std::vector<long> handled_ts;

    static const size_t FILE_GRANULARITY_DEFAULT_BYTES = 2097152;
    static const size_t MAX_MEM_DEFAULT_BLOCKS = 16384;
    static const int NUM_HANDLING_THREADS_DEFAULT = 1;
    static const int NUM_MSYNC_THREADS_DEFAULT = 1;

    std::mutex handler_mutex_global;
    std::mutex* blocks_locks;
    std::mutex add_event_mutex;
    pthread_mutex_t handler_mutex;
    long m_uffd;
    std::atomic<bool> uffd_active;
    int uffd_pipe[2];
    
    std::vector<utility::event_queue<utility::fault_event>> events_queues;
    int num_handling_threads;
    int num_msync_threads;
    std::vector<pthread_t> fault_handling_threads;
    std::atomic<long> debug = 0;
    std::atomic<uint64_t> next_sub_region = -1;
#endif
};
