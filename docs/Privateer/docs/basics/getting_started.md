# Getting Started

Privateer consists of header files that are included under privateer/include/privateer/.


## Requirements

* GCC 8.3 or higher with openmp enabled.

* Boost (Tested with 1.77.0).

* Spdlog (Tested with 1.9.2)

## Building Privateer

* To build an application that uses Privateer, add the Privateer headers path to the include path using "-I" compiler option or CPLUS_INCLUDE_PATH.

* To build Privateer as a shared library:
```bash
git clone git@github.com:LLNL/Privateer.git
cd Privateer
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=. ..
```
