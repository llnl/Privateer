#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <mpi.h>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/fmt/fmt.h>
#include <spdlog/formatter.h>
#include <spdlog/details/log_msg.h>
#include "spdlog/pattern_formatter.h"

#include "../include/privateer/privateer.hpp"
#include "../examples/utility/random.hpp"

std::vector<size_t> get_random_offsets(size_t region_length, size_t num_updates){
  std::vector<size_t> random_values;
  std::generate_n(std::back_inserter(random_values), num_updates, utility::RandomNumberBetween(0,region_length - 1));
  return random_values;
}

std::vector<size_t> get_random_offsets(size_t region_start, size_t region_end, size_t num_updates){
  std::vector<size_t> random_values;
  std::generate_n(std::back_inserter(random_values), num_updates, utility::RandomNumberBetween(region_start,region_end - 1));
  return random_values;
}

template <typename Func>
void run_parallel_for(size_t count, size_t num_threads, Func&& func) {
  if (count == 0) {
    return;
  }

  size_t thread_count = std::max<size_t>(1, num_threads);
  thread_count = std::min(thread_count, count);

  std::vector<std::thread> workers;
  workers.reserve(thread_count);

  size_t chunk_size = (count + thread_count - 1) / thread_count;
  for (size_t thread_index = 0; thread_index < thread_count; ++thread_index) {
    size_t begin = thread_index * chunk_size;
    if (begin >= count) {
      break;
    }
    size_t end = std::min(count, begin + chunk_size);
    workers.emplace_back([begin, end, &func]() {
      for (size_t index = begin; index < end; ++index) {
        func(index);
      }
    });
  }

  for (auto& worker : workers) {
    worker.join();
  }
}

template <typename Iterator, typename Func>
void run_parallel_for_each(Iterator begin, Iterator end, size_t num_threads, Func&& func) {
  using value_type = typename std::iterator_traits<Iterator>::value_type;
  std::vector<value_type> items(begin, end);
  run_parallel_for(items.size(), num_threads, [&](size_t index) {
    func(items[index]);
  });
}

class my_formatter_flag : public spdlog::custom_flag_formatter {
public:
    void format(const spdlog::details::log_msg &, const std::tm &, spdlog::memory_buf_t &dest) override
    {
      std::string some_txt = std::to_string(count);
      count++;
      dest.append(some_txt.data(), some_txt.data() + some_txt.size());
    }

    std::unique_ptr<custom_flag_formatter> clone() const override
    {
        return spdlog::details::make_unique<my_formatter_flag>();
    }

    static size_t count;
};

size_t my_formatter_flag::count = 0;

class PrivateerTest : public testing::TestWithParam<std::tuple<size_t, size_t, size_t, size_t, size_t, size_t, size_t>> {
  public:
    Privateer* priv = nullptr;
    size_t size_bytes;
    size_t num_ints;
    size_t* data;
    char* datastore = "/tmp/datastore";

    void SetUp() override {
      std::filesystem::remove_all(this->datastore);
      spdlog::set_level(spdlog::level::trace);

      // File sink with json pattern
      auto formatter = std::make_unique<spdlog::pattern_formatter>();
      formatter->add_flag<my_formatter_flag>('*').set_pattern("{\"id\":\"%*\",\"name\":\"%^%v%$\",\t\"cat\":\"CPP_APP\",\"pid\":\"%P\",\"tid\":\"%t\",\"ts\":\"%S%F\",\"dur\":\"%u\",\"ph\":\"X\",\"args\":{}}");
      auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/log.txt", true);
      file_sink->set_formatter(std::move(formatter));

      // Console sink with default pattern
      auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

      // Create a logger with both sinks
      spdlog::sinks_init_list sink_list = {file_sink, console_sink};
      auto logger = std::make_shared<spdlog::logger>("multi_sink", sink_list);

      // Set the logger as the default logger
      spdlog::set_default_logger(logger);

      char env[] = "PRIVATEER_MAX_MEM_BLOCKS=";
      char block_num[10];
      strcpy(block_num, (std::to_string(std::get<0>(GetParam()))).c_str());
      strcat(env, block_num);
      putenv(env);

      // uffd env var
      putenv("PRIVATEER_BLOCK_SIZE=2097152");

      // location of datastore directory, default is /tmp/datastore
      if (std::getenv("PRIVATEER_TEST_DIR") != NULL) {
        datastore = std::getenv("PRIVATEER_TEST_DIR");
      }

      priv = new Privateer(Privateer::CREATE, this->datastore);
      size_bytes = std::get<1>(GetParam());
      num_ints = size_bytes / sizeof(size_t);
      data = (size_t*) priv->create(nullptr, "v0", size_bytes, true);
    }

    void TearDown() override {
      delete priv;
      //std::filesystem::remove_all(this->datastore);
    }
};

// TO BE REMOVED
TEST_P(PrivateerTest, Immutable) {
  size_t start = 0;
  size_t middle = this->num_ints / 2;
  size_t middle_to_end = ( this->num_ints / 2 ) + ( this->num_ints / 4 );
  size_t end = this->num_ints - 1;
  this->data[start] = 7;
  this->data[middle] = 8;
  this->data[middle_to_end] = 9;
  this->data[end] = 10;
  std::cout << "written to: " << end << std::endl;
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_immutable(nullptr, "v0", "v1");
  this->data[start] = 1;
  this->data[middle] = 2;
  this->data[middle_to_end] = 3;
  this->data[end] = 4;
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle], 8);
  EXPECT_EQ(this->data[middle_to_end], 9);
  EXPECT_EQ(this->data[end], 10);
  this->data = (size_t*) priv->open_read_only(nullptr, "v1");
  EXPECT_EQ(this->data[start], 1);
  EXPECT_EQ(this->data[middle], 2);
  EXPECT_EQ(this->data[middle_to_end], 3);
  EXPECT_EQ(this->data[end], 4);
}

TEST_P(PrivateerTest, SimpleWrite) {
  size_t start = 0;
  size_t middle = this->num_ints / 2;
  size_t middle_to_end = ( this->num_ints / 2 ) + ( this->num_ints / 4 );
  size_t end = this->num_ints - 1;
  this->data[start] = 7;
  this->data[middle] = 8;
  this->data[middle_to_end] = 9;
  this->data[end] = 10;
  std::cout << "written to: " << end << std::endl;
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle], 8);
  EXPECT_EQ(this->data[middle_to_end], 9);
  EXPECT_EQ(this->data[end], 10);
}

TEST_P(PrivateerTest, SimpleDenseWrite) {
  for (size_t i = 0; i < this->num_ints; i++) {
    SPDLOG_TRACE("PrivateerTest: int - {}, val - {}", i, this->data[i]);
    this->data[i] = i;
  }
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  for (size_t i = 0; i < this->num_ints; i++) {
    SPDLOG_TRACE("PrivateerTest: int - {}, val - {} EXPECT_EQ", i, this->data[i]);
    EXPECT_EQ(this->data[i], i);
  }
}

TEST_P(PrivateerTest, SimpleWrite_Data) {
  size_t start = 0;
  size_t middle = this->num_ints / 2;
  size_t middle_to_end = ( this->num_ints / 2 ) + ( this->num_ints / 4 );
  size_t end = this->num_ints - 1;
  this->data[start] = 7;
  this->data[middle] = 8;
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle], 8);
  EXPECT_EQ(this->data[middle_to_end], 0);
  EXPECT_EQ(this->data[end], 0);
  EXPECT_EQ(((size_t*) this->priv->data())[start], 7);
  EXPECT_EQ(((size_t*) this->priv->data())[middle], 8);
  EXPECT_EQ(((size_t*) this->priv->data())[middle_to_end], 0);
  EXPECT_EQ(((size_t*) this->priv->data())[end], 0);
  this->data[middle_to_end] = 9;
  this->data[end] = 10;
  std::cout << "written to: " << end << std::endl;
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle], 8);
  EXPECT_EQ(this->data[middle_to_end], 9);
  EXPECT_EQ(this->data[end], 10);
  EXPECT_EQ(((size_t*) this->priv->data())[start], 7);
  EXPECT_EQ(((size_t*) this->priv->data())[middle], 8);
  EXPECT_EQ(((size_t*) this->priv->data())[middle_to_end], 9);
  EXPECT_EQ(((size_t*) this->priv->data())[end], 10);
}

TEST_P(PrivateerTest, SortWrite) {
  for (size_t i = 0; i < this->num_ints; i++) {
    this->data[i] = (this->num_ints - 1) - i;
  }
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open  (nullptr, "v0");
  for (size_t i = 0; i < this->num_ints; i++) {
    EXPECT_EQ(this->data[i], (this->num_ints - 1) - i);
  }
  std::sort(this->data, this->data + this->num_ints);
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  for (size_t i = 0; i < this->num_ints; i++) {
    EXPECT_EQ(this->data[i], i);
  }
}

TEST_P(PrivateerTest, MultipleWrite) {
  for (size_t i = 0; i < this->num_ints; i++) {
    //SPDLOG_TRACE("PrivateerTest: int - {}, val - {}", i, this->data[i]);
    this->data[i] = i;
  }

  priv->msync();

  int num_iterations = std::get<2>(GetParam());
  for (size_t k = 0; k < num_iterations; k++) {
    delete priv;
    SPDLOG_TRACE("PrivateerTest: iteration - {}", k);
    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open(nullptr, "v0");
    spdlog::info("PrivateerTest: opened datastore");
    for (size_t i = 0; i < this->num_ints; i++) {
      //SPDLOG_TRACE("PrivateerTest: int - {}, val - {} EXPECT_EQ", i, this->data[i]);
      EXPECT_EQ(this->data[i], i);
    }
      //SPDLOG_TRACE("PrivateerTest: EXPECT_EQ done");
    for (size_t i = 0; i < this->num_ints; i++) {
      //SPDLOG_TRACE("PrivateerTest: int - {}, val - {}", i, this->data[i]);
      this->data[i] = (this->num_ints - 1) - i;
    }
    priv->msync();
    delete priv;

    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open(nullptr, "v0");
    for (size_t i = 0; i < this->num_ints; i++) {
      //SPDLOG_TRACE("PrivateerTest: int - {}, val - {} EXPECT_EQ", i, this->data[i]);
      EXPECT_EQ(this->data[i], (this->num_ints - 1) - i);
    }
    for (size_t i = 0; i < this->num_ints; i++) {
      this->data[i] = i;
      //SPDLOG_TRACE("PrivateerTest: int - {}, val - {}", i, this->data[i]);
    }
    priv->msync();
  }
}

TEST_P(PrivateerTest, MultipleWrite_Threaded) {
  size_t num_threads = std::get<4>(GetParam());
  run_parallel_for(this->num_ints, num_threads, [&](size_t i) {
    this->data[i] = i;
  });

  priv->msync();

  int num_iterations = std::get<2>(GetParam());
  for (size_t k = 0; k < num_iterations; k++) {
    delete priv;
    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open(nullptr, "v0");
    run_parallel_for(this->num_ints, num_threads, [&](size_t i) {
      EXPECT_EQ(this->data[i], i);
    });
    run_parallel_for(this->num_ints, num_threads, [&](size_t i) {
      this->data[i] = (this->num_ints - 1) - i;
    });
    priv->msync();
    delete priv;

    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open  (nullptr, "v0");
    run_parallel_for(this->num_ints, num_threads, [&](size_t i) {
      EXPECT_EQ(this->data[i], (this->num_ints - 1) - i);
    });
    run_parallel_for(this->num_ints, num_threads, [&](size_t i) {
      this->data[i] = i;
    });
    priv->msync();
  }
}

TEST_P(PrivateerTest, MultipleWriteSparse) {
  size_t num_threads = std::get<4>(GetParam());
  //size_t int_iter = std::get<2>(GetParam());
  size_t int_iter = this->num_ints;
  for (size_t i = 0; i < int_iter; i++) {
    this->data[i] = i / num_threads + (i % num_threads) * (this->num_ints / num_threads);
    //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
  }
  priv->msync();

  int num_iterations = std::get<2>(GetParam());
  for (size_t k = 0; k < num_iterations; k++) {
      spdlog::info("Iteration: {}", k);
    delete priv;
    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open(nullptr, "v0");
/*
#pragma omp parallel for
    for (size_t i = 0; i < int_iter; i++) {
      EXPECT_EQ(this->data[i], i / num_threads + (i % num_threads) * (this->num_ints / num_threads));
    }
    //*/
    for (size_t i = 0; i < int_iter; i++) {
      this->data[i] = ((this->num_ints - 1) - i) / num_threads + (((this->num_ints - 1) - i) % num_threads) * (this->num_ints / num_threads);
      //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
    }
    priv->msync();
    delete priv;

    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open  (nullptr, "v0");
/*
#pragma omp parallel for
    for (size_t i = 0; i < int_iter; i++) {
      EXPECT_EQ(this->data[i], ((this->num_ints - 1) - i) / num_threads + (((this->num_ints - 1) - i) % num_threads) * (this->num_ints / num_threads));
    }
    //*/
    for (size_t i = 0; i < int_iter; i++) {
      this->data[i] = i / num_threads + (i % num_threads) * (this->num_ints / num_threads);;
      //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
    }
    priv->msync();
  }
}

TEST_P(PrivateerTest, MultipleWriteSparse_Threaded) {
  size_t num_threads = std::get<4>(GetParam());
  //size_t int_iter = std::get<2>(GetParam());
  size_t int_iter = this->num_ints;
  run_parallel_for(int_iter, num_threads, [&](size_t i) {
    this->data[i] = i / num_threads + (i % num_threads) * (this->num_ints / num_threads);
    //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
  });
  priv->msync();

  int num_iterations = std::get<2>(GetParam());
  for (size_t k = 0; k < num_iterations; k++) {
      spdlog::info("Iteration: {}", k);
    delete priv;
    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open(nullptr, "v0");
/*
#pragma omp parallel for
    for (size_t i = 0; i < int_iter; i++) {
      EXPECT_EQ(this->data[i], i / num_threads + (i % num_threads) * (this->num_ints / num_threads));
    }
    //*/
    run_parallel_for(int_iter, num_threads, [&](size_t i) {
      this->data[i] = ((this->num_ints - 1) - i) / num_threads + (((this->num_ints - 1) - i) % num_threads) * (this->num_ints / num_threads);
      //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
    });
    priv->msync();
    delete priv;

    priv = new Privateer(Privateer::OPEN, this->datastore);
    this->data = (size_t*) priv->open  (nullptr, "v0");
/*
#pragma omp parallel for
    for (size_t i = 0; i < int_iter; i++) {
      EXPECT_EQ(this->data[i], ((this->num_ints - 1) - i) / num_threads + (((this->num_ints - 1) - i) % num_threads) * (this->num_ints / num_threads));
    }
    //*/
    run_parallel_for(int_iter, num_threads, [&](size_t i) {
      this->data[i] = i / num_threads + (i % num_threads) * (this->num_ints / num_threads);;
      //spdlog::info("Wrote to block address: {}", this->data[i] * sizeof(size_t));
    });
    priv->msync();
  }
}

TEST_P(PrivateerTest, IncrementalRandomSparseWrite) {
  int num_iterations = std::get<2>(GetParam());
  int num_updates = std::get<3>(GetParam());

  for (int i = 0 ; i < num_iterations ; i++) {
    std::vector<size_t> offsets = get_random_offsets(this->num_ints, num_updates);
    for (auto offset : offsets) {
    EXPECT_GE(offset, 0);
    EXPECT_LT(offset, this->num_ints);
    SPDLOG_TRACE("PrivateerTest: offset - {}", offset);
      data[offset] += 1;
    }
    priv->msync();
  }
}

TEST_P(PrivateerTest, IncrementalRandomSparseWrite_Threaded) {
  int num_iterations = std::get<2>(GetParam());
  int num_updates = std::get<3>(GetParam());
  size_t num_threads = std::get<4>(GetParam());
  for (int i = 0 ; i < num_iterations ; i++) {
    std::vector<size_t> offsets = get_random_offsets(this->num_ints, num_updates);
    std::sort(offsets.begin(), offsets.end());
    offsets.erase(std::unique(offsets.begin(), offsets.end()), offsets.end());
    run_parallel_for_each(offsets.begin(), offsets.end(), num_threads, [&](size_t offset) {
      std::cout << "offset iterator: " << offset << std::endl;
      EXPECT_GE(offset, 0);
      EXPECT_LT(offset, this->num_ints);
      data[offset] += 1;
    });
    priv->msync();
  }
}

TEST_P(PrivateerTest, SingleSnapshot) {
  size_t start = 0;
  size_t middle_to_start = this->num_ints / 4;
  size_t middle = this->num_ints / 2;
  size_t end = this->num_ints - 1;
  this->data[start] = 7;
  this->data[middle_to_start] = 8;
  this->data[middle] = 9;
  this->data[end] = 10;
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle_to_start], 8);
  EXPECT_EQ(this->data[middle], 9);
  EXPECT_EQ(this->data[end], 10);
  std::cout << "written to: " << end << std::endl;
  priv->snapshot(("v1"));
  this->data[start] = 8;
  this->data[middle_to_start] = 9;
  this->data[middle] = 10;
  this->data[end] = 11;
  EXPECT_EQ(this->data[start], 8);
  EXPECT_EQ(this->data[middle_to_start], 9);
  EXPECT_EQ(this->data[middle], 10);
  EXPECT_EQ(this->data[end], 11);
  std::cout << "written to: " << end << std::endl;
  priv->snapshot(("v2"));
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  EXPECT_EQ(this->data[start], 8);
  EXPECT_EQ(this->data[middle_to_start], 9);
  EXPECT_EQ(this->data[middle], 10);
  EXPECT_EQ(this->data[end], 11);
  this->data = (size_t*) priv->open_read_only(nullptr, "v1");
  EXPECT_EQ(this->data[start], 7);
  EXPECT_EQ(this->data[middle_to_start], 8);
  EXPECT_EQ(this->data[middle], 9);
  EXPECT_EQ(this->data[end], 10);
  this->data = (size_t*) priv->open_read_only(nullptr, "v2");
  EXPECT_EQ(this->data[start], 8);
  EXPECT_EQ(this->data[middle_to_start], 9);
  EXPECT_EQ(this->data[middle], 10);
  EXPECT_EQ(this->data[end], 11);
}

TEST_P(PrivateerTest, SimpleSnapshot) {
  int num_iterations = std::get<2>(GetParam());
  for (size_t i = 0; i < this->num_ints; i++) {
    this->data[i] = 0;
  }
  priv->msync();
  for (int j = 1; j <= num_iterations; ++j){
    spdlog::info("iteration: {}", j);
    for (size_t k = 1; k < this->num_ints; k+=2) {
      this->data[k]++;
      EXPECT_EQ(data[k], j);
      EXPECT_EQ(data[k-1], 0);
    }
    priv->snapshot(("v" + std::to_string(j)).c_str());
  }
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  //*
  for (int j = 1; j <= num_iterations; ++j){
    this->data = (size_t*) priv->open_read_only(nullptr, ("v" + std::to_string(j)).c_str());
    for (size_t k = 1; k < this->num_ints; k+=2){
      EXPECT_EQ(data[k], j);
      EXPECT_EQ(data[k-1], 0);
    }
  }
  //*/
}

TEST_P(PrivateerTest, IncrementalRandomSparseSnapshot) {
  int num_iterations = std::get<2>(GetParam());
  size_t update_ratio = std::get<3>(GetParam());

  float initial_fill_ratio = 0.01;
  size_t initial_fill_size = this->num_ints * initial_fill_ratio;
  float initial_sparsity = 0.01;
  size_t num_updates = initial_fill_size*initial_sparsity;

  std::vector<size_t> random_indices_first_half = get_random_offsets(0, initial_fill_size, num_updates);
  for (auto offset_iterator : random_indices_first_half) {
    EXPECT_GE(offset_iterator, 0);
    EXPECT_LT(offset_iterator, this->num_ints);
    this->data[offset_iterator] += 1;
  }
  this->priv->msync();

  size_t update_size = num_ints*(update_ratio * 1.0 / 100);

  for (int i = 1; i < num_iterations; i++) {
  std::cout << "iteration: " << i << std::endl;
    size_t update_start = initial_fill_size + i*update_size;
    num_updates = update_size*initial_sparsity;

    std::vector<size_t> random_indices = get_random_offsets(update_start, update_start + update_size, num_updates);
    for (auto offset_iterator : random_indices) {
      SPDLOG_TRACE("PrivateerTest: val - {}", this->data[offset_iterator]);
      //EXPECT_GE(offset_iterator, 0);
      //EXPECT_LT(offset_iterator, this->num_ints);
      this->data[offset_iterator] += 1;
    }

    EXPECT_TRUE(priv->snapshot(("v" + std::to_string(i)).c_str()));
  }
}

TEST_P(PrivateerTest, IncrementalRandomSparseSnapshot_Threaded) {
  int num_iterations = std::get<2>(GetParam());
  size_t update_ratio = std::get<3>(GetParam());
  size_t num_threads = std::get<4>(GetParam());

  float initial_fill_ratio = 0.01;
  size_t initial_fill_size = this->num_ints * initial_fill_ratio;
  float initial_sparsity = 0.1;
  size_t num_updates = initial_fill_size*initial_sparsity;

  std::vector<size_t> random_indices_first_half = get_random_offsets(0, initial_fill_size, num_updates);
  std::sort(random_indices_first_half.begin(), random_indices_first_half.end());
  random_indices_first_half.erase(
    std::unique(random_indices_first_half.begin(), random_indices_first_half.end()),
    random_indices_first_half.end());
  run_parallel_for_each(random_indices_first_half.begin(), random_indices_first_half.end(), num_threads, [&](size_t offset) {
    EXPECT_GE(offset, 0);
    EXPECT_LT(offset, this->num_ints);
    spdlog::info("Faulted on block address: {}", offset);
    this->data[offset] += 1;
  });
  this->priv->msync();

  size_t update_size = num_ints*(update_ratio * 1.0 / 100);

  for (int i = 1; i < num_iterations; i++) {
    size_t update_start = initial_fill_size + i*update_size;
    num_updates = update_size*initial_sparsity;
    std::cout << "iteration: " << i << std::endl;
    std::vector<size_t> random_indices = get_random_offsets(update_start, update_start + update_size, num_updates);
    std::sort(random_indices.begin(), random_indices.end());
    random_indices.erase(std::unique(random_indices.begin(), random_indices.end()), random_indices.end());
    run_parallel_for_each(random_indices.begin(), random_indices.end(), num_threads, [&](size_t offset) {
      EXPECT_GE(offset, 0);
      EXPECT_LT(offset, this->num_ints);
      spdlog::info("Faulted on block address: {}", offset);
      this->data[offset] += 1;
    });

    EXPECT_TRUE(priv->snapshot(("v" + std::to_string(i)).c_str()));
  }
}

TEST_P(PrivateerTest, IncrementalRandomSparseSnapshot_Skewed_Threaded) {
  int num_iterations = std::get<2>(GetParam());
  size_t update_ratio = std::get<3>(GetParam());
  size_t num_threads = std::get<4>(GetParam());
  int dense_region_size_ratio = std::get<5>(GetParam());
  int dense_region_update_ratio = std::get<6>(GetParam());

  size_t total_updates_per_iteration = num_ints * (update_ratio*1.0/100);
  size_t dense_region_length = num_ints * (dense_region_size_ratio*1.0/100);
  size_t num_updates_dense_region = total_updates_per_iteration * (dense_region_update_ratio*1.0/100);
  size_t sparse_region_start = dense_region_length;
  size_t num_updates_sparse_region = total_updates_per_iteration - num_updates_dense_region;

  run_parallel_for(num_ints / 2, num_threads, [&](size_t i) {
    data[i] = 0;
  });
  this->priv->msync();

  size_t update_size = num_ints*(update_ratio * 1.0 / 100);

  for (int i = 1; i < num_iterations; i++) {
    // update dense region
    std::vector<size_t> random_indices = get_random_offsets(0, dense_region_length, num_updates_dense_region);
    std::sort(random_indices.begin(), random_indices.end());
    random_indices.erase(std::unique(random_indices.begin(), random_indices.end()), random_indices.end());
    run_parallel_for_each(random_indices.begin(), random_indices.end(), num_threads, [&](size_t offset) {
      data[offset] += 1;
    });

    // update sparse region
    std::vector<size_t> random_indices_sparse_region = get_random_offsets(sparse_region_start, num_ints, num_updates_sparse_region);
    std::sort(random_indices_sparse_region.begin(), random_indices_sparse_region.end());
    random_indices_sparse_region.erase(
      std::unique(random_indices_sparse_region.begin(), random_indices_sparse_region.end()),
      random_indices_sparse_region.end());
    run_parallel_for_each(random_indices_sparse_region.begin(), random_indices_sparse_region.end(), num_threads, [&](size_t offset) {
      data[offset] += 1;
    });

    // snapshot
    EXPECT_TRUE(priv->snapshot(("v" + std::to_string(i)).c_str()));
  }
}

TEST(PrivateerTest_Concurrent, ConcurrentWrite) {
  char* datastore = "/tmp/datastore";
  if (std::getenv("PRIVATEER_TEST_DIR") != NULL) {
    datastore = std::getenv("PRIVATEER_TEST_DIR");
  }
  size_t size_bytes = 1024LLU;
  size_t num_ints = size_bytes / sizeof(size_t);
  MPI_Init(NULL, NULL);
  int world_size;
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  int world_rank;
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  if (world_rank == 0){
      Privateer privateer(Privateer::CREATE, datastore);
      privateer.create(nullptr, "v0", size_bytes, true);
      privateer.msync();
  }
  MPI_Barrier(MPI_COMM_WORLD);

  if (world_rank != 0){
      Privateer privateer(Privateer::OPEN, datastore);
      size_t *data = (size_t*) privateer.open_immutable(nullptr, "v0", ("v" + std::to_string(world_rank)).c_str());
      for (size_t i = 0; i < num_ints; i++){
        data[i] = i;
      }
      privateer.msync();
  }
  MPI_Barrier(MPI_COMM_WORLD);

  if (world_rank == 0) {
    for (int i = 1; i < world_size; i++){
      Privateer privateer(Privateer::OPEN, datastore);
      size_t *data = (size_t*) privateer.open_read_only(nullptr, ("v" + std::to_string(i)).c_str());
      for (size_t j = 0; j < num_ints; j++){
        EXPECT_EQ(data[j], j);
      }
    }
  }
  MPI_Finalize();
  std::filesystem::remove_all(datastore);
}

/* Out of range cases are undefined!
// Death tests
TEST_P(PrivateerTest, LowerBoundOutOfRange) {
  EXPECT_DEATH({
      this->data[-1] = 1;
    }, "Fault address out of range");
}

TEST_P(PrivateerTest, UpperBoundOutOfRange) {
  EXPECT_DEATH({
      this->data[this->num_ints] = 1;
    }, "Fault address out of range");
}

TEST_P(PrivateerTest, LowerBoundOutOfRangeAfterInitialFault) {
  this->data[0] = 1;
  EXPECT_DEATH({
      this->data[-1] = 1;
    }, "Fault address out of range");
}

TEST_P(PrivateerTest, UpperBoundOutOfRangeAfterInitialFault) {
  this->data[this->num_ints - 1] = 1;
  EXPECT_DEATH({
      this->data[this->num_ints] = 1;
    }, "Fault address out of range");
}
//*/

TEST_P(PrivateerTest, ReadOnly) {
  this->data[0] = 7;
  spdlog::info("Read op");
  EXPECT_EQ(this->data[0], 7);
  priv->msync();
  delete priv;

  priv = new Privateer(Privateer::OPEN, this->datastore);
  this->data = (size_t*) priv->open_read_only(nullptr, "v0");
  spdlog::info("Read op");
  EXPECT_EQ(this->data[0], 7);
  spdlog::info("Write op");
  EXPECT_DEATH({
      this->data[0] = 1;
    }, "");
}

#ifdef USE_COMPRESSION
TEST_P(PrivateerTest, SimpleCompressionTest) {
  for (size_t i = 0; i < this->num_ints; i++) {
    this->data[i] = 7;
  }
  priv->msync();
  size_t size = 0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(std::string(this->datastore) + std::string("/blocks"))) {
    if (std::filesystem::is_regular_file(entry.path())) {
      size += std::filesystem::file_size(entry.path());
    }
  }
  EXPECT_LT(size, 2097152);
}
#endif

/*
** PARAMS
** 0 - max number of 2MB blocks, constraining max allotted memory region
** 1 - size of datastore region
** 2 - iterations
** 3 - update rate/ratio
** 4 - number of threads
** 5 - dense region size ratio
** 6 - dense update size ratio
*/

INSTANTIATE_TEST_SUITE_P(
    Parameterized_PrivateerTest,
    PrivateerTest,
    ::testing::Values(
      std::make_tuple(    1,               8 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(    2,               8 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(    4,               8 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(    8,               8 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(   16,               8 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(16384,               8 * 1024LLU,  5, 10, 6, 10, 10),
#ifdef ENABLE_PAGE_EVICTION
      std::make_tuple(    1,        8 * 1024 * 1024LLU,  3, 10, 2, 10, 10), // page eviction occurs
      std::make_tuple(    2,        8 * 1024 * 1024LLU,  3, 10, 2, 10, 10), // page eviction occurs
      std::make_tuple(    1,        8 * 1024 * 1024LLU,  3, 10, 4, 10, 10), // page eviction occurs
      std::make_tuple(    2,        8 * 1024 * 1024LLU,  3, 10, 4, 10, 10), // page eviction occurs
      std::make_tuple(    1,        8 * 1024 * 1024LLU,  3, 10, 6, 10, 10), // page eviction occurs
      std::make_tuple(    2,        8 * 1024 * 1024LLU,  3, 10, 6, 10, 10), // page eviction occurs
      std::make_tuple(    1,        8 * 1024 * 1024LLU,  3, 10, 8, 10, 10), // page eviction occurs
      std::make_tuple(    2,        8 * 1024 * 1024LLU,  3, 10, 8, 10, 10), // page eviction occurs
      std::make_tuple(    1,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10), // page eviction occurs
      std::make_tuple(    2,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10), // page eviction occurs
#endif
      std::make_tuple(    4,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(    8,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(   16,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10),
      std::make_tuple(16384,        8 * 1024 * 1024LLU,  5, 10, 6, 10, 10)/*,
      std::make_tuple(    1, 8 * 1024 * 1024 * 1024LLU,  2, 10, 6, 10, 10), // page eviction occurs
      std::make_tuple(    2, 8 * 1024 * 1024 * 1024LLU, 10, 10), // page eviction occurs
      std::make_tuple(    4, 8 * 1024 * 1024 * 1024LLU, 10, 10), // page eviction occurs
      std::make_tuple(    8, 8 * 1024 * 1024 * 1024LLU, 10, 10), // page eviction occurs
      std::make_tuple(   16, 8 * 1024 * 1024 * 1024LLU, 10, 10), // page eviction occurs
      std::make_tuple(16384, 8 * 1024 * 1024 * 1024LLU, 10, 10)*/
    )
);
