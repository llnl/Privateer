#include <iostream>
#include <omp.h>
#include <privateer/privateer.hpp>

int main(int argc, char **argv) {

 
  if (argc != 3) {
    std::cerr << "Usage: " << argv[0]
              << " <privateer_base_dir> <size in bytes (int)>" << std::endl;
    return -1;
  }

    char* privateer_base_dir = argv[1];
    size_t size_bytes = size_t(atol(argv[2]));

  {
    
    Privateer priv(Privateer::CREATE, privateer_base_dir);
    size_t* data = (size_t*) priv.create(nullptr, "v0", size_bytes, false);

    #pragma omp parallel for
    for (size_t i = 0; i < (size_bytes) / sizeof(size_t); i++) {
        data[i] = i;
    }
    
    priv.msync();
  }

  {
    Privateer priv(Privateer::OPEN, privateer_base_dir);
    size_t* data = (size_t*) priv.open(nullptr, "v0");

    #pragma omp parallel for
    for (size_t i = 0; i < (size_bytes) / sizeof(size_t); i++) {
        if (data[i] != i) {
            std::cerr << "Data mismatch at index " << i << ": expected " << i << ", got " << data[i] << std::endl;
            exit(-1);
        }
    }

    std::cout << "Data verification successful!" << std::endl;
  }

  return 0;
}