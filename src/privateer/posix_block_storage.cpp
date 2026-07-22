#include <privateer/posix_block_storage.hpp>
#include <sys/mman.h>
#include <fstream>

// Constructors
posix_block_storage::posix_block_storage(std::string base_directory) {
    open(base_directory, "");
}

posix_block_storage::posix_block_storage(std::string base_directory, std::string stash_directory) {
    open(base_directory, stash_directory);
}

posix_block_storage::posix_block_storage(std::string base_directory, size_t block_granularity) {
    create(base_directory, "", block_granularity);
}

posix_block_storage::posix_block_storage(std::string base_directory, std::string stash_directory, 
                                        size_t block_granularity_arg) {
    create(base_directory, stash_directory, block_granularity_arg);
}

posix_block_storage::posix_block_storage(const posix_block_storage& other) {
    base_directory = other.base_directory;
    block_granularity = other.block_granularity;
    stash_directory = other.stash_directory;
    stash_block_ids = other.stash_block_ids;
}

// Public interface methods
std::string posix_block_storage::store_block(void* buffer, bool write_to_file, uint64_t block_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: store_block()");
    std::string block_hash = "";
    bool on_stash = is_multi_tiered();
    if (on_stash) {
        std::string block_hash = store_block(buffer, write_to_file, block_index, true, "");
        if (block_hash.empty()) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Error storing block with index: {}", block_index);
            return block_hash;
        }
        on_stash = false;
    }
    std::string block_hash_final = store_block(buffer, write_to_file, block_index, on_stash, block_hash);
    if (!block_hash_final.empty()) {
        stash_committed_block_ids.insert(std::pair<uint64_t,std::string>(block_index, block_hash));
    }
    return block_hash_final;
}

bool posix_block_storage::stash_block(void* block_start, uint64_t block_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: stash_block()");
    std::string block_UUID = "";
    std::string block_temp_path = "";
    bool file_exists = false;
    int block_fd = -1;
    
    if (stash_block_ids.find(block_index) != stash_block_ids.end()) {
        SPDLOG_LOGGER_INFO(spdlog::default_logger(), "file exists while stashing");
        block_UUID = stash_block_ids[block_index];
        file_exists = true;
    } else {
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        block_UUID = boost::lexical_cast<std::string>(uuid);
        SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
            "block index - {} block UUID - {}", block_index, block_UUID.c_str());
        stash_block_ids.insert(std::pair<uint64_t,std::string>(block_index, block_UUID));
    }
    
    block_temp_path = stash_directory + "/" + block_UUID;
    int open_flags = file_exists ? O_RDWR : O_CREAT | O_RDWR;
    block_fd = ::open(block_temp_path.c_str(), open_flags, S_IRUSR | S_IWUSR);
    if (block_fd == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error opening stash file descriptor - {}", strerror(errno));
        return false;
    }
    
    if (!file_exists) {
        int trunc_status = ftruncate(block_fd, block_granularity);
        if (trunc_status == -1) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Error sizing file - {}", strerror(errno));
            return false;
        }
    }
    
    if (pwrite(block_fd, block_start, block_granularity, 0) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error writing block to stash file - {}", strerror(errno));
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "block_start: {}", block_start);
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "block_granularity: {}", block_granularity);
        return false;
    }
    
    if (close(block_fd) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error closing stash file: {}", strerror(errno));
        return false;
    }
    return true;
}

bool posix_block_storage::unstash_block(uint64_t block_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: unstash_block()");
    if (stash_block_ids.find(block_index) == stash_block_ids.end()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error unstashing block with index = {}, No backing stash block", block_index);
        return false;
    }
    std::string block_UUID = stash_block_ids[block_index];
    std::string block_temp_path = stash_directory + "/" + block_UUID;
    
    if (remove(block_temp_path.c_str()) == -1) {
        return false;
    }
    stash_block_ids.erase(block_index);
    return true;
}

std::string posix_block_storage::commit_stash_block(uint64_t block_index) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: commit_stash_block()");
    if (stash_block_ids.find(block_index) == stash_block_ids.end()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error - block with index {} has no backing stash file", block_index);
        exit(-1);
    }
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_index - {}", std::to_string(block_index));
    std::string block_stash_path = stash_directory + "/" + stash_block_ids[block_index];
    int block_fd = ::open(block_stash_path.c_str(), O_RDONLY);
    if (block_fd == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error opening stash file - {}", strerror(errno));
        exit(-1);
    }
    
    void* temp_buffer = mmap(nullptr, block_granularity, PROT_READ | PROT_WRITE, 
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (temp_buffer == MAP_FAILED) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error mmapping temp buffer for stash block - {}", strerror(errno));
        exit(-1);
    }

    if (pread(block_fd, temp_buffer, block_granularity, 0) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(),"Error reading stash file {}", strerror(errno));
        exit(-1);
    }

    if (close(block_fd) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error closing file - {}", strerror(errno));
        exit(-1);
    }

    std::string block_hash = utility::compute_hash((char*) temp_buffer, block_granularity);
    
    std::string subdirectory_name;
    bool is_stash = is_multi_tiered();
    subdirectory_name = get_blocks_subdirectory(block_hash, is_stash);
    
    std::string final_filename = subdirectory_name + "/" + block_hash;
    std::string stash_filename = stash_directory + "/" + stash_block_ids[block_index];

    if (!utility::file_exists(final_filename.c_str())) {
        #ifndef USE_COMPRESSION
        int rename_status = rename(stash_filename.c_str(), final_filename.c_str());
        if (rename_status != 0) {
            if (utility::file_exists(final_filename.c_str())) {
                int remove_status = remove(stash_filename.c_str());
                if (remove_status != 0) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                        "block_storage: Error removing stash file");
                    return "";
                }
                stash_block_ids.erase(block_index);
                if (is_multi_tiered()) {
                    block_hash = store_block(temp_buffer, true, block_index, false, block_hash);
                    if (block_hash.empty()) {
                        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                            "block_storage: Error committing stash block with index {} to base path", block_index);
                        return "";
                    }
                }

                int munmap_status = munmap(temp_buffer, block_granularity);
                if (munmap_status == -1) {
                    SPDLOG_ERROR("Error unmapping temporary buffer while unstashing");
                    exit(-1);
                }
                return block_hash;
            } else {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error renaming file {}", strerror(errno));
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "Stash file name = {}", stash_filename);
                return "";
            }
        } else {
        #endif
            block_hash = store_block(temp_buffer, true, block_index, false, block_hash);
            if (block_hash.empty()) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error committing stash block with index {} to base path", block_index);
                return "";
            }
            stash_block_ids.erase(block_index);

            #ifdef USE_COMPRESSION
            int remove_status = remove(block_stash_path.c_str());
            if (remove_status != 0) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error removing stash file");
                return "";
            }
            #endif

            int munmap_status = munmap(temp_buffer, block_granularity);
            if (munmap_status == -1) {
                SPDLOG_ERROR("Error unmapping temporary buffer while unstashing");
                exit(-1);
            }
            return block_hash;
        #ifndef USE_COMPRESSION
        }
        #endif
    } else {
        int remove_status = remove(stash_filename.c_str());
        if (remove_status != 0) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Error removing stash file");
            return "";
        }
        stash_block_ids.erase(block_index);
        int munmap_status = munmap(temp_buffer, block_granularity);
        if (munmap_status == -1) {
            SPDLOG_ERROR("Error unmapping temporary buffer while unstashing");
            exit(-1);
        }
        return block_hash;
    }
}

std::string posix_block_storage::get_block_full_path(uint64_t block_index, std::string block_hash) {
    std::string base_subdir = get_blocks_subdirectory(block_hash, false);
    if (is_multi_tiered()) {
        std::string stash_subdir = get_blocks_subdirectory(block_hash, true);
        if (stash_committed_block_ids.find(block_index) == stash_committed_block_ids.end()) {
            std::string stash_block_path = stash_subdir + "/" + block_hash;
            std::string base_block_path = base_subdir + "/" + block_hash;
            if (!copy_to_stash(base_block_path, stash_block_path)) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error copying block with index {} and ID {} From base directory to stash directory", 
                    block_index, block_hash);
                exit(-1);
            }
            stash_committed_block_ids.insert(std::pair<uint64_t,std::string>(block_index, block_hash));
        }
        return stash_subdir;
    } else {
        return base_subdir;
    }
}

size_t posix_block_storage::get_block_granularity() {
    return block_granularity;
}

std::string posix_block_storage::get_block_stash_path(size_t block_index) {
    std::string block_stash_path = "";
    if (stash_block_ids.find(block_index) != stash_block_ids.end()) {
        block_stash_path = stash_directory + "/" + stash_block_ids[block_index];
    }
    return block_stash_path;
}

std::string posix_block_storage::get_blocks_path() {
    return base_directory;
}

bool posix_block_storage::copy_to_stash(std::string base_block, std::string stash_block) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: copy_to_stash()");
    if (!utility::file_exists(stash_block.c_str())) {
        return utility::copy_file(base_block.c_str(), stash_block.c_str(), false);
    }
    return true;
}

// Private methods
void posix_block_storage::create(std::string base_directory_path, std::string stash_directory_path, 
                                 size_t block_granularity_arg) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: create()");
    
    stash_directory = stash_directory_path;
    if (!stash_directory.empty()) {
        if (!utility::directory_exists(stash_directory_path.c_str())) {
            if (!utility::create_directory(stash_directory_path.c_str())) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Failed to create stash directory");
                exit(-1);
            }
        }
    }
    
    base_directory = base_directory_path;
    block_granularity = block_granularity_arg;

    if (!utility::directory_exists(base_directory_path.c_str())) {
        if (!utility::create_directory(base_directory.c_str())) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Failed to create blocks directory");
            exit(-1);
        }
        std::string granularity_file_name = base_directory + "/_granularity";
        std::ofstream granularity_file;
        granularity_file.open(granularity_file_name);
        granularity_file << block_granularity;
        granularity_file.close();
    }
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: create() done");
}

void posix_block_storage::open(std::string base_directory_path, std::string stash_directory_path) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: open()");
    
    stash_directory = stash_directory_path;
    if (!stash_directory.empty()) {
        if (!utility::directory_exists(stash_directory_path.c_str())) {
            if (!utility::create_directory(stash_directory_path.c_str())) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Failed to create stash directory");
                exit(-1);
            }
        }
    }
    
    if (!utility::directory_exists(base_directory_path.c_str())) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Blocks directory does not exist");
        exit(-1);
    }
    
    base_directory = base_directory_path;

    std::string granularity_string;
    std::string granularity_file_name = base_directory + "/_granularity";
    std::ifstream granularity_file;
    granularity_file.open(granularity_file_name);
    if (!granularity_file.is_open()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error opening block granularity metadata");
        exit(-1);
    }
    if (!std::getline(granularity_file, granularity_string)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error reading block granularity metadata");
        exit(-1);
    }
    block_granularity = std::stol(granularity_string);
}

std::string posix_block_storage::get_blocks_subdirectory(std::string block_hash, bool on_stash) {
    std::string base_path = on_stash ? stash_directory : base_directory;
    std::string block_hash_prefix = "0x" + block_hash.substr(0, hash_prefix_length);
    size_t block_prefix_index = std::stoul(block_hash_prefix, nullptr, 16);
    size_t subdir_index = block_prefix_index % num_subdirs;
    std::string subdir_name = base_path + "/" + std::to_string(subdir_index);
    if (!utility::directory_exists(subdir_name.c_str())) {
        if (!utility::create_directory(subdir_name.c_str())) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Failed to create blocks subdirectory");
            exit(-1);
        }
    }
    return subdir_name;
}

std::string posix_block_storage::store_block(void* buffer, bool write_to_file, uint64_t block_index, 
                                            bool on_stash, std::string pre_computed_hash) {
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), "block_storage: store_block()");

    std::string block_hash = pre_computed_hash;
    if (block_hash.empty()) {
        block_hash = utility::compute_hash((char*) buffer, block_granularity);
    }

    std::string subdirectory_name = get_blocks_subdirectory(block_hash, on_stash);

    std::string temporary_file_name_template = std::to_string(block_index) + "_temp_XXXXXX";
    char* name_template = (char*) temporary_file_name_template.c_str();
    std::pair<int, std::string> temp_file_fd_name = create_temporary_unique_block(
        subdirectory_name, name_template, block_index, on_stash);
    int block_fd = temp_file_fd_name.first;

    std::string final_filename = subdirectory_name + "/" + block_hash;
    std::string temporary_filename = temp_file_fd_name.second;

    if (!utility::file_exists(final_filename.c_str())) {
        if (write_to_file) {
            #ifdef USE_COMPRESSION
            std::pair<void*,size_t> compressed_buffer_and_size = utility::compress(buffer, block_granularity);
            void* const write_buffer = compressed_buffer_and_size.first;
            size_t compressed_block_size = compressed_buffer_and_size.second;
            int trunc_status = ftruncate(block_fd, compressed_block_size);
            if (trunc_status == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error sizing temporary file to compressed size");
                exit(-1);
            }
            size_t written = pwrite(block_fd, write_buffer, compressed_block_size, 0);
            if (written == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error writing to file - {}", strerror(errno));
                return "";
            }
            int munmap_status = munmap(write_buffer, compressed_block_size);
            if (munmap_status == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error unmapping compressed buffer - {}", strerror(errno));
                exit(-1);
            }
            #else
            size_t written = pwrite(block_fd, buffer, block_granularity, 0);
            if (written == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error writing to file - {}", strerror(errno));
                return "";
            }
            #endif
        }
        
        int rename_status = rename(temporary_filename.c_str(), final_filename.c_str());
        if (rename_status != 0) {
            if (utility::file_exists(final_filename.c_str())) {
                int remove_status = remove(temporary_filename.c_str());
                if (remove_status != 0) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                        "block_storage: Error removing temporary file");
                    return "";
                }
                if (::close(block_fd) == -1) {
                    SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                        "block_storage: Error closing file descriptor for block: {} - {}", 
                        block_index, strerror(errno));
                    exit(-1);
                }
                return block_hash;
            } else {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error renaming file {}", strerror(errno));
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "Temporary file name = {}", temporary_filename);
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), "Final file name = {}", final_filename);
            }
            if (::close(block_fd) == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error closing file descriptor for block: {} - {}", 
                    block_index, strerror(errno));
                exit(-1);
            }
            return "";
        }
    } else {
        int remove_status = remove(temporary_filename.c_str());
        if (remove_status != 0) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "block_storage: Error removing temporary file");
            if (::close(block_fd) == -1) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "block_storage: Error closing file descriptor for block: {} - {}", 
                    block_index, strerror(errno));
                exit(-1);
            }
            return "";
        }
    }
    
    if (::close(block_fd) == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error closing file descriptor for block: {} - {}", 
            block_index, strerror(errno));
        exit(-1);
    }
    return block_hash;
}

bool posix_block_storage::is_multi_tiered() {
    size_t base_suffix_start = base_directory.find_last_of("/");
    size_t stash_suffix_start = stash_directory.find_last_of("/");
    std::string base_prefix = base_directory.substr(0, base_suffix_start);
    std::string stash_prefix = stash_directory.substr(0, stash_suffix_start);
    return (base_prefix.compare(stash_prefix) != 0);
}

std::pair<int,std::string> posix_block_storage::create_temporary_unique_block(
    std::string prefix, char* name_template, uint64_t block_index, bool on_stash) {
    
    std::string temporary_file_name = prefix + "/" + std::string(name_template);
    char* temporary_file_name_template = new char[temporary_file_name.length() + 1];
    temporary_file_name_template = (char*) memcpy((void*) temporary_file_name_template, 
                                                  (void*) temporary_file_name.c_str(), 
                                                  temporary_file_name.length() + 1);
    
    int fd = mkstemp(temporary_file_name_template);
    if (fd == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error creating temporary file {}", strerror(errno));
        exit(-1);
    }
    
    #ifndef USE_COMPRESSION
    int trunc_status = ftruncate(fd, block_granularity);
    if (trunc_status == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error sizing temporary file");
        exit(-1);
    }
    #endif
    
    std::pair<int, std::string> fd_name(fd, std::string(temporary_file_name_template));
    delete [] temporary_file_name_template;
    return fd_name;
}
