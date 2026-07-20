#include <privateer/uffd_virtual_memory_manager.hpp>
#include <privateer/block_storage_factory.hpp>
#include <privateer/utility/system.hpp>

#include <cassert>
#include <fstream>

#ifdef USERFAULTFD

// Constructor for creating new region
uffd_virtual_memory_manager::uffd_virtual_memory_manager(
    void* start_address, size_t region_max_capacity, size_t block_size,
    std::string version_metadata_path, std::string blocks_path, 
    std::string stash_path, bool allow_overwrite) {
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager() - start");
    
    const std::lock_guard<std::mutex> lock(handler_mutex_global);

    size_t pagesize = sysconf(_SC_PAGE_SIZE);
    if (((uint64_t)start_address) % pagesize != 0) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Start_address is not system-page aligned");
        exit(-1);
    }

    num_handling_threads = utility::get_environment_variable("NUM_HANDLING_THREADS");
    if (std::isnan(num_handling_threads) || num_handling_threads == 0) {
        num_handling_threads = NUM_HANDLING_THREADS_DEFAULT;
    }
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "num_handling_threads: {}", num_handling_threads);

    num_msync_threads = utility::get_environment_variable("NUM_MSYNC_THREADS");
    if (std::isnan(num_msync_threads) || num_msync_threads == 0) {
        num_msync_threads = NUM_MSYNC_THREADS_DEFAULT;
    }
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "num_msync_threads: {}", num_msync_threads);

    m_block_size = block_size;
    if (region_max_capacity % m_block_size != 0 && region_max_capacity != 0) {
        region_max_capacity = ((region_max_capacity / m_block_size) + 1) * m_block_size;
    }

    size_t max_mem_size_blocks = utility::get_environment_variable("PRIVATEER_MAX_MEM_BLOCKS");
    if (std::isnan((double)max_mem_size_blocks) || max_mem_size_blocks == 0) {
        max_mem_size_blocks = MAX_MEM_DEFAULT_BLOCKS;
    }

#ifndef ENABLE_PAGE_EVICTION
    if (max_mem_size_blocks * m_block_size < region_max_capacity) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Page eviction not permitted");
    }
#endif

    create_version_metadata(version_metadata_path.c_str(), blocks_path.c_str(), region_max_capacity, allow_overwrite);

    m_region_max_capacity = region_max_capacity;
    m_max_mem_size = max_mem_size_blocks * m_block_size;
    m_version_metadata_path = version_metadata_path;

    m_block_storage = block_storage_factory::create(blocks_path, stash_path, m_block_size);

    int flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE;
    if (start_address != nullptr) {
        flags |= MAP_FIXED;
    }

    prot = PROT_READ | PROT_WRITE;
    m_region_start_address = mmap(start_address, m_region_max_capacity, prot, flags, -1, 0);
    if (m_region_start_address == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: mmap-ing region starting address - {}", strerror(errno));
        exit(-1);
    }

    size_t num_blocks = m_region_max_capacity / m_block_size;
    blocks_ids = new std::string[num_blocks];
    for (size_t i = 0; i < num_blocks; i++) {
        blocks_ids[i] = EMPTY_BLOCK_HASH;
    }

    m_read_only = false;

    uffd_active = true;
    sub_regions_mutex_list = std::vector<std::mutex>(num_handling_threads);
    events_queues = std::vector<utility::event_queue<utility::fault_event>>(num_handling_threads);
    for (int i = 0; i < num_handling_threads; i++) {
        clean_lru.emplace_back();
        dirty_lru.emplace_back();
        stash_set.emplace_back();
        present_blocks.emplace_back();
    }

    char* tmp;
    if (posix_memalign((void**)&tmp, m_block_size, m_block_size)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error posix_memalign - {}", strerror(errno));
    }
    zero_page = mmap((void*)tmp, m_block_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE, -1, 0);
    if (zero_page == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error mmap zero page ", strerror(errno));
        exit(-1);
    }
    temp_buffer = mmap(nullptr, m_block_size * num_handling_threads, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (temp_buffer == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error mmap zero page ", strerror(errno));
        exit(-1);
    }
    start_handler_thread();
}

// Constructor for opening existing region
uffd_virtual_memory_manager::uffd_virtual_memory_manager(
    void* addr, std::string version_metadata_path, 
    std::string stash_path, bool read_only) {
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager() - open");
    
    const std::lock_guard<std::mutex> lock(handler_mutex_global);

    num_handling_threads = utility::get_environment_variable("NUM_HANDLING_THREADS");
    if (std::isnan(num_handling_threads) || num_handling_threads == 0) {
        num_handling_threads = NUM_HANDLING_THREADS_DEFAULT;
    }

    num_msync_threads = utility::get_environment_variable("NUM_MSYNC_THREADS");
    if (std::isnan(num_msync_threads) || num_msync_threads == 0) {
        num_msync_threads = NUM_MSYNC_THREADS_DEFAULT;
    }

    m_version_metadata_path = version_metadata_path;
    std::string blocks_path_file_name = std::string(m_version_metadata_path) + "/_blocks_path";
    std::ifstream blocks_path_file;
    std::string blocks_dir_path;

    blocks_path_file.open(blocks_path_file_name);
    if (!blocks_path_file.is_open()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error opening blocks file path at {}", blocks_path_file_name);
    }
    if (!std::getline(blocks_path_file, blocks_dir_path)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error reading blocks path file");
    }
    m_block_storage = block_storage_factory::create(blocks_dir_path, stash_path);
    m_block_size = m_block_storage->get_block_granularity();

    std::string metadata_file_name = std::string(m_version_metadata_path) + "/_metadata";
    int flags = read_only ? O_RDONLY : O_RDWR;
    int metadata_fd = ::open(metadata_file_name.c_str(), flags, (mode_t)0666);
    assert(metadata_fd != -1);
    struct stat st;
    fstat(metadata_fd, &st);
    size_t metadata_size = st.st_size;

    m_region_max_capacity = version_capacity(version_metadata_path);

    size_t num_blocks = m_region_max_capacity / m_block_size;
    int mmap_flags = MAP_ANONYMOUS | MAP_PRIVATE | MAP_NORESERVE;
    if (addr != nullptr) {
        mmap_flags |= MAP_FIXED;
    }

    prot = read_only ? PROT_READ : (PROT_READ | PROT_WRITE);
    m_region_start_address = mmap(addr, m_region_max_capacity, prot, mmap_flags, -1, 0);
    if (m_region_start_address == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: mmap error - {}", strerror(errno));
        exit(-1);
    }

    blocks_ids = new std::string[num_blocks];
    char* metadata_content = new char[metadata_size];
    size_t read = ::pread(metadata_fd, (void*)metadata_content, metadata_size, 0);
    if (read == (size_t)-1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error reading metadata - {}", strerror(errno));
        exit(-1);
    }
    std::string all_hashes(metadata_content, metadata_size);
    for (size_t i = 0; i < metadata_size; i += HASH_SIZE) {
        std::string block_hash(all_hashes, i, HASH_SIZE);
        blocks_ids[i / HASH_SIZE] = block_hash;
    }
    size_t num_occupied_blocks = metadata_size / HASH_SIZE;
    for (size_t i = num_occupied_blocks; i < num_blocks; i++) {
        blocks_ids[i] = EMPTY_BLOCK_HASH;
    }
    delete [] metadata_content;

    size_t max_mem_size_blocks = utility::get_environment_variable("PRIVATEER_MAX_MEM_BLOCKS");
    if (std::isnan((double)max_mem_size_blocks) || max_mem_size_blocks == 0) {
        max_mem_size_blocks = MAX_MEM_DEFAULT_BLOCKS;
    }
    m_max_mem_size = max_mem_size_blocks * m_block_size;

#ifndef ENABLE_PAGE_EVICTION
    if (max_mem_size_blocks * m_block_size < m_region_max_capacity) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Page eviction not permitted");
    }
#endif

    m_read_only = read_only;

    sub_regions_mutex_list = std::vector<std::mutex>(num_handling_threads);
    events_queues = std::vector<utility::event_queue<utility::fault_event>>(num_handling_threads);
    for (int i = 0; i < num_handling_threads; i++) {
        clean_lru.emplace_back();
        dirty_lru.emplace_back();
        stash_set.emplace_back();
        present_blocks.emplace_back();
    }

    char* tmp;
    if (posix_memalign((void**)&tmp, m_block_size, m_block_size)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error posix_memalign - {}", strerror(errno));
    }
    zero_page = mmap((void*)tmp, m_block_size, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_POPULATE, -1, 0);
    if (zero_page == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error mmap zero page - {}", strerror(errno));
        exit(-1);
    }

    temp_buffer = mmap(nullptr, m_block_size * num_handling_threads, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (temp_buffer == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error mmap zero page - {}", strerror(errno));
        exit(-1);
    }

    uffd_active = true;
    start_handler_thread();
}

uffd_virtual_memory_manager::~uffd_virtual_memory_manager() {
    SPDLOG_LOGGER_TRACE(spdlog::default_logger(), "destructor, starting");
    if (close() != 0) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "Error in close()");
    }
    SPDLOG_LOGGER_TRACE(spdlog::default_logger(), "destructor, done");
}

void* uffd_virtual_memory_manager::get_region_start_address() {
    return m_region_start_address;
}

size_t uffd_virtual_memory_manager::current_region_capacity() {
    return m_region_max_capacity;
}

size_t uffd_virtual_memory_manager::get_block_size() {
    return m_block_size;
}

void uffd_virtual_memory_manager::msync() {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: msync() - start");
    
    for (int i = 0; i < num_handling_threads; i++) {
        msync(i);
    }
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: msync() - done");
}

void uffd_virtual_memory_manager::msync(int sub_region_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: msync({}) - start", sub_region_index);
    
    const std::lock_guard<std::mutex> lock(handler_mutex_global);
    std::vector<uint64_t> dirty_lru_vector(dirty_lru[sub_region_index].begin(), dirty_lru[sub_region_index].end());

#pragma omp parallel for num_threads(num_msync_threads)
    for (auto dirty_lru_iterator = dirty_lru_vector.begin(); dirty_lru_iterator != dirty_lru_vector.end(); ++dirty_lru_iterator) {
        auto* block_address = (void*)*dirty_lru_iterator;
        uint64_t block_index = ((uint64_t)block_address - (uint64_t)m_region_start_address) / m_block_size;
        bool write_block_fd = true;
        std::string block_hash = m_block_storage->store_block(block_address, write_block_fd, block_index);
        if (block_hash.empty()) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error storing block with index {}", block_index);
            exit(-1);
        }

        blocks_ids[block_index] = block_hash;
#pragma omp critical
        {
            is_valid_uffd(m_uffd);
            struct uffdio_writeprotect uffdio_writeprotect;
            uffdio_writeprotect.range.start = (uint64_t)block_address;
            uffdio_writeprotect.range.len = (uint64_t)m_block_size;
            uffdio_writeprotect.mode = UFFDIO_WRITEPROTECT_MODE_WP;
            if (ioctl(m_uffd, UFFDIO_WRITEPROTECT, &uffdio_writeprotect) == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: msync() - Error ioctl-UFFDIO_WRITEPROTECT - {}", strerror(errno));
                exit(-1);
            }
            int block_sub_region_index = ((uint64_t)block_address) % num_handling_threads;
            clean_lru[block_sub_region_index].push_front((uint64_t)block_address);
        }
    }
    dirty_lru[sub_region_index].clear();

    std::vector<uint64_t> stash_vector(stash_set[sub_region_index].begin(), stash_set[sub_region_index].end());
#pragma omp parallel for shared(m_block_storage)
    for (auto stash_iterator = stash_vector.begin(); stash_iterator != stash_vector.end(); ++stash_iterator) {
        void* block_address = (void*)*stash_iterator;
        uint64_t block_index = ((uint64_t)block_address - (uint64_t)m_region_start_address) / m_block_size;
#pragma omp critical
        {
            std::string block_hash = m_block_storage->commit_stash_block(block_index);
            if (block_hash.empty()) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error committing stash block with address: {} - {}", (uint64_t)block_address, strerror(errno));
                exit(-1);
            }
            blocks_ids[block_index] = block_hash;
        }
    }

    stash_set[sub_region_index].clear();
    
    update_metadata(sub_region_index);

    struct stat st_dev_null;
    if (fstat(0, &st_dev_null) != 0) {
        int dev_null_fd = ::open("/dev/null", O_RDWR);
        (void)dev_null_fd;
    }
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: msync({}) - done", sub_region_index);
}

bool uffd_virtual_memory_manager::snapshot(const char* version_metadata_path) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: snapshot() - start");
    
    std::string snapshot_metadata_path = std::string(version_metadata_path) + "/_metadata";
    std::string temp_current_metadata_path = m_version_metadata_path;

    if (utility::directory_exists(version_metadata_path)) {
        if (utility::file_exists(snapshot_metadata_path.c_str())) {
            spdlog::warn("virtual_memory_manager: Version metadata directory already exists");
            return false;
        }
    } else if (!utility::create_directory(version_metadata_path)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Failed to create version metadata directory at {} - {}", version_metadata_path, strerror(errno));
        return false;
    }

    m_version_metadata_path = std::string(version_metadata_path);
    int metadata_fd = ::open(snapshot_metadata_path.c_str(), O_RDWR | O_CREAT, (mode_t)0666);
    int close_status = ::close(metadata_fd);
    (void)close_status;

    msync();
    m_version_metadata_path = temp_current_metadata_path;

    std::string blocks_path_file_name = std::string(version_metadata_path) + "/_blocks_path";
    std::ofstream blocks_path_file;
    blocks_path_file.open(blocks_path_file_name);
    blocks_path_file << m_block_storage->get_blocks_path();
    blocks_path_file.close();

    std::string capacity_path_file_name = std::string(version_metadata_path) + "/_capacity";
    std::ofstream capacity_path_file;
    capacity_path_file.open(capacity_path_file_name);
    capacity_path_file << m_region_max_capacity;
    capacity_path_file.close();
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: snapshot() - done");
    return true;
}

int uffd_virtual_memory_manager::close() {
    msync();

    for (int i = 0; i < num_handling_threads; i++) {
        for (auto it = present_blocks[i].begin(); it != present_blocks[i].end(); ++it) {
            void* address = (void*)*it;
            int status = munmap(address, m_block_size);
            if (status == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error unmapping region with address {} - {}", *it, strerror(errno));
                return -1;
            }
        }
    }

    delete [] blocks_ids;
    delete m_block_storage;
    m_region_start_address = nullptr;
    stop_handler_thread();
    return 0;
}

void* uffd_virtual_memory_manager::handler(uint64_t sub_region_index) {
    while (uffd_active) {
        SPDLOG_LOGGER_INFO(spdlog::default_logger(), "Dequeing from thread {}", (uint64_t)syscall(SYS_gettid));
        utility::fault_event fevent = events_queues[sub_region_index].dequeue();
        if (fevent.address == 0) {
            break;
        }

        uint64_t fault_address = fevent.address;
        uint64_t block_address = get_block_address(fault_address);
        uint64_t block_index = (block_address - (uint64_t)m_region_start_address) / m_block_size;

        bool is_wp_fault = fevent.is_wp_fault;
        bool is_write_fault = fevent.is_write_fault;
        if ((std::find(present_blocks[sub_region_index].begin(), present_blocks[sub_region_index].end(), block_address) != present_blocks[sub_region_index].end()) && !is_wp_fault) {
            continue;
        }

        if (is_wp_fault) {
            if (m_read_only) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: write fault on a read-only region");
                exit(-1);
            }
            clean_lru[sub_region_index].remove((uint64_t)block_address);
            dirty_lru[sub_region_index].push_front((uint64_t)block_address);
            if (stash_set[sub_region_index].find(block_address) != stash_set[sub_region_index].end()) {
                if (!m_block_storage->unstash_block(block_index)) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error unstashing block with index {}", block_index);
                    exit(-1);
                }
                stash_set[sub_region_index].erase(block_address);
            }
            is_valid_uffd(m_uffd);
            struct uffdio_writeprotect uffdio_writeprotect;
            uffdio_writeprotect.range.start = block_address;
            uffdio_writeprotect.range.len = m_block_size;
            uffdio_writeprotect.mode = 0;
            if (ioctl(m_uffd, UFFDIO_WRITEPROTECT, &uffdio_writeprotect) == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: handler() - Error ioctl-UFFDIO_WRITEPROTECT - {}", strerror(errno));
                exit(-1);
            }
            events_queues[sub_region_index].remove_processed(fevent);
            continue;
        }

        if (present_blocks[sub_region_index].find(block_address) == present_blocks[sub_region_index].end()) {
            evict_if_needed(sub_region_index);
            int backing_block_fd = -1;
            std::string backing_block_path = "";
            std::string stash_backing_block_path = m_block_storage->get_block_stash_path(block_index);
            if (!stash_backing_block_path.empty()) {
                backing_block_path = stash_backing_block_path;
            } else if (blocks_ids[block_index].compare(EMPTY_BLOCK_HASH) != 0) {
                backing_block_path = m_block_storage->get_block_full_path(block_index, blocks_ids[block_index]) + "/" + blocks_ids[block_index];
            }

            if (!backing_block_path.empty()) {
                backing_block_fd = open(backing_block_path.c_str(), O_RDONLY);
                if (backing_block_fd == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error opening backing block {} for address {} - {}", backing_block_path, block_address, strerror(errno));
                    exit(-1);
                }
#ifdef USE_COMPRESSION
                size_t compressed_block_size = utility::get_file_size(backing_block_path.c_str());
                void* const read_buffer = mmap(nullptr, compressed_block_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
                if (pread(backing_block_fd, read_buffer, compressed_block_size, 0) == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error reading backing block {} for address {} - {}", backing_block_path, block_address, strerror(errno));
                    exit(-1);
                }
                utility::decompress(read_buffer, (void*)(((uint64_t)temp_buffer) + (sub_region_index * m_block_size)), compressed_block_size);
                int munmap_status = munmap(read_buffer, compressed_block_size);
                if (munmap_status == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error munmapping read buffer decompression {}", strerror(errno));
                }
#else
                if (pread(backing_block_fd, (void*)(((uint64_t)temp_buffer) + (sub_region_index * m_block_size)), m_block_size, 0) == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error closing backing block {} for address {} - {}", backing_block_path, block_address, strerror(errno));
                    exit(-1);
                }
#endif
                if (::close(backing_block_fd) == -1) {
                    exit(-1);
                }

                is_valid_uffd(m_uffd);
                struct uffdio_copy uffdio_copy;
                uffdio_copy.src = (unsigned long)(((uint64_t)temp_buffer) + (sub_region_index * m_block_size));
                uffdio_copy.dst = (unsigned long)block_address;
                uffdio_copy.len = m_block_size;
                uffdio_copy.mode = is_write_fault ? UFFDIO_COPY_MODE_DONTWAKE : (UFFDIO_COPY_MODE_WP | UFFDIO_COPY_MODE_DONTWAKE);
                uffdio_copy.copy = 0;
                if (ioctl(m_uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: handler() - Error ioctl-UFFDIO_COPY - {}", strerror(errno));
                    exit(-1);
                }
            } else {
                if (is_write_fault) {
                    is_valid_uffd(m_uffd);
                    struct uffdio_zeropage uffdio_zeropage;
                    uffdio_zeropage.range.start = block_address;
                    uffdio_zeropage.range.len = m_block_size;
                    uffdio_zeropage.mode = UFFDIO_ZEROPAGE_MODE_DONTWAKE;
                    if (ioctl(m_uffd, UFFDIO_ZEROPAGE, &uffdio_zeropage) == -1) {
                        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error ioctl-UFFDIO_ZEROPAGE for Zero-page write fault - {}", strerror(errno));
                        exit(-1);
                    }
                } else {
                    is_valid_uffd(m_uffd);
                    struct uffdio_copy uffdio_copy;
                    uffdio_copy.src = (unsigned long)zero_page;
                    uffdio_copy.dst = (unsigned long)block_address;
                    uffdio_copy.len = m_block_size;
                    uffdio_copy.mode = UFFDIO_COPY_MODE_WP | UFFDIO_COPY_MODE_DONTWAKE;
                    uffdio_copy.copy = 0;
                    if (ioctl(m_uffd, UFFDIO_COPY, &uffdio_copy) == -1) {
                        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error ioctl-UFFDIO_COPY for zero page - {}", strerror(errno));
                        exit(-1);
                    }
                }
            }

            if (is_write_fault) {
                dirty_lru[sub_region_index].push_front(block_address);
            } else {
                clean_lru[sub_region_index].push_front(block_address);
            }
            present_blocks[sub_region_index].insert(block_address);
            events_queues[sub_region_index].remove_processed(fevent);
            is_valid_uffd(m_uffd);
            struct uffdio_range uffdio_range;
            uffdio_range.start = block_address;
            uffdio_range.len = m_block_size;
            if (ioctl(m_uffd, UFFDIO_WAKE, &uffdio_range) == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error ioctl-UFFDIO_WAKE - {}", strerror(errno));
                exit(-1);
            }
        }
    }
    return NULL;
}

void uffd_virtual_memory_manager::set_uffd(uint64_t uffd) {
    m_uffd = uffd;
}

void* uffd_virtual_memory_manager::handler_helper(void* context) {
    int sub_region_index = ((uffd_virtual_memory_manager*)context)->get_next_sub_region();
    return ((uffd_virtual_memory_manager*)context)->handler(sub_region_index);
}

uint64_t uffd_virtual_memory_manager::get_block_address(uint64_t fault_address) {
    uint64_t start_address = (uint64_t)m_region_start_address;
    uint64_t block_index = (fault_address - start_address) / m_block_size;
    uint64_t block_address = start_address + block_index * m_block_size;
    return block_address;
}

void uffd_virtual_memory_manager::add_page_fault_event(utility::fault_event fevent) {
    uint64_t start_address = (uint64_t)m_region_start_address;
    uint64_t block_index = (fevent.address - start_address) / m_block_size;
    uint64_t sub_region_index = block_index % num_handling_threads;
    if (!events_queues[sub_region_index].found(fevent)) {
        events_queues[sub_region_index].enqueue(fevent);
    }
}

void uffd_virtual_memory_manager::add_page_fault_event_all(utility::fault_event fevent) {
    for (int i = 0; i < num_handling_threads; i++) {
        events_queues[i].enqueue(fevent);
    }
}

void uffd_virtual_memory_manager::start_handler_thread() {
    for (int i = 0; i < num_handling_threads; i++) {
        pthread_t fault_handling_thread;
        int status = pthread_create(&fault_handling_thread, NULL, handler_helper, (void*)this);
        if (status != 0) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: pthread_create - {}", strerror(status));
            exit(-1);
        }
        fault_handling_threads.push_back(fault_handling_thread);
    }
}

void uffd_virtual_memory_manager::stop_handler_thread() {
    uffd_active = false;
    for (int i = 0; i < num_handling_threads; i++) {
        add_page_fault_event({.address = 0, .is_wp_fault = false, .is_write_fault = false});
    }
    for (int i = 0; i < num_handling_threads; i++) {
        int status = pthread_join(fault_handling_threads[i], NULL);
        if (status != 0) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error pthread_join - {}", strerror(status));
            exit(-1);
        }
    }
}

void uffd_virtual_memory_manager::deactivate_uffd_thread() {
    uffd_active = false;
}

int uffd_virtual_memory_manager::get_next_sub_region() {
    return ++next_sub_region;
}

void uffd_virtual_memory_manager::update_metadata(int sub_region_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: update_metadata()");
    
    if (present_blocks[sub_region_index].size() == 0) {
        return;
    }
    size_t max_address = *present_blocks[sub_region_index].rbegin();
    size_t current_size = max_address - (uint64_t)m_region_start_address + m_block_size;
    size_t num_blocks = current_size / m_block_size;
    char* blocks_bytes = new char[num_blocks * HASH_SIZE];
    for (size_t i = 0; i < num_blocks; i++) {
        const char* block_hash_bytes = blocks_ids[i].c_str();
        for (int j = 0; j < HASH_SIZE; j++) {
            blocks_bytes[i * HASH_SIZE + j] = block_hash_bytes[j];
        }
    }

    std::string metadata_path = m_version_metadata_path + "/_metadata";
    int metadata_fd = open(metadata_path.c_str(), O_RDWR);
    if (metadata_fd == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error opening metadata file - {}", strerror(errno));
        exit(-1);
    }
    const auto written = ::pwrite(metadata_fd, (void*)blocks_bytes, num_blocks * HASH_SIZE, 0);
    if (written == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Failed to update metadata and mappings - {}", strerror(errno));
        exit(-1);
    }
    if (::close(metadata_fd) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error closing metadata file after update - {}", strerror(errno));
        exit(-1);
    }
    delete [] blocks_bytes;
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "uffd_virtual_memory_manager: update_metadata() - done");
}

void uffd_virtual_memory_manager::evict_if_needed(int sub_region_index) {
    void* to_evict;
    if ((present_blocks[sub_region_index].size() * m_block_size) >= m_max_mem_size) {
        SPDLOG_LOGGER_INFO(spdlog::default_logger(), "virtual_memory_manager: evict_if_needed() - Evicting");
        if (clean_lru[sub_region_index].size() > 0) {
            to_evict = (void*)clean_lru[sub_region_index].back();
            SPDLOG_LOGGER_INFO(spdlog::default_logger(), "virtual_memory_manager: evict_if_needed() - Evicting clean block: {}", ((uint64_t)to_evict - (uint64_t)m_region_start_address) / m_block_size);
            clean_lru[sub_region_index].pop_back();
        } else {
            to_evict = (void*)dirty_lru[sub_region_index].back();
            dirty_lru[sub_region_index].pop_back();
            uint64_t block_index = ((uint64_t)to_evict - (uint64_t)m_region_start_address) / m_block_size;
            SPDLOG_LOGGER_INFO(spdlog::default_logger(), "virtual_memory_manager: evict_if_needed() - Stashing block: {}", block_index);
            if (!m_block_storage->stash_block(to_evict, block_index)) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error stashing block with index {}", block_index);
                exit(-1);
            }
            stash_set[sub_region_index].insert((uint64_t)to_evict);
        }
        void* evicted_addr = mmap(to_evict, m_block_size, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (evicted_addr == MAP_FAILED) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "virtual_memory_manager: Error evicting block with address - {}", strerror(errno));
            exit(-1);
        }
        present_blocks[sub_region_index].erase((uint64_t)to_evict);
    }
}

bool uffd_virtual_memory_manager::is_valid_uffd(int uffd) {
    return true;
}

#endif // USERFAULTFD
