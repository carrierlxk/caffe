// Copyright 2014 BVLC and contributors.

#include <cuda_runtime.h>

#include <cstring>

#include "caffe/common.hpp"
#include "caffe/syncedmem.hpp"

namespace caffe {

SyncedMemory::~SyncedMemory() {
  if (cpu_ptr_ && own_cpu_data_) {
    CaffeFreeHost(cpu_ptr_);
  }
  if (gpu_data_) {
    CUDA_CHECK(cudaFree(gpu_data_));
  }
}

inline void SyncedMemory::to_cpu() {
  switch (head_) {
  case UNINITIALIZED:
    cpu_resize();
    head_ = HEAD_AT_CPU;
    own_cpu_data_ = true;
    break;
  case HEAD_AT_GPU:
    gpu_resize();
    cpu_resize();
    CUDA_CHECK(cudaMemcpy(cpu_data_, gpu_data_, size_, cudaMemcpyDeviceToHost));
    if (cpu_ptr_ == NULL) {
      CaffeMallocHost(&cpu_ptr_, size_);
      own_cpu_data_ = true;
    }
    head_ = SYNCED;
    break;
  case HEAD_AT_CPU:
    cpu_resize();
    break;
  case SYNCED:
    if (cpu_resize()) {
      head_ = HEAD_AT_CPU;
    }
    break;
  }
}

inline void SyncedMemory::to_gpu() {
  switch (head_) {
  case UNINITIALIZED:
    gpu_resize();
    head_ = HEAD_AT_GPU;
    break;
  case HEAD_AT_CPU:
    cpu_resize();
    gpu_resize();
    CUDA_CHECK(cudaMemcpy(gpu_data_, cpu_data_, size_, cudaMemcpyHostToDevice));
    head_ = SYNCED;
    break;
  case HEAD_AT_GPU:
    gpu_resize();
    break;
  case SYNCED:
    if (gpu_resize()) {
      head_ = HEAD_AT_GPU;
    }
    break;
  }
}

const void* SyncedMemory::cpu_data() {
  to_cpu();
  return static_cast<const void*>(cpu_data_);
}

void SyncedMemory::set_cpu_data(void* data) {
  CHECK(data);
  if (own_cpu_data_) {
    CaffeFreeHost(cpu_ptr_);
  }
  cpu_ptr_ = data;
  head_ = HEAD_AT_CPU;
  own_cpu_data_ = false;
}

const void* SyncedMemory::gpu_data() {
  to_gpu();
  return static_cast<const void*>(gpu_data_);
}

void* SyncedMemory::mutable_cpu_data() {
  to_cpu();
  head_ = HEAD_AT_CPU;
  return cpu_data_;
}

void* SyncedMemory::mutable_gpu_data() {
  to_gpu();
  head_ = HEAD_AT_GPU;
  return gpu_data_;
}

bool SyncedMemory::cpu_resize() {
  if (!cpu_data_) {
    CaffeMallocHost(&cpu_data_, size_);
    memset(cpu_data_, 0, size_);
    cpu_capacity_ = size_;
  } else if (size_ > cpu_capacity_) {
    CaffeReallocHost(&cpu_data_, size_);
    const size_t num_new_bytes = size_ - cpu_capacity_;
    memset(static_cast<uint8_t*>(cpu_data_) + cpu_capacity_, 0, num_new_bytes);
    cpu_capacity_ = size_;
  } else {
    return false;
  }
  return true;
}

bool SyncedMemory::gpu_resize() {
  if (!gpu_data_) {
    CUDA_CHECK(cudaMalloc(&gpu_data_, size_));
    CUDA_CHECK(cudaMemset(gpu_data_, 0, size_));
    gpu_capacity_ = size_;
  } else if (size_ > gpu_capacity_) {
    void* new_gpu_data_;
    CUDA_CHECK(cudaMalloc(&new_gpu_data_, size_));
    CUDA_CHECK(cudaMemcpy(new_gpu_data_, gpu_data_, gpu_capacity_,
                          cudaMemcpyDeviceToDevice));
    CUDA_CHECK(cudaFree(gpu_data_));
    gpu_data_ = new_gpu_data_;
    const size_t num_new_bytes = size_ - gpu_capacity_;
    CUDA_CHECK(cudaMemset(
        static_cast<uint8_t*>(gpu_data_) + gpu_capacity_, 0, num_new_bytes));
    gpu_capacity_ = size_;
  } else {
    return false;
  }
  return true;
}

}  // namespace caffe
