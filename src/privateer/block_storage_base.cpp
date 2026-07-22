#include <privateer/block_storage_base.hpp>
#include <privateer/utility/file_util.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <cstdlib>

size_t block_storage_base::get_version_block_granularity(std::string blocks_path) {
    std::string granularity_string;
    std::string granularity_file_name = blocks_path + "/_granularity";
    std::ifstream granularity_file;
    granularity_file.open(granularity_file_name);
    if (!granularity_file.is_open()) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error opening block granularity metadata");
        exit(-1);
    }
    if (!std::getline(granularity_file, granularity_string)) {
        SPDLOG_LOGGER_ERROR(spdlog::default_logger(), 
            "block_storage: Error opening block granularity metadata");
        exit(-1);
    }
    return std::stol(granularity_string);
}
