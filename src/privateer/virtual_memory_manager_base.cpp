#include <privateer/virtual_memory_manager_base.hpp>
#include <privateer/block_storage_base.hpp>
#include <privateer/utility/file_util.hpp>
#include <fstream>
#include <iostream>

const std::string virtual_memory_manager_base::EMPTY_BLOCK_HASH = 
    "0000000000000000000000000000000000000000000000000000000000000000";

size_t virtual_memory_manager_base::version_capacity(std::string version_path) {
    // Read size path
    std::string size_string;
    std::string size_file_name = std::string(version_path) + "/_capacity";
    std::ifstream size_file;
    size_file.open(size_file_name);
    if (!size_file.is_open()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Error opening capacity file");
        exit(-1);
    }
    if (!std::getline(size_file, size_string)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Error reading capacity file");
        exit(-1);
    }
    size_file.close();
    try {
        return std::stol(size_string);
    }
    catch (const std::invalid_argument& ia) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Invalid capacity value");
        exit(-1);
    }
}

size_t virtual_memory_manager_base::version_block_size(std::string version_path) {
    std::string blocks_path_file_name = std::string(version_path) + "/_blocks_path";
    std::ifstream blocks_path_file;
    std::string blocks_dir_path;
    
    blocks_path_file.open(blocks_path_file_name);
    if (!blocks_path_file.is_open()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Error opening blocks path file");
        exit(-1);
    }
    if (!std::getline(blocks_path_file, blocks_dir_path)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Error reading blocks path file");
        exit(-1);
    }
    return block_storage_base::get_version_block_granularity(blocks_dir_path);
}

void virtual_memory_manager_base::create_version_metadata(
    const char* version_metadata_dir_path, 
    const char* block_storage_dir_path, 
    size_t version_capacity, 
    bool allow_overwrite) {
    
    SPDLOG_LOGGER_INFO(spdlog::default_logger(), 
        "virtual_memory_manager: create_version_metadata()");
    
    std::string metadata_file_name = std::string(version_metadata_dir_path) + "/_metadata";
    std::string blocks_path_file_name = std::string(version_metadata_dir_path) + "/_blocks_path";
    std::string capacity_file_name = std::string(version_metadata_dir_path) + "/_capacity";

    // Create version directory
    if (utility::directory_exists(version_metadata_dir_path)) {
        if (!allow_overwrite) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "virtual_memory_manager: Version metadata directory already exists");
            exit(-1);
        }
        // Remove existing metadata file
        if (utility::file_exists(metadata_file_name.c_str())) {
            if (remove(metadata_file_name.c_str()) != 0) {
                SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                    "virtual_memory_manager: Error removing existing metadata file");
                exit(-1);
            }
        }
    } else {
        if (!utility::create_directory(version_metadata_dir_path)) {
            SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
                "virtual_memory_manager: Error creating version metadata directory");
            exit(-1);
        }
    }
    
    // Create blocks metadata file
    metadata_fd = ::open(metadata_file_name.c_str(), O_RDWR | O_CREAT | O_EXCL, (mode_t) 0666);
    if (metadata_fd == -1) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "virtual_memory_manager: Error creating metadata file");
        exit(-1);
    }
    
    // Create file to save blocks path
    std::ofstream blocks_path_file;
    blocks_path_file.open(blocks_path_file_name);
    blocks_path_file << block_storage_dir_path;
    blocks_path_file.close();

    // Create capacity file
    std::ofstream capacity_file;
    capacity_file.open(capacity_file_name);
    capacity_file << version_capacity;
    capacity_file.close();
}
