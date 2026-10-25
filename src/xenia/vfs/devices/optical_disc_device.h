/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_VFS_DEVICES_OPTICAL_DISC_DEVICE_H_
#define XENIA_VFS_DEVICES_OPTICAL_DISC_DEVICE_H_

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "xenia/vfs/device.h"

namespace xe {
namespace vfs {

class OpticalDiscEntry;
class OpticalDiscReader;

// Xbox 360 discs store their game partition at LBA 0x1FB20. The first
// partition is a normal DVD-Video partition, so this offset is also where the
// game partition begins in a full-disc image.
constexpr uint64_t kOpticalDiscSectorSize = 2048;
constexpr uint64_t kXgdGamePartitionOffset = 0x0FD90000;
// XGD3 discs are slightly larger than the 8.5 GB DVD limit. This is only used
// as an upper bound while probing; individual files still have to be readable.
constexpr uint64_t kOpticalDiscMaxSize = uint64_t(9) * 1024 * 1024 * 1024;

// A VFS device for a physical Xbox 360 disc in an optical drive.
//
// The disc is read on demand using raw 2048-byte sector requests, so no
// multi-gigabyte temporary image is created. Reading the game partition
// requires a drive that can access data beyond the fake DVD-Video lead-out
// (for example a Kreon drive, an Xbox 360 drive in 0800 mode, or another drive
// with equivalent support). A stock PC DVD drive can only read the video
// partition and will fail to find the game filesystem.
class OpticalDiscDevice : public Device {
 public:
  OpticalDiscDevice(const std::string_view mount_path,
                    const std::filesystem::path& host_path);
  ~OpticalDiscDevice() override;

  bool Initialize() override;
  void Dump(StringBuffer* string_buffer) override;
  Entry* ResolvePath(const std::string_view path) override;

  const std::string& name() const override { return name_; }
  uint32_t attributes() const override { return 0; }
  uint32_t component_name_max_length() const override { return 255; }

  uint32_t total_allocation_units() const override {
    return uint32_t(media_size_ / bytes_per_sector());
  }
  uint32_t available_allocation_units() const override { return 0; }
  uint32_t sectors_per_allocation_unit() const override { return 1; }
  uint32_t bytes_per_sector() const override { return 0x200; }

  // Reads a byte range from the disc. The range doesn't need to be sector
  // aligned; the reader takes care of the necessary sector alignment.
  bool Read(uint64_t byte_offset, void* buffer, size_t length);

  const std::filesystem::path& host_path() const { return host_path_; }
  size_t game_offset() const { return game_offset_; }
  uint64_t media_size() const { return media_size_; }
  const std::string& last_error() const { return last_error_; }

  // Returns true if the path names an optical drive (for example "D:",
  // "D:\", or "\\.\D:"). Bare drive letters are accepted only if Windows
  // reports the drive as an optical drive; an explicit \\.\ device path is
  // accepted regardless so that unusual drives can still be addressed.
  static bool IsDrivePath(const std::filesystem::path& path);

  // Returns the drive letters of all optical drives, as strings such as "D:".
  static std::vector<std::string> GetAvailableDrivePaths();

  // Returns a cheap identity for the currently inserted media (volume serial
  // number and size), or an empty string if the drive is empty.
  static std::string GetVolumeIdentity(const std::filesystem::path& path);

  // Checks whether the inserted media contains an Xbox 360 game partition at
  // one of the known offsets. This performs a few raw sector reads and is
  // intended to be called only when the media identity changes.
  static bool IsXbox360Disc(const std::filesystem::path& path);

 private:
  enum class Error {
    kSuccess = 0,
    kErrorOutOfMemory = -1,
    kErrorReadError = -10,
    kErrorFileMismatch = -30,
    kErrorDamagedFile = -31,
  };

  struct ParseState {
    uint64_t size;
    uint64_t game_offset;
    uint64_t root_sector;
    uint64_t root_offset;
    uint64_t root_size;
  };

  bool Verify(ParseState* state);
  bool VerifyMagic(ParseState* state, uint64_t offset);
  bool ReadAllEntries(ParseState* state);
  bool ReadEntry(ParseState* state, const uint8_t* buffer, size_t buffer_size,
                 uint16_t entry_ordinal, OpticalDiscEntry* parent,
                 uint32_t depth);

  std::string name_;
  std::filesystem::path host_path_;
  std::unique_ptr<OpticalDiscReader> reader_;
  std::unique_ptr<Entry> root_entry_;
  size_t game_offset_ = 0;
  uint64_t media_size_ = 0;
  std::string last_error_;
};

}  // namespace vfs
}  // namespace xe

#endif  // XENIA_VFS_DEVICES_OPTICAL_DISC_DEVICE_H_
