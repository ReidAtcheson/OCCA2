#if OCCA_CUDA_ENABLED

#include "occaCUDA.hpp"

namespace occa {
  //---[ Helper Functions ]-----------
  namespace cuda {
    void init(){
      if(!isNotInitialized)
        return;

      cuInit(0);

      isNotInitialized = false;
    }

    occa::device wrapDevice(CUdevice device, CUcontext context){
      occa::device dev;
      device_t<CUDA> &devH      = *(new device_t<CUDA>());
      CUDADeviceData_t &devData = *(new CUDADeviceData_t);

      dev.mode_   = occa::CUDA;
      dev.strMode = "CUDA";
      dev.dHandle = &devH;

      // This will get set in the copy-back
      devH.dev = &dev;

      //---[ Setup ]----------
      devH.data = &devData;

      devData.device  = device;
      devData.context = context;
      //======================

      dev.modelID_ = library::deviceModelID(dev.getIdentifier());
      dev.id_      = library::genDeviceID();

      dev.currentStream = dev.createStream();

      return dev;
    }
  };

  const CUarray_format cudaFormats[8] = {CU_AD_FORMAT_UNSIGNED_INT8,
                                         CU_AD_FORMAT_UNSIGNED_INT16,
                                         CU_AD_FORMAT_UNSIGNED_INT32,
                                         CU_AD_FORMAT_SIGNED_INT8,
                                         CU_AD_FORMAT_SIGNED_INT16,
                                         CU_AD_FORMAT_SIGNED_INT32,
                                         CU_AD_FORMAT_HALF,
                                         CU_AD_FORMAT_FLOAT};

  template <>
  void* formatType::format<occa::CUDA>() const {
    return ((void*) &(cudaFormats[format_]));
  }

  const int CUDA_ADDRESS_NONE  = 0; // cudaBoundaryModeNone
  const int CUDA_ADDRESS_CLAMP = 1; // cudaBoundaryModeClamp
  // cudaBoundaryModeTrap = 2
  //==================================

  //---[ Kernel ]---------------------
  template <>
  kernel_t<CUDA>::kernel_t(){
    data = NULL;
    dev  = NULL;

    functionName = "";

    dims  = 1;
    inner = occa::dim(1,1,1);
    outer = occa::dim(1,1,1);

    nestedKernelCount = 0;

    preferredDimSize_ = 0;

    startTime = (void*) new CUevent;
    endTime   = (void*) new CUevent;
  }

  template <>
  kernel_t<CUDA>::kernel_t(const kernel_t<CUDA> &k){
    data = k.data;
    dev  = k.dev;

    functionName = k.functionName;

    dims  = k.dims;
    inner = k.inner;
    outer = k.outer;

    nestedKernelCount = k.nestedKernelCount;

    if(nestedKernelCount){
      nestedKernels = new kernel[nestedKernelCount];

      for(int i = 0; i < nestedKernelCount; ++i)
        nestedKernels[i] = k.nestedKernels[i];
    }

    preferredDimSize_ = k.preferredDimSize_;

    startTime = k.startTime;
    endTime   = k.endTime;
  }

  template <>
  kernel_t<CUDA>& kernel_t<CUDA>::operator = (const kernel_t<CUDA> &k){
    data = k.data;
    dev  = k.dev;

    functionName = k.functionName;

    dims  = k.dims;
    inner = k.inner;
    outer = k.outer;

    nestedKernelCount = k.nestedKernelCount;

    if(nestedKernelCount){
      nestedKernels = new kernel[nestedKernelCount];

      for(int i = 0; i < nestedKernelCount; ++i)
        nestedKernels[i] = k.nestedKernels[i];
    }

    *((CUevent*) startTime) = *((CUevent*) k.startTime);
    *((CUevent*) endTime)   = *((CUevent*) k.endTime);

    return *this;
  }

  template <>
  kernel_t<CUDA>::~kernel_t(){}

  template <>
  std::string kernel_t<CUDA>::getCachedBinaryName(const std::string &filename,
                                                  kernelInfo &info_){
    OCCA_EXTRACT_DATA(CUDA, Kernel);

    info_.addDefine("OCCA_USING_GPU" , 1);
    info_.addDefine("OCCA_USING_CUDA", 1);

    info_.addOCCAKeywords(occaCUDADefines);

    std::stringstream salt;

    salt << "CUDA"
         << info_.salt()
         << parser::version
         << dev->dHandle->compilerEnvScript
         << dev->dHandle->compiler
         << dev->dHandle->compilerFlags;

    return getCachedName(filename, salt.str());
  }

  template <>
  kernel_t<CUDA>* kernel_t<CUDA>::buildFromSource(const std::string &filename,
                                                  const std::string &functionName_,
                                                  const kernelInfo &info_){
    OCCA_EXTRACT_DATA(CUDA, Kernel);

    functionName = functionName_;

    kernelInfo info = info_;
    std::string cachedBinary = getCachedBinaryName(filename, info);

    struct stat buffer;
    const bool fileExists = (stat(cachedBinary.c_str(), &buffer) == 0);

    if(fileExists){
      std::cout << "Found cached binary of [" << filename << "] in [" << cachedBinary << "]\n";
      return buildFromBinary(cachedBinary, functionName);
    }

    if(!haveFile(cachedBinary)){
      waitForFile(cachedBinary);

      return buildFromBinary(cachedBinary, functionName);
    }

    std::string iCachedBinary = createIntermediateSource(filename,
                                                         cachedBinary,
                                                         info);

    std::string libPath, soname;
    getFilePrefixAndName(cachedBinary, libPath, soname);

    std::string oCachedBinary = libPath + "o_" + soname + ".o";

    std::string archSM = "";

    if(dev->dHandle->compilerFlags.find("-arch=sm_") == std::string::npos){
      std::stringstream archSM_;

      int major, minor;
      OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Getting CUDA Device Arch",
                      cuDeviceComputeCapability(&major, &minor, data_.device) );

      archSM_ << " -arch=sm_" << major << minor << ' ';

      archSM = archSM_.str();
    }

    std::stringstream command;

    //---[ PTX Check Command ]----------
    if(dev->dHandle->compilerEnvScript.size())
      command << dev->dHandle->compilerEnvScript << " && ";

    command << dev->dHandle->compiler
            << ' '          << dev->dHandle->compilerFlags
            << archSM
            << " -Xptxas -v,-dlcm=cg,-abi=no"
            << ' '          << info.flags
            << " -x cu -c " << iCachedBinary
            << " -o "       << oCachedBinary;

    const std::string &ptxCommand = command.str();

    std::cout << "Compiling [" << functionName << "]\n" << ptxCommand << "\n";

#if (OCCA_OS == LINUX_OS) || (OCCA_OS == OSX_OS)
    const int ptxError = system(ptxCommand.c_str());
#else
    const int ptxError = system(("\"" +  ptxCommand + "\"").c_str());
#endif

    // Not needed here I guess
    // if(ptxError){
    //   releaseFile(cachedBinary);
    //   throw 1;
    // }

    //---[ Compiling Command ]----------
    command.str("");

    command << dev->dHandle->compiler
            << " -o "       << cachedBinary
            << " -ptx -I."
            << ' '          << dev->dHandle->compilerFlags
            << archSM
            << ' '          << info.flags
            << " -x cu "    << iCachedBinary;

    const std::string &sCommand = command.str();

    std::cout << sCommand << '\n';

    const int compileError = system(sCommand.c_str());

    if(compileError){
      releaseFile(cachedBinary);
      throw 1;
    }

    const CUresult moduleLoadError = cuModuleLoad(&data_.module,
                                                  cachedBinary.c_str());

    if(moduleLoadError)
      releaseFile(cachedBinary);

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Module",
                    moduleLoadError);

    const CUresult moduleGetFunctionError = cuModuleGetFunction(&data_.function,
                                                                data_.module,
                                                                functionName.c_str());

    if(moduleGetFunctionError)
      releaseFile(cachedBinary);

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Function",
                    moduleGetFunctionError);

    releaseFile(cachedBinary);

    return this;
  }

  template <>
  kernel_t<CUDA>* kernel_t<CUDA>::buildFromBinary(const std::string &filename,
                                                 const std::string &functionName_){
    OCCA_EXTRACT_DATA(CUDA, Kernel);

    functionName = functionName_;

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Module",
                    cuModuleLoad(&data_.module, filename.c_str()));

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Function",
                    cuModuleGetFunction(&data_.function, data_.module, functionName.c_str()));

    return this;
  }

  template <>
  kernel_t<CUDA>* kernel_t<CUDA>::loadFromLibrary(const char *cache,
                                                  const std::string &functionName_){
    OCCA_EXTRACT_DATA(CUDA, Kernel);

    functionName = functionName_;

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Module",
                    cuModuleLoadData(&data_.module, cache));

    OCCA_CUDA_CHECK("Kernel (" + functionName + ") : Loading Function",
                    cuModuleGetFunction(&data_.function, data_.module, functionName.c_str()));

    return this;
  }

  template <>
  int kernel_t<CUDA>::preferredDimSize(){
    preferredDimSize_ = 32;
    return 32;
  }

#include "operators/occaCUDAKernelOperators.cpp"

  template <>
  double kernel_t<CUDA>::timeTaken(){
    CUevent &startEvent = *((CUevent*) startTime);
    CUevent &endEvent   = *((CUevent*) endTime);

    cuEventSynchronize(endEvent);

    float msTimeTaken;
    cuEventElapsedTime(&msTimeTaken, startEvent, endEvent);

    return (1.0e-3 * msTimeTaken);
  }

  template <>
  double kernel_t<CUDA>::timeTakenBetween(void *start, void *end){
    CUevent &startEvent = *((CUevent*) start);
    CUevent &endEvent   = *((CUevent*) end);

    cuEventSynchronize(endEvent);

    float msTimeTaken;
    cuEventElapsedTime(&msTimeTaken, startEvent, endEvent);

    return (1.0e-3 * msTimeTaken);
  }

  template <>
  void kernel_t<CUDA>::free(){
  }
  //==================================


  //---[ Memory ]---------------------
  template <>
  memory_t<CUDA>::memory_t(){
    handle = NULL;
    dev    = NULL;
    size = 0;

    isTexture = false;
    textureInfo.dim = 1;
    textureInfo.w = textureInfo.h = textureInfo.d = 0;

    isAWrapper = false;
  }

  template <>
  memory_t<CUDA>::memory_t(const memory_t<CUDA> &m){
    handle = m.handle;
    dev    = m.dev;
    size   = m.size;

    isTexture = m.isTexture;
    textureInfo.dim  = m.textureInfo.dim;

    textureInfo.w = m.textureInfo.w;
    textureInfo.h = m.textureInfo.h;
    textureInfo.d = m.textureInfo.d;

    isAWrapper = m.isAWrapper;
  }

  template <>
  memory_t<CUDA>& memory_t<CUDA>::operator = (const memory_t<CUDA> &m){
    handle = m.handle;
    dev    = m.dev;
    size   = m.size;

    isTexture = m.isTexture;
    textureInfo.dim  = m.textureInfo.dim;

    textureInfo.w = m.textureInfo.w;
    textureInfo.h = m.textureInfo.h;
    textureInfo.d = m.textureInfo.d;

    isAWrapper = m.isAWrapper;

    return *this;
  }

  template <>
  memory_t<CUDA>::~memory_t(){}

  template <>
  void* memory_t<CUDA>::getMemoryHandle(){
    return handle;
  }

  template <>
  void* memory_t<CUDA>::getTextureHandle(){
    return textureInfo.arg;
  }

  template <>
  void memory_t<CUDA>::copyFrom(const void *source,
                                const uintptr_t bytes,
                                const uintptr_t offset){
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + offset) <= size);

    if(!isTexture)
      OCCA_CUDA_CHECK("Memory: Copy From",
                      cuMemcpyHtoD(*((CUdeviceptr*) handle) + offset, source, bytes_) );
    else{
      if(textureInfo.dim == 1)
        OCCA_CUDA_CHECK("Texture Memory: Copy From",
                        cuMemcpyHtoA(((CUDATextureData_t*) handle)->array, offset, source, bytes_) );
      else{
        CUDA_MEMCPY2D info;

        info.srcXInBytes   = 0;
        info.srcY          = 0;
        info.srcMemoryType = CU_MEMORYTYPE_HOST;
        info.srcHost       = source;
        info.srcPitch      = 0;

        info.dstXInBytes   = offset;
        info.dstY          = 0;
        info.dstMemoryType = CU_MEMORYTYPE_ARRAY;
        info.dstArray      = ((CUDATextureData_t*) handle)->array;

        info.WidthInBytes = textureInfo.w * textureInfo.bytesInEntry;
        info.Height       = (bytes_ / info.WidthInBytes);

        cuMemcpy2D(&info);

        dev->finish();
      }
    }
  }

  template <>
  void memory_t<CUDA>::copyFrom(const memory_v *source,
                                const uintptr_t bytes,
                                const uintptr_t destOffset,
                                const uintptr_t srcOffset){
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + destOffset) <= size);
    OCCA_CHECK((bytes_ + srcOffset)  <= source->size);

    void *dstPtr, *srcPtr;

    if(!isTexture)
      dstPtr = (void*) ((CUDATextureData_t*) handle)->array;
    else
      dstPtr = handle;

    if( !(source->isTexture) )
      srcPtr = (void*) ((CUDATextureData_t*) source->handle)->array;
    else
      srcPtr = source->handle;

    if(!isTexture){
      if(!source->isTexture)
        OCCA_CUDA_CHECK("Memory: Copy From [Memory -> Memory]",
                        cuMemcpyDtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Copy From [Memory -> Texture]",
                        cuMemcpyDtoA((CUarray) dstPtr         , destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
    }
    else{
      if(source->isTexture)
        OCCA_CUDA_CHECK("Memory: Copy From [Texture -> Memory]",
                        cuMemcpyAtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     (CUarray) srcPtr         , srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Copy From [Texture -> Texture]",
                        cuMemcpyAtoA((CUarray) dstPtr, destOffset,
                                     (CUarray) srcPtr, srcOffset,
                                     bytes_) );
    }
  }

  template <>
  void memory_t<CUDA>::copyTo(void *dest,
                              const uintptr_t bytes,
                              const uintptr_t offset){
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + offset) <= size);

    if(!isTexture)
      OCCA_CUDA_CHECK("Memory: Copy To",
                      cuMemcpyDtoH(dest, *((CUdeviceptr*) handle) + offset, bytes_) );
    else{
      if(textureInfo.dim == 1)
        OCCA_CUDA_CHECK("Texture Memory: Copy To",
                        cuMemcpyAtoH(dest, ((CUDATextureData_t*) handle)->array, offset, bytes_) );
      else{
        CUDA_MEMCPY2D info;

        info.srcXInBytes   = offset;
        info.srcY          = 0;
        info.srcMemoryType = CU_MEMORYTYPE_ARRAY;
        info.srcArray      = ((CUDATextureData_t*) handle)->array;

        info.dstXInBytes   = 0;
        info.dstY          = 0;
        info.dstMemoryType = CU_MEMORYTYPE_HOST;
        info.dstHost       = dest;
        info.dstPitch      = 0;

        info.WidthInBytes = textureInfo.w * textureInfo.bytesInEntry;
        info.Height       = (bytes_ / info.WidthInBytes);

        cuMemcpy2D(&info);

        dev->finish();
      }
    }
  }

  template <>
  void memory_t<CUDA>::copyTo(memory_v *dest,
                              const uintptr_t bytes,
                              const uintptr_t destOffset,
                              const uintptr_t srcOffset){
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + srcOffset)  <= size);
    OCCA_CHECK((bytes_ + destOffset) <= dest->size);

    void *dstPtr, *srcPtr;

    if(!isTexture)
      srcPtr = (void*) ((CUDATextureData_t*) handle)->array;
    else
      srcPtr = handle;

    if( !(dest->isTexture) )
      dstPtr = (void*) ((CUDATextureData_t*) dest->handle)->array;
    else
      dstPtr = dest->handle;

    if(!isTexture){
      if(!dest->isTexture)
        OCCA_CUDA_CHECK("Memory: Copy To [Memory -> Memory]",
                        cuMemcpyDtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Copy To [Memory -> Texture]",
                        cuMemcpyDtoA((CUarray) dstPtr         , destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
    }
    else{
      if(dest->isTexture)
        OCCA_CUDA_CHECK("Memory: Copy To [Texture -> Memory]",
                        cuMemcpyAtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     (CUarray) srcPtr         , srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Copy To [Texture -> Texture]",
                        cuMemcpyAtoA((CUarray) dstPtr, destOffset,
                                     (CUarray) srcPtr, srcOffset,
                                     bytes_) );
    }
  }

  template <>
  void memory_t<CUDA>::asyncCopyFrom(const void *source,
                                     const uintptr_t bytes,
                                     const uintptr_t offset){
    const CUstream &stream = *((CUstream*) dev->currentStream);
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + offset) <= size);

    if(!isTexture)
      OCCA_CUDA_CHECK("Memory: Asynchronous Copy From",
                      cuMemcpyHtoDAsync(*((CUdeviceptr*) handle) + offset, source, bytes_, stream) );
    else
      OCCA_CUDA_CHECK("Texture Memory: Asynchronous Copy From",
                      cuMemcpyHtoAAsync(((CUDATextureData_t*) handle)->array, offset, source, bytes_, stream) );
  }

  template <>
  void memory_t<CUDA>::asyncCopyFrom(const memory_v *source,
                                     const uintptr_t bytes,
                                     const uintptr_t destOffset,
                                     const uintptr_t srcOffset){
    const CUstream &stream = *((CUstream*) dev->currentStream);
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + destOffset) <= size);
    OCCA_CHECK((bytes_ + srcOffset)  <= source->size);

    void *dstPtr, *srcPtr;

    if(!isTexture)
      dstPtr = (void*) ((CUDATextureData_t*) handle)->array;
    else
      dstPtr = handle;

    if( !(source->isTexture) )
      srcPtr = (void*) ((CUDATextureData_t*) source->handle)->array;
    else
      srcPtr = source->handle;

    if(!isTexture){
      if(!source->isTexture)
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy From [Memory -> Memory]",
                        cuMemcpyDtoDAsync(*((CUdeviceptr*) dstPtr) + destOffset,
                                          *((CUdeviceptr*) srcPtr) + srcOffset,
                                          bytes_, stream) );
      else
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy From [Memory -> Texture]",
                        cuMemcpyDtoA((CUarray) dstPtr         , destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
    }
    else{
      if(source->isTexture)
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy From [Texture -> Memory]",
                        cuMemcpyAtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     (CUarray) srcPtr         , srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy From [Texture -> Texture]",
                        cuMemcpyAtoA((CUarray) dstPtr, destOffset,
                                     (CUarray) srcPtr, srcOffset,
                                     bytes_) );
    }
  }

  template <>
  void memory_t<CUDA>::asyncCopyTo(void *dest,
                                   const uintptr_t bytes,
                                   const uintptr_t offset){
    const CUstream &stream = *((CUstream*) dev->currentStream);
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + offset) <= size);

    if(!isTexture)
      OCCA_CUDA_CHECK("Memory: Asynchronous Copy To",
                      cuMemcpyDtoHAsync(dest, *((CUdeviceptr*) handle) + offset, bytes_, stream) );
    else
      OCCA_CUDA_CHECK("Texture Memory: Asynchronous Copy To",
                      cuMemcpyAtoHAsync(dest,((CUDATextureData_t*) handle)->array, offset, bytes_, stream) );
  }

  template <>
  void memory_t<CUDA>::asyncCopyTo(memory_v *dest,
                                   const uintptr_t bytes,
                                   const uintptr_t destOffset,
                                   const uintptr_t srcOffset){
    const CUstream &stream = *((CUstream*) dev->currentStream);
    const uintptr_t bytes_ = (bytes == 0) ? size : bytes;

    OCCA_CHECK((bytes_ + srcOffset)  <= size);
    OCCA_CHECK((bytes_ + destOffset) <= dest->size);

    void *dstPtr, *srcPtr;

    if(!isTexture)
      srcPtr = (void*) ((CUDATextureData_t*) handle)->array;
    else
      srcPtr = handle;

    if( !(dest->isTexture) )
      dstPtr = (void*) ((CUDATextureData_t*) dest->handle)->array;
    else
      dstPtr = dest->handle;

    if(!isTexture){
      if(!dest->isTexture)
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy To [Memory -> Memory]",
                        cuMemcpyDtoDAsync(*((CUdeviceptr*) dstPtr) + destOffset,
                                          *((CUdeviceptr*) srcPtr) + srcOffset,
                                          bytes_, stream) );
      else
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy To [Memory -> Texture]",
                        cuMemcpyDtoA((CUarray) dstPtr         , destOffset,
                                     *((CUdeviceptr*) srcPtr) + srcOffset,
                                     bytes_) );
    }
    else{
      if(dest->isTexture)
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy To [Texture -> Memory]",
                        cuMemcpyAtoD(*((CUdeviceptr*) dstPtr) + destOffset,
                                     (CUarray) srcPtr         , srcOffset,
                                     bytes_) );
      else
        OCCA_CUDA_CHECK("Memory: Asynchronous Copy To [Texture -> Texture]",
                        cuMemcpyAtoA((CUarray) dstPtr, destOffset,
                                     (CUarray) srcPtr, srcOffset,
                                     bytes_) );
    }
  }

  template <>
  void memory_t<CUDA>::free(){
    if(!isTexture){
      cuMemFree(*((CUdeviceptr*) handle));

      if(!isAWrapper)
        delete (CUdeviceptr*) handle;
    }
    else{
      CUarray &array        = ((CUDATextureData_t*) handle)->array;
      CUsurfObject &surface = ((CUDATextureData_t*) handle)->surface;

      cuArrayDestroy(array);
      cuSurfObjectDestroy(surface);

      if(!isAWrapper){
        delete (CUDATextureData_t*) handle;
        delete (CUaddress_mode*)    textureInfo.arg;
      }
    }

    size = 0;
  }
  //==================================


  //---[ Device ]---------------------
  template <>
  device_t<CUDA>::device_t() {
    data            = NULL;
    memoryAllocated = 0;

    getEnvironmentVariables();
  }

  template <>
  device_t<CUDA>::device_t(const device_t<CUDA> &d){
    data            = d.data;
    memoryAllocated = d.memoryAllocated;

    compiler      = d.compiler;
    compilerFlags = d.compilerFlags;
  }

  template <>
  device_t<CUDA>& device_t<CUDA>::operator = (const device_t<CUDA> &d){
    data            = d.data;
    memoryAllocated = d.memoryAllocated;

    compiler      = d.compiler;
    compilerFlags = d.compilerFlags;

    return *this;
  }

  template <>
  void device_t<CUDA>::setup(argInfoMap &aim){
    cuda::init();

    data = new CUDADeviceData_t;

    OCCA_EXTRACT_DATA(CUDA, Device);

    if(!aim.has("deviceID")){
      std::cout << "[CUDA] device not given [deviceID]\n";
      throw 1;
    }

    const int deviceID = aim.iGet("deviceID");

    OCCA_CUDA_CHECK("Device: Creating Device",
                    cuDeviceGet(&data_.device, deviceID));

    OCCA_CUDA_CHECK("Device: Creating Context",
                    cuCtxCreate(&data_.context, CU_CTX_SCHED_AUTO, data_.device));
  }

  template <>
  deviceIdentifier device_t<CUDA>::getIdentifier() const {
    deviceIdentifier dID;

    dID.mode_ = CUDA;

    const size_t archPos = compilerFlags.find("-arch=sm_");

    if(archPos == std::string::npos){
      OCCA_EXTRACT_DATA(CUDA, Device);

      std::stringstream archSM_;

      int major, minor;
      OCCA_CUDA_CHECK("Getting CUDA Device Arch",
                      cuDeviceComputeCapability(&major, &minor, data_.device) );

      archSM_ << major << minor;

      dID.flagMap["sm_arch"] = archSM_.str();
    }
    else{
      const char *c0 = (compilerFlags.c_str() + archPos);
      const char *c1 = c0;

      while((*c0 != '\0') && (*c0 != ' '))
        ++c1;

      dID.flagMap["sm_arch"] = std::string(c0, c1 - c0);
    }

    return dID;
  }

  template <>
  void device_t<CUDA>::getEnvironmentVariables(){
    char *c_compiler = getenv("OCCA_CUDA_COMPILER");

    if(c_compiler != NULL)
      compiler = std::string(c_compiler);
    else
      compiler = "nvcc";

    char *c_compilerFlags = getenv("OCCA_CUDA_COMPILER_FLAGS");

    if(c_compilerFlags != NULL)
      compilerFlags = std::string(c_compilerFlags);
    else{
#if OCCA_DEBUG_ENABLED
      compilerFlags = "-g";
#else
      compilerFlags = "";
#endif
    }
  }

  template <>
  void device_t<CUDA>::appendAvailableDevices(std::vector<device> &dList){
    cuda::init();

    int deviceCount;

    OCCA_CUDA_CHECK("Finding Number of Devices",
                    cuDeviceGetCount(&deviceCount));

    for(int i = 0; i < deviceCount; ++i){
      device d;
      d.setup("CUDA", i, 0);

      dList.push_back(d);
    }
  }

  template <>
  void device_t<CUDA>::setCompiler(const std::string &compiler_){
    compiler = compiler_;
  }

  template <>
  void device_t<CUDA>::setCompilerEnvScript(const std::string &compilerEnvScript_){
    compilerEnvScript = compilerEnvScript_;
  }

  template <>
  void device_t<CUDA>::setCompilerFlags(const std::string &compilerFlags_){
    compilerFlags = compilerFlags_;
  }

  template <>
  std::string& device_t<CUDA>::getCompiler(){
    return compiler;
  }

  template <>
  std::string& device_t<CUDA>::getCompilerEnvScript(){
    return compilerEnvScript;
  }

  template <>
  std::string& device_t<CUDA>::getCompilerFlags(){
    return compilerFlags;
  }

  template <>
  void device_t<CUDA>::flush(){}

  template <>
  void device_t<CUDA>::finish(){
    OCCA_CUDA_CHECK("Device: Finish",
                    cuStreamSynchronize(*((CUstream*) dev->currentStream)) );
  }

  template <>
  void device_t<CUDA>::waitFor(tag tag_){
    cuEventSynchronize(tag_.cuEvent);
  }

  template <>
  stream device_t<CUDA>::createStream(){
    OCCA_EXTRACT_DATA(CUDA, Device);

    CUstream *retStream = new CUstream;

    OCCA_CUDA_CHECK("Device: createStream",
                    cuStreamCreate(retStream, CU_STREAM_DEFAULT));

    return retStream;
  }

  template <>
  void device_t<CUDA>::freeStream(stream s){
    OCCA_CUDA_CHECK("Device: freeStream",
                    cuStreamDestroy( *((CUstream*) s) ));
    delete (CUstream*) s;
  }

  template <>
  stream device_t<CUDA>::wrapStream(void *handle_){
    return handle_;
  }

  template <>
  tag device_t<CUDA>::tagStream(){
    tag ret;

    cuEventCreate(&(ret.cuEvent), CU_EVENT_DEFAULT);
    cuEventRecord(ret.cuEvent, 0);

    return ret;
  }

  template <>
  double device_t<CUDA>::timeBetween(const tag &startTag, const tag &endTag){
    cuEventSynchronize(endTag.cuEvent);

    float msTimeTaken;
    cuEventElapsedTime(&msTimeTaken, startTag.cuEvent, endTag.cuEvent);

    return (double) (1.0e-3 * (double) msTimeTaken);
  }

  template <>
  kernel_v* device_t<CUDA>::buildKernelFromSource(const std::string &filename,
                                                 const std::string &functionName,
                                                 const kernelInfo &info_){
    OCCA_EXTRACT_DATA(CUDA, Device);

    kernel_v *k = new kernel_t<CUDA>;

    k->dev  = dev;
    k->data = new CUDAKernelData_t;

    CUDAKernelData_t &kData_ = *((CUDAKernelData_t*) k->data);

    kData_.device  = data_.device;
    kData_.context = data_.context;

    k->buildFromSource(filename, functionName, info_);
    return k;
  }

  template <>
  kernel_v* device_t<CUDA>::buildKernelFromBinary(const std::string &filename,
                                                 const std::string &functionName){
    OCCA_EXTRACT_DATA(CUDA, Device);

    kernel_v *k = new kernel_t<CUDA>;

    k->dev  = dev;
    k->data = new CUDAKernelData_t;

    CUDAKernelData_t &kData_ = *((CUDAKernelData_t*) k->data);

    kData_.device  = data_.device;
    kData_.context = data_.context;

    k->buildFromBinary(filename, functionName);
    return k;
  }

  template <>
  void device_t<CUDA>::cacheKernelInLibrary(const std::string &filename,
                                            const std::string &functionName,
                                            const kernelInfo &info_){
    //---[ Creating shared library ]----
    kernel tmpK = dev->buildKernelFromSource(filename, functionName, info_);
    tmpK.free();

    kernelInfo info = info_;
    info.addDefine("OCCA_USING_GPU" , 1);
    info.addDefine("OCCA_USING_CUDA", 1);

    info.addOCCAKeywords(occaCUDADefines);

    std::stringstream salt;
    salt << "CUDA"
         << info.salt()
         << parser::version
         << dev->dHandle->compilerEnvScript
         << dev->dHandle->compiler
         << dev->dHandle->compilerFlags;

    std::string cachedBinary = getCachedName(filename, salt.str());
    std::string contents     = readFile(cachedBinary);
    //==================================

    library::infoID_t infoID;

    infoID.modelID    = dev->modelID_;
    infoID.kernelName = functionName;

    library::infoHeader_t &header = library::headerMap[infoID];

    header.fileID = -1;
    header.mode   = CUDA;

    const std::string flatDevID = getIdentifier().flattenFlagMap();

    header.flagsOffset = library::addToScratchPad(flatDevID);
    header.flagsBytes  = flatDevID.size();

    header.contentOffset = library::addToScratchPad(contents);
    header.contentBytes  = contents.size();

    header.kernelNameOffset = library::addToScratchPad(functionName);
    header.kernelNameBytes  = functionName.size();
  }

  template <>
  kernel_v* device_t<CUDA>::loadKernelFromLibrary(const char *cache,
                                                  const std::string &functionName){
    OCCA_EXTRACT_DATA(CUDA, Device);

    kernel_v *k = new kernel_t<CUDA>;

    k->dev  = dev;
    k->data = new CUDAKernelData_t;

    CUDAKernelData_t &kData_ = *((CUDAKernelData_t*) k->data);

    kData_.device  = data_.device;
    kData_.context = data_.context;

    k->loadFromLibrary(cache, functionName);
    return k;
  }

  template <>
  memory_v* device_t<CUDA>::wrapMemory(void *handle_,
                                       const uintptr_t bytes){
    memory_v *mem = new memory_t<CUDA>;

    // CUdeviceptr ~ void*
    mem->dev    = dev;
    mem->size   = bytes;
    mem->handle = &handle_;

    mem->isAWrapper = true;

    return mem;
  }

  template <>
  memory_v* device_t<CUDA>::wrapTexture(void *handle_,
                                        const int dim, const occa::dim &dims,
                                        occa::formatType type, const int permissions){
    memory_v *mem = new memory_t<CUDA>;

    mem->dev    = dev;
    mem->size   = ((dim == 1) ? dims.x : (dims.x * dims.y)) * type.bytes();
    mem->handle = handle_;

    mem->isTexture = true;
    mem->textureInfo.dim  = dim;

    mem->textureInfo.w = dims.x;
    mem->textureInfo.h = dims.y;
    mem->textureInfo.d = dims.z;

    mem->textureInfo.bytesInEntry = type.bytes();

    mem->isAWrapper = true;

    return mem;
  }

  template <>
  memory_v* device_t<CUDA>::malloc(const uintptr_t bytes,
                                   void *source){
    OCCA_EXTRACT_DATA(CUDA, Device);

    memory_v *mem = new memory_t<CUDA>;

    mem->dev    = dev;
    mem->handle = new CUdeviceptr;
    mem->size   = bytes;

    OCCA_CUDA_CHECK("Device: malloc",
                    cuMemAlloc((CUdeviceptr*) mem->handle, bytes));

    if(source != NULL)
      mem->copyFrom(source, bytes, 0);

    return mem;
  }

  template <>
  memory_v* device_t<CUDA>::talloc(const int dim, const occa::dim &dims,
                                   void *source,
                                   occa::formatType type, const int permissions){
    OCCA_EXTRACT_DATA(CUDA, Device);

    memory_v *mem = new memory_t<CUDA>;

    mem->dev    = dev;
    mem->handle = new CUDATextureData_t;
    mem->size   = ((dim == 1) ? dims.x : (dims.x * dims.y)) * type.bytes();

    mem->isTexture = true;
    mem->textureInfo.dim  = dim;

    mem->textureInfo.w = dims.x;
    mem->textureInfo.h = dims.y;
    mem->textureInfo.d = dims.z;

    mem->textureInfo.bytesInEntry = type.bytes();

    CUarray &array        = ((CUDATextureData_t*) mem->handle)->array;
    CUsurfObject &surface = ((CUDATextureData_t*) mem->handle)->surface;

    CUDA_ARRAY_DESCRIPTOR arrayDesc;
    CUDA_RESOURCE_DESC surfDesc;

    memset(&arrayDesc, 0, sizeof(arrayDesc));
    memset(&surfDesc , 0, sizeof(surfDesc));

    arrayDesc.Width       = dims.x;
    arrayDesc.Height      = (dim == 1) ? 0 : dims.y;
    arrayDesc.Format      = *((CUarray_format*) type.format<CUDA>());
    arrayDesc.NumChannels = type.count();

    OCCA_CUDA_CHECK("Device: Creating Array",
                    cuArrayCreate(&array, (CUDA_ARRAY_DESCRIPTOR*) &arrayDesc) );

    surfDesc.res.array.hArray = array;
    surfDesc.resType = CU_RESOURCE_TYPE_ARRAY;

    OCCA_CUDA_CHECK("Device: Creating Surface Object",
                    cuSurfObjectCreate(&surface, &surfDesc) );

    mem->textureInfo.arg = new int;
    *((int*) mem->textureInfo.arg) = CUDA_ADDRESS_CLAMP;

    mem->copyFrom(source);

    /*
      if(dims == 3){
      CUDA_ARRAY3D_DESCRIPTOR arrayDesc;
      memset(&arrayDesc, 0, sizeof(arrayDesc);

      arrayDesc.Width  = size.x;
      arrayDesc.Height = size.y;
      arrayDesc.Depth  = size.z;

      arrayDesc.Format      = type.format<CUDA>();
      arrayDesc.NumChannels = type.count();

      cuArray3DCreate(&arr, (CUDA_ARRAY3D_DESCRIPTOR*) &arrayDesc);
      }
    */

    return mem;
  }

  template <>
  void device_t<CUDA>::free(){
    OCCA_EXTRACT_DATA(CUDA, Device);

    OCCA_CUDA_CHECK("Device: Freeing Context",
                    cuCtxDestroy(data_.context) );

    delete (CUDADeviceData_t*) data;
  }

  template <>
  int device_t<CUDA>::simdWidth(){
    if(simdWidth_)
      return simdWidth_;

    OCCA_EXTRACT_DATA(CUDA, Device);

    OCCA_CUDA_CHECK("Device: Get Warp Size",
                    cuDeviceGetAttribute(&simdWidth_,
                                         CU_DEVICE_ATTRIBUTE_WARP_SIZE,
                                         data_.device) );

    return simdWidth_;
  }
  //==================================


  //---[ Error Handling ]-------------
  std::string cudaError(const CUresult errorCode){
    switch(errorCode){
    case CUDA_SUCCESS:                              return "CUDA_SUCCESS";
    case CUDA_ERROR_INVALID_VALUE:                  return "CUDA_ERROR_INVALID_VALUE";
    case CUDA_ERROR_OUT_OF_MEMORY:                  return "CUDA_ERROR_OUT_OF_MEMORY";
    case CUDA_ERROR_NOT_INITIALIZED:                return "CUDA_ERROR_NOT_INITIALIZED";
    case CUDA_ERROR_DEINITIALIZED:                  return "CUDA_ERROR_DEINITIALIZED";
    case CUDA_ERROR_PROFILER_DISABLED:              return "CUDA_ERROR_PROFILER_DISABLED";
    case CUDA_ERROR_PROFILER_NOT_INITIALIZED:       return "CUDA_ERROR_PROFILER_NOT_INITIALIZED";
    case CUDA_ERROR_PROFILER_ALREADY_STARTED:       return "CUDA_ERROR_PROFILER_ALREADY_STARTED";
    case CUDA_ERROR_PROFILER_ALREADY_STOPPED:       return "CUDA_ERROR_PROFILER_ALREADY_STOPPED";
    case CUDA_ERROR_NO_DEVICE:                      return "CUDA_ERROR_NO_DEVICE";
    case CUDA_ERROR_INVALID_DEVICE:                 return "CUDA_ERROR_INVALID_DEVICE";
    case CUDA_ERROR_INVALID_IMAGE:                  return "CUDA_ERROR_INVALID_IMAGE";
    case CUDA_ERROR_INVALID_CONTEXT:                return "CUDA_ERROR_INVALID_CONTEXT";
    case CUDA_ERROR_CONTEXT_ALREADY_CURRENT:        return "CUDA_ERROR_CONTEXT_ALREADY_CURRENT";
    case CUDA_ERROR_MAP_FAILED:                     return "CUDA_ERROR_MAP_FAILED";
    case CUDA_ERROR_UNMAP_FAILED:                   return "CUDA_ERROR_UNMAP_FAILED";
    case CUDA_ERROR_ARRAY_IS_MAPPED:                return "CUDA_ERROR_ARRAY_IS_MAPPED";
    case CUDA_ERROR_ALREADY_MAPPED:                 return "CUDA_ERROR_ALREADY_MAPPED";
    case CUDA_ERROR_NO_BINARY_FOR_GPU:              return "CUDA_ERROR_NO_BINARY_FOR_GPU";
    case CUDA_ERROR_ALREADY_ACQUIRED:               return "CUDA_ERROR_ALREADY_ACQUIRED";
    case CUDA_ERROR_NOT_MAPPED:                     return "CUDA_ERROR_NOT_MAPPED";
    case CUDA_ERROR_NOT_MAPPED_AS_ARRAY:            return "CUDA_ERROR_NOT_MAPPED_AS_ARRAY";
    case CUDA_ERROR_NOT_MAPPED_AS_POINTER:          return "CUDA_ERROR_NOT_MAPPED_AS_POINTER";
    case CUDA_ERROR_ECC_UNCORRECTABLE:              return "CUDA_ERROR_ECC_UNCORRECTABLE";
    case CUDA_ERROR_UNSUPPORTED_LIMIT:              return "CUDA_ERROR_UNSUPPORTED_LIMIT";
    case CUDA_ERROR_CONTEXT_ALREADY_IN_USE:         return "CUDA_ERROR_CONTEXT_ALREADY_IN_USE";
    case CUDA_ERROR_PEER_ACCESS_UNSUPPORTED:        return "CUDA_ERROR_PEER_ACCESS_UNSUPPORTED";
    case CUDA_ERROR_INVALID_SOURCE:                 return "CUDA_ERROR_INVALID_SOURCE";
    case CUDA_ERROR_FILE_NOT_FOUND:                 return "CUDA_ERROR_FILE_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND: return "CUDA_ERROR_SHARED_OBJECT_SYMBOL_NOT_FOUND";
    case CUDA_ERROR_SHARED_OBJECT_INIT_FAILED:      return "CUDA_ERROR_SHARED_OBJECT_INIT_FAILED";
    case CUDA_ERROR_OPERATING_SYSTEM:               return "CUDA_ERROR_OPERATING_SYSTEM";
    case CUDA_ERROR_INVALID_HANDLE:                 return "CUDA_ERROR_INVALID_HANDLE";
    case CUDA_ERROR_NOT_FOUND:                      return "CUDA_ERROR_NOT_FOUND";
    case CUDA_ERROR_NOT_READY:                      return "CUDA_ERROR_NOT_READY";
    case CUDA_ERROR_LAUNCH_FAILED:                  return "CUDA_ERROR_LAUNCH_FAILED";
    case CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES:        return "CUDA_ERROR_LAUNCH_OUT_OF_RESOURCES";
    case CUDA_ERROR_LAUNCH_TIMEOUT:                 return "CUDA_ERROR_LAUNCH_TIMEOUT";
    case CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING:  return "CUDA_ERROR_LAUNCH_INCOMPATIBLE_TEXTURING";
    case CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED:    return "CUDA_ERROR_PEER_ACCESS_ALREADY_ENABLED";
    case CUDA_ERROR_PEER_ACCESS_NOT_ENABLED:        return "CUDA_ERROR_PEER_ACCESS_NOT_ENABLED";
    case CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE:         return "CUDA_ERROR_PRIMARY_CONTEXT_ACTIVE";
    case CUDA_ERROR_CONTEXT_IS_DESTROYED:           return "CUDA_ERROR_CONTEXT_IS_DESTROYED";
    case CUDA_ERROR_ASSERT:                         return "CUDA_ERROR_ASSERT";
    case CUDA_ERROR_TOO_MANY_PEERS:                 return "CUDA_ERROR_TOO_MANY_PEERS";
    case CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED: return "CUDA_ERROR_HOST_MEMORY_ALREADY_REGISTERED";
    case CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED:     return "CUDA_ERROR_HOST_MEMORY_NOT_REGISTERED";
    case CUDA_ERROR_NOT_PERMITTED:                  return "CUDA_ERROR_NOT_PERMITTED";
    case CUDA_ERROR_NOT_SUPPORTED:                  return "CUDA_ERROR_NOT_SUPPORTED";
    default:                                        return "UNKNOWN ERROR";
    };
    //==================================
  }
};

#endif
