//==============================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
// =============================================================
#include <sycl/ext/intel/ac_types/ac_int.hpp>
// oneAPI headers
#include <sycl/ext/intel/fpga_extensions.hpp>
#include <sycl/ext/intel/experimental/fpga_kernel_properties.hpp>
#include <sycl/sycl.hpp>

#include "lib_rtl.hpp"

#include "exception_handler.hpp"

// Forward declare the kernel name in the global scope.
// This FPGA best practice reduces name mangling in the optimization report.
class KernelComputeRTL;

using MyInt27 = ac_int<27, false>;
using MyInt54 = ac_int<54, false>;


// Using host pipes to stream data in and out of kernal
// IDPipeA and IDPipeB will be written at host, and then read data in kernel (device)
// IDPipeC will be written at kernel (device) respectively, and then read data by host
class IDPipeA;
using InputPipeA = sycl::ext::intel::experimental::pipe<IDPipeA, unsigned>;
class IDPipeB;
using InputPipeB = sycl::ext::intel::experimental::pipe<IDPipeB, unsigned>;
class IDPipeC;
using OutputPipeC = sycl::ext::intel::experimental::pipe<IDPipeC, unsigned long>;

// This kernel compute multiplier result by call RTL function RtlDSPm27x27u
template <typename PipeIn1, typename PipeIn2, typename PipeOut>
struct RtlMult27x27 {

  auto get(sycl::ext::oneapi::experimental::properties_tag) {
    return sycl::ext::oneapi::experimental::properties{
        sycl::ext::intel::experimental::streaming_interface_accept_downstream_stall, 
        sycl::ext::intel::experimental::pipelined<1>};
  }
  
  void operator()() const {
    unsigned a_val = PipeIn1::read();
    unsigned b_val = PipeIn2::read();
    MyInt27 a = a_val;
    MyInt27 b = b_val;
    MyInt54 res = RtlDSPm27x27u(a, b);
    PipeOut::write(res);
  }
};

// This kernel compute result by performing the basic multipler soft logic
int main() {
  unsigned long result_rtl = 0;
  unsigned kA = 134217727;
  unsigned kB = 100;

  // Select the FPGA emulator (CPU), FPGA simulator, or FPGA device
#if FPGA_SIMULATOR
  auto selector = sycl::ext::intel::fpga_simulator_selector_v;
#elif FPGA_HARDWARE
  auto selector = sycl::ext::intel::fpga_selector_v;
#else  // #if FPGA_EMULATOR
  auto selector = sycl::ext::intel::fpga_emulator_selector_v;
#endif

  try {
    sycl::queue q(selector, fpga_tools::exception_handler);

    auto device = q.get_device();

    std::cout << "Running on device: "
              << device.get_info<sycl::info::device::name>().c_str()
              << std::endl;
    {
      //write data to host-to-device hostpipes
      InputPipeA::write(q, kA);
      InputPipeB::write(q, kB);
      // launch a kernel to that uses a multiplier defined in RTL
      q.single_task<KernelComputeRTL>(RtlMult27x27<InputPipeA,InputPipeB,OutputPipeC>{}).wait();
      //read data from device-to-host hostpipe
      result_rtl = OutputPipeC::read(q);
    }

  } catch (sycl::exception const &e) {
    // Catches exceptions in the host code
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

  // Compute the expected "golden" result
  unsigned long gold = (unsigned long) kA * kB;
  
  // Check the results
  if (result_rtl != gold) {
    std::cout << "FAILED: result (" << result_rtl << ") is incorrect! Expected " << gold << "\n";
    return -1;
  }
  std::cout << "PASSED: result is correct!\n";
  return 0;
}

