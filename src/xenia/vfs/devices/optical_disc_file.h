/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_OPTICAL_DISC_FILE_H_
#define XENIA_VFS_DEVICES_OPTICAL_DISC_FILE_H_

#include "xenia/vfs/file.h"

namespace xe {
namespace vfs {

class OpticalDiscEntry;

class OpticalDiscFile : public File {
 public:
  OpticalDiscFile(uint32_t file_access, OpticalDiscEntry* entry);
  ~OpticalDiscFile() override;

  void Destroy() override;

  X_STATUS ReadSync(std::span<uint8_t> buffer, size_t byte_offset,
                    size_t* out_bytes_read) override;
  X_STATUS WriteSync(std::span<const uint8_t> buffer, size_t byte_offset,
                     size_t* out_bytes_written) override {
    return X_STATUS_ACCESS_DENIED;
  }
  X_STATUS SetLength(size_t length) override { return X_STATUS_ACCESS_DENIED; }

 private:
  OpticalDiscEntry* entry_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_OPTICAL_DISC_FILE_H_
