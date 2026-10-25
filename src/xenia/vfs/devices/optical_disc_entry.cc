/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/optical_disc_entry.h"

#include "xenia/base/string.h"
#include "xenia/vfs/devices/optical_disc_file.h"

namespace xe {
namespace vfs {

OpticalDiscEntry::OpticalDiscEntry(Device* device, Entry* parent,
                                   const std::string_view path)
    : Entry(device, parent, path) {}

OpticalDiscEntry::~OpticalDiscEntry() = default;

std::unique_ptr<OpticalDiscEntry> OpticalDiscEntry::Create(
    Device* device, Entry* parent, const std::string_view name) {
  auto path = xe::utf8::join_guest_paths(parent->path(), name);
  return std::make_unique<OpticalDiscEntry>(device, parent, path);
}

X_STATUS OpticalDiscEntry::Open(uint32_t desired_access, File** out_file) {
  *out_file = new OpticalDiscFile(desired_access, this);
  return X_STATUS_SUCCESS;
}

bool OpticalDiscEntry::DeleteEntryInternal(Entry* entry) { return false; }

}  // namespace vfs
}  // namespace xe
