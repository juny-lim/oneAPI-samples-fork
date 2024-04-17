#include <iostream>

// oneAPI headers
#include <sycl/ext/intel/experimental/task_sequence.hpp>
#include <sycl/ext/intel/fpga_extensions.hpp>
#include <sycl/sycl.hpp>

#include "exception_handler.hpp"

// Forward declare the kernel name in the global scope. This is an FPGA best
// practice that reduces name mangling in the optimization reports.
class IDVectorOp;

using D3Vector = std::array<float, 3>;

// Minimum capacity of a pipe.
// Set to 0 to allow the compiler to save area if possible.
constexpr size_t kPipeMinCapacity = 0;

// Pipes
class IDInputPipeA;
class IDOutputPipeZ;

using InputPipeA = sycl::ext::intel::experimental::pipe<IDInputPipeA, D3Vector,
                                                        kPipeMinCapacity>;
using OutputPipeZ =
    sycl::ext::intel::experimental::pipe<IDOutputPipeZ, float, 5>;

// The square-root of a dot-product is an expensive operation.
float OpSqrt(D3Vector val, const D3Vector coef) {
  float res = sqrt(val[0] * coef[0] + val[1] * coef[1] + val[2] * coef[2]);
  return res;
}

struct VectorOp {
  void operator()() const {
    constexpr D3Vector coef1 = {0.2, 0.3, 0.4};
    constexpr D3Vector coef2 = {0.6, 0.7, 0.8};

    D3Vector new_item;

    // Object declarations of a parameterized task_sequence class must be local,
    // which means global declarations and dynamic allocations are not allowed.
    // Declare the task sequence object outside the for loop so that the
    // hardware can be shared at the return point.
    sycl::ext::intel::experimental::task_sequence<OpSqrt> task_a;

    // put `async()` and `get()` calls in separate loops so that the `OpSqrt()`
    // can be pipelined.
    for (int i = 0; i < new_item.size(); i++) {
      D3Vector item = InputPipeA::read();
      task_a.async(item, coef1);
    }

    for (int i = 0; i < new_item.size(); i++) {
      new_item[i] = task_a.get();
    }

    task_a.async(new_item, coef2);
    OutputPipeZ::write(task_a.get());
  }
};

int main() {
  constexpr int N = 5;
  bool passed = false;

  try {
    // Use compile-time macros to select either:
    //  - the FPGA emulator device (CPU emulation of the FPGA)
    //  - the FPGA device (a real FPGA)
    //  - the simulator device
#if FPGA_SIMULATOR
    auto selector = sycl::ext::intel::fpga_simulator_selector_v;
#elif FPGA_HARDWARE
    auto selector = sycl::ext::intel::fpga_selector_v;
#else  // #if FPGA_EMULATOR
    auto selector = sycl::ext::intel::fpga_emulator_selector_v;
#endif

    sycl::queue q(selector, fpga_tools::exception_handler,
                  sycl::property::queue::enable_profiling{});

    auto device = q.get_device();

    std::cout << "Running on device: "
              << device.get_info<sycl::info::device::name>().c_str()
              << std::endl;

    // initialize input D3Vector
    constexpr float test_vecs[D3Vector{}.size()][D3Vector{}.size()] = {
        {.49, .26, .82}, {.78, .43, .92}, {.17, .72, .34}};

    // input data
    for (int j = 0; j < N; j++) {
      for (int i = 0; i < D3Vector{}.size(); i++) {
        D3Vector data;
        for (int k = 0; k < D3Vector{}.size(); k++) {
          data[k] = test_vecs[i][k];
        }
        InputPipeA::write(q, data);
      }
    }

    std::cout << "Processing vector of size " << D3Vector{}.size() << std::endl;

    float result[N];
    sycl::event e;
    for (int i = 0; i < N; i++) {
      e = q.single_task<VectorOpID>(VectorOp{});
    }

    // verify that result is correct
    passed = true;
    for (int i = 0; i < N; i++) {
      result[i] = OutputPipeZ::read(q);
    }
    for (int i = 1; i < N; i++) {
      if (result[i] != result[i - 1]) {
        std::cout << "idx=" << i << ", async result " << result[i]
                  << ", previously " << result[i - 1] << std::endl;
        passed = false;
      }
    }
    // Wait for kernel to exit
    e.wait();
    std::cout << (passed ? "PASSED" : "FAILED") << std::endl;
  } catch (sycl::exception const &e) {
    // Catches exceptions in the host code.
    std::cerr << "Caught a SYCL host exception:\n" << e.what() << "\n";

    // Most likely the runtime couldn't find FPGA hardware!
    if (e.code().value() == CL_DEVICE_NOT_FOUND) {
      std::cerr << "If you are targeting an FPGA, please ensure that your "
                   "system has a correctly configured FPGA board.\n";
      std::cerr << "Run sys_check in the oneAPI root directory to verify.\n";
      std::cerr << "If you are targeting the FPGA emulator, compile with "
                   "-DFPGA_EMULATOR.\n";
    }
    std::terminate();
  }

  return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}