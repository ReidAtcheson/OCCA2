#if OCCA_PTHREADS_ENABLED
#  ifndef OCCA_PTHREADS_HEADER
#  define OCCA_PTHREADS_HEADER

#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <dlfcn.h>
#include <fcntl.h>

#include <pthread.h>
#include <queue>

#include "occaBase.hpp"
#include "occaLibrary.hpp"

#include "occaKernelDefines.hpp"

namespace occa {
  //---[ Data Structs ]---------------
  struct PthreadKernelArg_t;
  typedef void (*PthreadLaunchHandle_t)(PthreadKernelArg_t &args);

  // [-] Hard-coded for now
  struct PthreadsDeviceData_t {
    int coreCount;

    int pThreadCount;
    int schedule;

    pthread_t tid[OCCA_MAX_ARGS];

    int pendingJobs;

    std::queue<PthreadLaunchHandle_t> kernelLaunch[OCCA_MAX_ARGS];
    std::queue<PthreadKernelArg_t*> kernelArgs[OCCA_MAX_ARGS];

    pthread_mutex_t pendingJobsMutex, kernelMutex;
  };

  struct PthreadsKernelData_t {
    void *dlHandle, *handle;
    int pThreadCount;

    int *pendingJobs;

    std::queue<PthreadLaunchHandle_t> *kernelLaunch[50];
    std::queue<PthreadKernelArg_t*> *kernelArgs[50];

    pthread_mutex_t *pendingJobsMutex, *kernelMutex;
  };

  struct PthreadWorkerData_t {
    int rank, count;
    int pinnedCore;

    int *pendingJobs;

    std::queue<PthreadLaunchHandle_t> *kernelLaunch;
    std::queue<PthreadKernelArg_t*> *kernelArgs;

    pthread_mutex_t *pendingJobsMutex, *kernelMutex;
  };

  // [-] Hard-coded for now
  struct PthreadKernelArg_t {
    int rank, count;

    void *kernelHandle;

    int dims;
    occa::dim inner, outer;

    occa::kernelArg args[50];
  };

  static const int compact = (1 << 10);
  static const int scatter = (1 << 11);
  static const int manual  = (1 << 12);
  //==================================


  //---[ Kernel ]---------------------
  template <>
  kernel_t<Pthreads>::kernel_t();

  template <>
  kernel_t<Pthreads>::kernel_t(const kernel_t &k);

  template <>
  kernel_t<Pthreads>& kernel_t<Pthreads>::operator = (const kernel_t<Pthreads> &k);

  template <>
  kernel_t<Pthreads>::kernel_t(const kernel_t<Pthreads> &k);

  template <>
  std::string kernel_t<Pthreads>::getCachedBinaryName(const std::string &filename,
                                                      kernelInfo &info_);

  template <>
  kernel_t<Pthreads>* kernel_t<Pthreads>::buildFromSource(const std::string &filename,
                                                          const std::string &functionName_,
                                                          const kernelInfo &info_);

  template <>
  kernel_t<Pthreads>* kernel_t<Pthreads>::buildFromBinary(const std::string &filename,
                                                          const std::string &functionName_);

  template <>
  kernel_t<Pthreads>* kernel_t<Pthreads>::loadFromLibrary(const char *cache,
                                                          const std::string &functionName_);

  template <>
  int kernel_t<Pthreads>::preferredDimSize();

  template <>
  double kernel_t<Pthreads>::timeTaken();

  template <>
  void kernel_t<Pthreads>::free();
  //==================================


  //---[ Memory ]---------------------
  template <>
  memory_t<Pthreads>::memory_t();

  template <>
  memory_t<Pthreads>::memory_t(const memory_t &m);

  template <>
  memory_t<Pthreads>& memory_t<Pthreads>::operator = (const memory_t &m);

  template <>
  void* memory_t<Pthreads>::getMemoryHandle();

  template <>
  void* memory_t<Pthreads>::getTextureHandle();

  template <>
  void memory_t<Pthreads>::copyFrom(const void *source,
                                    const uintptr_t bytes,
                                    const uintptr_t offset);

  template <>
  void memory_t<Pthreads>::copyFrom(const memory_v *source,
                                    const uintptr_t bytes,
                                    const uintptr_t destOffset,
                                    const uintptr_t srcOffset);

  template <>
  void memory_t<Pthreads>::copyTo(void *dest,
                                  const uintptr_t bytes,
                                  const uintptr_t destOffset);

  template <>
  void memory_t<Pthreads>::copyTo(memory_v *dest,
                                  const uintptr_t bytes,
                                  const uintptr_t srcOffset,
                                  const uintptr_t offset);

  template <>
  void memory_t<Pthreads>::asyncCopyFrom(const void *source,
                                         const uintptr_t bytes,
                                         const uintptr_t destOffset);

  template <>
  void memory_t<Pthreads>::asyncCopyFrom(const memory_v *source,
                                         const uintptr_t bytes,
                                         const uintptr_t srcOffset,
                                         const uintptr_t offset);

  template <>
  void memory_t<Pthreads>::asyncCopyTo(void *dest,
                                       const uintptr_t bytes,
                                       const uintptr_t offset);

  template <>
  void memory_t<Pthreads>::asyncCopyTo(memory_v *dest,
                                       const uintptr_t bytes,
                                       const uintptr_t destOffset,
                                       const uintptr_t srcOffset);

  template <>
  void memory_t<Pthreads>::free();
  //==================================


  //---[ Device ]---------------------
  template <>
  device_t<Pthreads>::device_t();

  template <>
  device_t<Pthreads>::device_t(const device_t<Pthreads> &k);

  template <>
  device_t<Pthreads>& device_t<Pthreads>::operator = (const device_t<Pthreads> &k);

  template <>
  void device_t<Pthreads>::setup(argInfoMap &aim);

  template <>
  deviceIdentifier device_t<Pthreads>::getIdentifier() const;

  template <>
  void device_t<Pthreads>::getEnvironmentVariables();

  template <>
  void device_t<Pthreads>::appendAvailableDevices(std::vector<device> &dList);

  template <>
  void device_t<Pthreads>::setCompiler(const std::string &compiler_);

  template <>
  void device_t<Pthreads>::setCompilerEnvScript(const std::string &compilerEnvScript_);

  template <>
  void device_t<Pthreads>::setCompilerFlags(const std::string &compilerFlags_);

  template <>
  std::string& device_t<Pthreads>::getCompiler();

  template <>
  std::string& device_t<Pthreads>::getCompilerEnvScript();

  template <>
  std::string& device_t<Pthreads>::getCompilerFlags();

  template <>
  void device_t<Pthreads>::flush();

  template <>
  void device_t<Pthreads>::finish();

  template <>
  void device_t<Pthreads>::waitFor(tag tag_);

  template <>
  stream device_t<Pthreads>::createStream();

  template <>
  void device_t<Pthreads>::freeStream(stream s);

  template <>
  stream device_t<Pthreads>::wrapStream(void *handle_);

  template <>
  tag device_t<Pthreads>::tagStream();

  template <>
  double device_t<Pthreads>::timeBetween(const tag &startTag, const tag &endTag);

  template <>
  kernel_v* device_t<Pthreads>::buildKernelFromSource(const std::string &filename,
                                                      const std::string &functionName_,
                                                      const kernelInfo &info_);

  template <>
  kernel_v* device_t<Pthreads>::buildKernelFromBinary(const std::string &filename,
                                                      const std::string &functionName_);

  template <>
  void device_t<Pthreads>::cacheKernelInLibrary(const std::string &filename,
                                                const std::string &functionName_,
                                                const kernelInfo &info_);

  template <>
  kernel_v* device_t<Pthreads>::loadKernelFromLibrary(const char *cache,
                                                      const std::string &functionName_);

  template <>
  void device_t<Pthreads>::free();

  template <>
  memory_v* device_t<Pthreads>::wrapMemory(void *handle_,
                                           const uintptr_t bytes);

  template <>
  memory_v* device_t<Pthreads>::wrapTexture(void *handle_,
                                            const int dim, const occa::dim &dims,
                                            occa::formatType type, const int permissions);

  template <>
  memory_v* device_t<Pthreads>::malloc(const uintptr_t bytes,
                                       void *source);

  template <>
  memory_v* device_t<Pthreads>::talloc(const int dim, const occa::dim &dims,
                                       void *source,
                                       occa::formatType type, const int permissions);

  template <>
  int device_t<Pthreads>::simdWidth();
  //==================================

#include "operators/occaFunctionPointerTypeDefs.hpp"
#include "operators/occaPthreadsKernelOperators.hpp"

  //---[ Pthreads ]-------------------
  static void* pthreadLimbo(void *args){
    PthreadWorkerData_t &data = *((PthreadWorkerData_t*) args);

    // Thread affinity
#if (OCCA_OS == LINUX_OS)
    cpu_set_t cpuHandle;
    CPU_ZERO(&cpuHandle);
    CPU_SET(data.pinnedCore, &cpuHandle);
#else
    fprintf(stderr, "[Pthreads] Affinity not guaranteed in this OS\n");
#endif

    while(true){
      // Fence local data (incase of out-of-socket updates)
      __asm__ __volatile__ ("lfence");

      if( *(data.pendingJobs) ){
        pthread_mutex_lock(data.kernelMutex);

        std::cout.flush();

        PthreadLaunchHandle_t launchKernel = data.kernelLaunch->front();
        data.kernelLaunch->pop();

        PthreadKernelArg_t &launchArgs = *(data.kernelArgs->front());
        data.kernelArgs->pop();

        pthread_mutex_unlock(data.kernelMutex);

        launchKernel(launchArgs);

        //---[ Barrier ]----------------
        pthread_mutex_lock(data.pendingJobsMutex);
        --( *(data.pendingJobs) );
        pthread_mutex_unlock(data.pendingJobsMutex);

        while((*data.pendingJobs) % data.count)
          __asm__ __volatile__ ("lfence");
        //==============================
      }
    }
  }
  //==================================
};

#  endif
#endif
