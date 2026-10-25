/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_OPTICAL_DISC_ENTRY_H_
#define XENIA_VFS_DEVICES_OPTICAL_DISC_ENTRY_H_

#include <cstddef>
#include <memory>
#include <string>

#include "xenia/vfs/entry.h"

namespace xe {
namespace vfs {

class OpticalDiscDevice;

class OpticalDiscEntry : public Entry {
 public:
  OpticalDiscEntry(Device* device, Entry* parent, const std::string_view path);
  ~OpticalDiscEntry() override;

  static std::unique_ptr<OpticalDiscEntry> Create(Device* device, Entry* parent,
                                                  const std::string_view name);

  size_t data_offset() const { return data_offset_; }
  size_t data_size() const { return data_size_; }

  X_STATUS Open(uint32_t desired_access, File** out_file) override;

  bool can_map() const override { return false; }

 private:
  friend class OpticalDiscDevice;

  bool DeleteEntryInternal(Entry* entry) override;

  size_t data_offset_ = 0;
  size_t data_size_ = 0;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_OPTICAL_DISC_ENTRY_H_
