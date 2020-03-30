/*
 * Copyright (c) 2020, NVIDIA CORPORATION.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <tests/utilities/cudf_gtest.hpp>
#include "cxxopts.hpp"
#include <cudf/wrappers/bool.hpp>
#include <cudf/utilities/traits.hpp>

#include <rmm/mr/device/default_memory_resource.hpp>
#include <rmm/mr/device/device_memory_resource.hpp>

#include <ftw.h>
#include <random>

namespace cudf {
namespace test {

/**---------------------------------------------------------------------------*
 * @brief Base test fixture class from which all libcudf tests should inherit.
 *
 * Example:
 * ```
 * class MyTestFixture : public cudf::test::BaseFixture {};
 * ```
 *---------------------------------------------------------------------------**/
class BaseFixture : public ::testing::Test {
  rmm::mr::device_memory_resource* _mr{rmm::mr::get_default_resource()};

 public:
  /**---------------------------------------------------------------------------*
   * @brief Returns pointer to `device_memory_resource` that should be used for
   * all tests inheritng from this fixture
   *---------------------------------------------------------------------------**/
  rmm::mr::device_memory_resource* mr() { return _mr; }
};

template <typename T, typename Enable = void>
struct uniform_distribution_impl{};
template<typename T>
struct uniform_distribution_impl<T,
  std::enable_if_t<std::is_integral<T>::value && not cudf::is_boolean<T>()> > {
   using type = std::uniform_int_distribution<T>;
};

template<typename T>
struct uniform_distribution_impl<T, std::enable_if_t<std::is_floating_point<T>::value > > {
   using type = std::uniform_real_distribution<T>;
};

template<typename T>
struct uniform_distribution_impl<T, std::enable_if_t<cudf::is_boolean<T>() > > {
   using type = std::bernoulli_distribution;
};

template<typename T>
struct uniform_distribution_impl<T, std::enable_if_t<cudf::is_timestamp<T>() > > {
   using type = std::uniform_int_distribution<typename T::duration::rep>;
};

template <typename T>
using uniform_distribution_t = typename uniform_distribution_impl<T>::type;

/**---------------------------------------------------------------------------*
 * @brief Provides uniform random number generation.
 *
 * It is often useful in testing to have a convenient source of random numbers.
 * This class is intended to serve as a base class for test fixtures to provide
 * random number generation. `UniformRandomGenerator::generate()` will generate
 * the next random number in the sequence.
 *
 * Example:
 * ```c++
 * UniformRandomGenerator g(0,100);
 * g.generate(); // Returns a random number in the range [0,100]
 * ```
 *
 * @tparam T The type of values that will be generated.
 *---------------------------------------------------------------------------**/
template <typename T = cudf::size_type,
          typename Engine = std::default_random_engine>
class UniformRandomGenerator {
 public:
  using uniform_distribution = uniform_distribution_t<T>;

  UniformRandomGenerator() = default;

  /**---------------------------------------------------------------------------*
   * @brief Construct a new Uniform Random Generator to generate uniformly
   * random numbers in the range `[upper,lower]`
   *
   * @param lower Lower bound of the range
   * @param upper Upper bound of the desired range
   *---------------------------------------------------------------------------**/
  UniformRandomGenerator(T lower, T upper) : dist{lower, upper} {}

  /**---------------------------------------------------------------------------*
   * @brief Returns the next random number.
   *---------------------------------------------------------------------------**/
  T generate() { return T{dist(rng)}; }

 private:
  uniform_distribution dist{};         ///< Distribution
  Engine rng{std::random_device{}()};  ///< Random generator
};

/**---------------------------------------------------------------------------*
 * @brief Provides temporary directory for temporary test files.
 *
 * Example:
 * ```c++
 * ::testing::Environment* const temp_env =
 *    ::testing::AddGlobalTestEnvironment(new TempDirTestEnvironment);
 * ```
 *---------------------------------------------------------------------------**/
class TempDirTestEnvironment : public ::testing::Environment {
 public:
  std::string tmpdir;

  void SetUp() {
    char tmp_format[] = "/tmp/gtest.XXXXXX";
    tmpdir = mkdtemp(tmp_format);
    tmpdir += "/";
  }

  void TearDown() {
    // TODO: should use std::filesystem instead, once C++17 support added
    nftw(tmpdir.c_str(), rm_files, 10, FTW_DEPTH | FTW_MOUNT | FTW_PHYS);
  }

  static int rm_files(const char *pathname, const struct stat *sbuf, int type,
                      struct FTW *ftwb) {
    return remove(pathname);
  }

  /**
   * @brief Get directory path to use for temporary files
   *
   * @return std::string The temporary directory path
   */
  std::string get_temp_dir() { return tmpdir; }

  /**
   * @brief Get a temporary filepath to use for the specified filename
   *
   * @return std::string The temporary filepath
   */
  std::string get_temp_filepath(std::string filename) {
    return tmpdir + filename;
  }
};


/**---------------------------------------------------------------------------*
 * @brief Test environment that initializes the default rmm memory resource.
 * 
 * Required for tests programs that use rmm. It is recommended to include
 * `CUDF_TEST_PROGRAM_MAIN()` in a code file instead of directly instantiating 
 * an object of this type.
 *---------------------------------------------------------------------------**/
class RmmTestEnvironment : public ::testing::Environment {
/**---------------------------------------------------------------------------*
 * @brief String representing which RMM allocation mode is to be used.
 *
 * Valid values are 'cuda', 'pool' and 'managed'. 
 *---------------------------------------------------------------------------**/
  std::string mode;
public:
  /**
   * @brief Constructor where the only argument is a string `mode`.
   *
   * Mode repesents the rmm allocation mode, @ref mode.
   */
  RmmTestEnvironment(std::string const& mode):mode(mode){}

  /**
   * @brief Initializes the default RMM memory resource.
   *
   * Sets different allocation mode based on the value passed to the constructor.
   * 
   * @throws cudf::logic_error if mode value is invalid.
   * 
   * @return void
   */
  void SetUp() {
    rmmOptions_t opts{};
    if (mode == "cuda")
      opts.allocation_mode = CudaDefaultAllocation;
    else if (mode == "pool")
      opts.allocation_mode = PoolAllocation;
    else if (mode == "managed")
      opts.allocation_mode = CudaManagedMemory;
    else 
      CUDF_FAIL("Invalid RMM allocation mode");

    ASSERT_EQ(rmmInitialize(&opts), RMM_SUCCESS);
  }

  /**
   * @brief Shuts down the rmm memory manager.
   * 
   * @throws cudf::logic_error if rmmFinalize is unsuccessful 
   * 
   * @return void
   */
  void TearDown() {
    ASSERT_EQ(rmmFinalize(), RMM_SUCCESS);
  }
};

}  // namespace test
}  // namespace cudf

/**
 * @brief Parses the cuDF test command line options.
 *
 * Currently only supports 'rmm_mode' string paramater, which set the rmm
 * allocation mode. The default value of the parameter is 'pool'.
 * 
 * @return Parsing results in the form of unordered map
 */
inline auto parse_cudf_test_opts(int argc, char **argv) {
  try {
    cxxopts::Options options(argv[0], " - cuDF tests command line options");
    options
      .allow_unrecognised_options()
      .add_options()
      ("rmm_mode", "RMM allocation mode",
        cxxopts::value<std::string>()->default_value("pool"));

    return options.parse(argc, argv);
  }
  catch (const cxxopts::OptionException& e) {
    CUDF_FAIL("Error parsing command line options");
  }
}

/**
 * @brief Macro that defines main function for gtest programs that use rmm
 * 
 * Should be included in every test program that uses rmm allocators.The `main`
 * function is a wrapper around the google test generated `main`, mantaining
 * the original functionality. In addition, the custom `main` function parses
 * the command line to customize test behavior, like allocation mode.
 *
 */
#define CUDF_TEST_PROGRAM_MAIN() int main(int argc, char **argv) {  \
  ::testing::InitGoogleTest(&argc, argv);                           \
  auto const cmd_opts = parse_cudf_test_opts(argc, argv);           \
  auto const rmm_mode = cmd_opts["rmm_mode"].as<std::string>();     \
  auto const rmm_env = ::testing::AddGlobalTestEnvironment(         \
    new cudf::test::RmmTestEnvironment(rmm_mode));                  \
  return RUN_ALL_TESTS();                                           \
}
