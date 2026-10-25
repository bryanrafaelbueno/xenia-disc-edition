/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/optical_disc_file.h"

#include <algorithm>
#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/vfs/devices/optical_disc_device.h"
#include "xenia/vfs/devices/optical_disc_entry.h"

namespace xe {
namespace vfs {

OpticalDiscFile::OpticalDiscFile(uint32_t file_access, OpticalDiscEntry* entry)
    : File(file_access, entry), entry_(entry) {}

OpticalDiscFile::~OpticalDiscFile() = default;

void OpticalDiscFile::Destroy() { delete this; }

X_STATUS OpticalDiscFile::ReadSync(std::span<uint8_t> buffer,
                                   size_t byte_offset,
                                   size_t* out_bytes_read) {
  *out_bytes_read = 0;

  if (byte_offset >= entry_->data_size()) {
    return X_STATUS_END_OF_FILE;
  }

  const size_t read_length =
      std::min(buffer.size(), entry_->data_size() - byte_offset);
  if (!read_length) {
    return X_STATUS_SUCCESS;
  }

  auto device = static_cast<OpticalDiscDevice*>(entry_->device());
  if (!device->Read(entry_->data_offset() + byte_offset, buffer.data(),
                    read_length)) {
    XELOGE("Optical disc read failed for {} at offset {}", entry_->path(),
           byte_offset);
    return X_STATUS_UNSUCCESSFUL;
  }

  *out_bytes_read = read_length;
  return X_STATUS_SUCCESS;
}

}  // namespace vfs
}  // namespace xe
