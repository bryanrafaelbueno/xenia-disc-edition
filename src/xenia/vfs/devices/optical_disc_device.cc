/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary contributors                                    *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/vfs/devices/optical_disc_device.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/literals.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/platform.h"
#include "xenia/base/string.h"
#include "xenia/vfs/devices/optical_disc_entry.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"

#include <winioctl.h>
#include <ntddcdrm.h>
#include <ntddscsi.h>
#endif  // XE_PLATFORM_WIN32

namespace xe {
namespace vfs {

using namespace xe::literals;

namespace {

constexpr size_t kXESectorSize = 2_KiB;
constexpr uint32_t kCacheBlockSectors = 32;
constexpr size_t kCacheBlockCount = 64;
constexpr uint32_t kScsiSenseSize = 32;

// The first entry is the game partition of a trimmed image. The last entry is
// the game partition of a full Xbox 360 disc, including the video partition.
// The other offsets are used by some image layouts.
constexpr uint64_t kLikelyGameOffsets[] = {
    0x00000000, 0x0000FB20, 0x00020600, 0x02080000, kXgdGamePartitionOffset,
};

#if XE_PLATFORM_WIN32
bool ParseDriveLetter(const std::filesystem::path& path, wchar_t* out_letter,
                      bool* out_explicit_device) {
  std::wstring value = path.wstring();
  if (value.empty()) {
    return false;
  }

  // A drive root written as "D:\" is equivalent to the raw device path
  // "\\.\D:". Remove the trailing separators before looking for the drive
  // letter. This intentionally accepts only the root, not arbitrary files on
  // an optical drive.
  while (value.size() > 1 &&
         (value.back() == L'\\' || value.back() == L'/')) {
    value.pop_back();
  }

  bool explicit_device = false;
  if (value.size() >= 4 && value.compare(0, 4, L"\\\\.\\") == 0) {
    value = value.substr(4);
    explicit_device = true;
  } else if (value.size() >= 4 && value.compare(0, 4, L"\\\\?\\") == 0) {
    value = value.substr(4);
    explicit_device = true;
  }

  if (value.size() != 2 || value[1] != L':' || !iswalpha(value[0])) {
    return false;
  }

  *out_letter = static_cast<wchar_t>(towupper(value[0]));
  *out_explicit_device = explicit_device;
  return true;
}
#endif  // XE_PLATFORM_WIN32

#if XE_PLATFORM_WIN32
std::string Win32ErrorMessage(DWORD error_code) {
  return std::system_category().message(static_cast<int>(error_code));
}
#endif  // XE_PLATFORM_WIN32

}  // namespace

// Reads raw 2048-byte sectors from a Windows optical drive. The drive is
// addressed as a SCSI device so that the data can be read outside the
// filesystem the drive advertises for the DVD-Video partition.
class OpticalDiscReader {
 public:
  OpticalDiscReader() = default;
  ~OpticalDiscReader();

  bool Open(const std::filesystem::path& path, std::string* error);
  bool Read(uint64_t byte_offset, void* buffer, size_t length,
            std::string* error);

  uint64_t reported_size() const { return reported_size_; }
  uint32_t sector_size() const { return sector_size_; }
  const std::string& device_path() const { return device_path_; }

 private:
  bool ReadSectors(uint64_t lba, uint32_t count, void* buffer,
                   std::string* error);
  bool GetCachedBlock(uint64_t block_index, const uint8_t** out_data,
                      uint32_t* out_size, std::string* error);

  struct CacheBlock {
    uint64_t block_index = UINT64_MAX;
    uint32_t sector_count = 0;
    uint64_t last_use = 0;
    std::vector<uint8_t> data;
  };

#if XE_PLATFORM_WIN32
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#endif  // XE_PLATFORM_WIN32
  std::string device_path_;
  uint32_t sector_size_ = 2048;
  uint64_t reported_size_ = 0;
  std::array<CacheBlock, kCacheBlockCount> cache_;
  uint64_t cache_clock_ = 0;
  std::mutex cache_mutex_;
};

OpticalDiscReader::~OpticalDiscReader() {
#if XE_PLATFORM_WIN32
  if (handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
  }
#endif  // XE_PLATFORM_WIN32
}

bool OpticalDiscReader::Open(const std::filesystem::path& path,
                             std::string* error) {
#if !XE_PLATFORM_WIN32
  *error = "Optical disc drives are only supported on Windows.";
  return false;
#else
  wchar_t letter = 0;
  bool explicit_device = false;
  if (!ParseDriveLetter(path, &letter, &explicit_device)) {
    *error = fmt::format(
        "Invalid optical drive path '{}'. Use a drive letter such as D:.",
        xe::path_to_utf8(path));
    return false;
  }
  (void)explicit_device;

  device_path_ = fmt::format("\\\\.\\{}:", static_cast<char>(letter));
  std::wstring wide_device_path(device_path_.begin(), device_path_.end());

  // SCSI pass-through requires write access to the device object, even for
  // read-only commands. This does not grant permission to write to the disc.
  handle_ = CreateFileW(wide_device_path.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_EXISTING, 0, nullptr);
  if (handle_ == INVALID_HANDLE_VALUE) {
    const DWORD last_error = GetLastError();
    *error = fmt::format("Failed to open optical drive {} ({}).", device_path_,
                         Win32ErrorMessage(last_error));
    return false;
  }

  DISK_GEOMETRY geometry = {};
  DWORD bytes_returned = 0;
  if (!DeviceIoControl(handle_, IOCTL_CDROM_GET_DRIVE_GEOMETRY, nullptr, 0,
                       &geometry, sizeof(geometry), &bytes_returned, nullptr) &&
      !DeviceIoControl(handle_, IOCTL_DISK_GET_DRIVE_GEOMETRY, nullptr, 0,
                       &geometry, sizeof(geometry), &bytes_returned, nullptr)) {
    const DWORD last_error = GetLastError();
    *error = fmt::format(
        "Failed to query the geometry of optical drive {} ({}).",
        device_path_, Win32ErrorMessage(last_error));
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return false;
  }

  sector_size_ = geometry.BytesPerSector ? geometry.BytesPerSector : 2048;
  if (sector_size_ != 2048) {
    *error = fmt::format(
        "Optical drive {} reports {}-byte sectors; an Xbox 360 disc must be "
        "read as 2048-byte sectors.",
        device_path_, sector_size_);
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return false;
  }

  GET_LENGTH_INFORMATION length_information = {};
  if (DeviceIoControl(handle_, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0,
                      &length_information, sizeof(length_information),
                      &bytes_returned, nullptr)) {
    reported_size_ =
        static_cast<uint64_t>(length_information.Length.QuadPart);
  }

  // Probe the disc before reporting success. This catches an empty drive as
  // well as drives that can not issue raw reads.
  std::vector<uint8_t> probe(sector_size_);
  if (!ReadSectors(0, 1, probe.data(), error)) {
    if (error->empty()) {
      *error = fmt::format("The optical drive {} is not ready.", device_path_);
    } else {
      *error = fmt::format("Could not read from optical drive {}: {}",
                           device_path_, *error);
    }
    CloseHandle(handle_);
    handle_ = INVALID_HANDLE_VALUE;
    return false;
  }

  XELOGI("Opened optical drive {} ({} byte sectors, reported media size {}).",
         device_path_, sector_size_, reported_size_);
  return true;
#endif  // XE_PLATFORM_WIN32
}

bool OpticalDiscReader::ReadSectors(uint64_t lba, uint32_t count, void* buffer,
                                    std::string* error) {
#if !XE_PLATFORM_WIN32
  *error = "Optical disc drives are only supported on Windows.";
  return false;
#else
  if (!count) {
    return true;
  }
  if (lba > UINT32_MAX) {
    *error = "Logical block address is outside the range supported by SCSI.";
    return false;
  }

  const size_t data_size = size_t(count) * sector_size_;
  const size_t header_size = sizeof(SCSI_PASS_THROUGH_DIRECT);
  const size_t sense_size = kScsiSenseSize;
  std::vector<uint8_t> header_buffer(header_size + sense_size, 0);

  // SCSI_PASS_THROUGH_DIRECT transfers into a separately supplied buffer.
  // The storage stack requires this buffer to be sector aligned. Over-allocate
  // and align manually so no platform-specific allocator is needed.
  std::vector<uint8_t> data_storage(data_size + 4096);
  const uintptr_t storage_address =
      reinterpret_cast<uintptr_t>(data_storage.data());
  auto* data_buffer = reinterpret_cast<uint8_t*>(
      (storage_address + 4095) & ~uintptr_t(4095));

  auto* spt =
      reinterpret_cast<SCSI_PASS_THROUGH_DIRECT*>(header_buffer.data());
  spt->Length = static_cast<USHORT>(sizeof(SCSI_PASS_THROUGH_DIRECT));
  spt->CdbLength = 12;
  spt->SenseInfoLength = static_cast<UCHAR>(sense_size);
  spt->DataIn = SCSI_IOCTL_DATA_IN;
  spt->DataTransferLength = static_cast<ULONG>(data_size);
  spt->TimeOutValue = 60;
  spt->DataBuffer = data_buffer;
  spt->SenseInfoOffset = static_cast<ULONG>(header_size);

  // SCSI READ(12).
  uint8_t* cdb = spt->Cdb;
  cdb[0] = 0xA8;
  cdb[1] = 0;
  cdb[2] = static_cast<uint8_t>((lba >> 24) & 0xFF);
  cdb[3] = static_cast<uint8_t>((lba >> 16) & 0xFF);
  cdb[4] = static_cast<uint8_t>((lba >> 8) & 0xFF);
  cdb[5] = static_cast<uint8_t>(lba & 0xFF);
  cdb[6] = static_cast<uint8_t>((count >> 24) & 0xFF);
  cdb[7] = static_cast<uint8_t>((count >> 16) & 0xFF);
  cdb[8] = static_cast<uint8_t>((count >> 8) & 0xFF);
  cdb[9] = static_cast<uint8_t>(count & 0xFF);
  cdb[10] = 0;
  cdb[11] = 0;

  DWORD bytes_returned = 0;
  if (!DeviceIoControl(handle_, IOCTL_SCSI_PASS_THROUGH_DIRECT,
                       header_buffer.data(),
                       static_cast<DWORD>(header_buffer.size()),
                       header_buffer.data(),
                       static_cast<DWORD>(header_buffer.size()),
                       &bytes_returned, nullptr)) {
    const DWORD last_error = GetLastError();
    *error = fmt::format("SCSI read failed at LBA {} ({}).", lba,
                         Win32ErrorMessage(last_error));
    return false;
  }

  if (spt->ScsiStatus != 0x00) {
    const uint8_t* sense =
        header_buffer.data() + static_cast<size_t>(spt->SenseInfoOffset);
    const uint8_t sense_key = sense[2] & 0x0F;
    const uint8_t asc = sense[12];
    const uint8_t ascq = sense[13];
    *error = fmt::format(
        "SCSI read failed at LBA {} (status 0x{:02X}, sense 0x{:02X}/0x{:02X}/"
        "0x{:02X}).",
        lba, spt->ScsiStatus, sense_key, asc, ascq);
    return false;
  }

  std::memcpy(buffer, data_buffer, data_size);
  return true;
#endif  // XE_PLATFORM_WIN32
}

bool OpticalDiscReader::GetCachedBlock(uint64_t block_index,
                                       const uint8_t** out_data,
                                       uint32_t* out_size,
                                       std::string* error) {
  const uint64_t cache_block_bytes =
      uint64_t(kCacheBlockSectors) * sector_size_;
  const uint64_t block_start_byte = block_index * cache_block_bytes;
  if (block_start_byte >= kOpticalDiscMaxSize) {
    *error = "Read is past the supported optical disc size.";
    return false;
  }

  CacheBlock* target = nullptr;
  for (auto& block : cache_) {
    if (block.block_index == block_index) {
      block.last_use = ++cache_clock_;
      *out_data = block.data.data();
      *out_size = static_cast<uint32_t>(block.data.size());
      return true;
    }
  }

  for (auto& block : cache_) {
    if (block.block_index == UINT64_MAX) {
      target = &block;
      break;
    }
    if (!target || block.last_use < target->last_use) {
      target = &block;
    }
  }
  if (!target) {
    *error = "Optical disc cache is unexpectedly empty.";
    return false;
  }

  const uint64_t block_start_sector = block_index * kCacheBlockSectors;
  const uint64_t max_sectors =
      (kOpticalDiscMaxSize + sector_size_ - 1) / sector_size_;
  if (block_start_sector >= max_sectors) {
    *error = "Read is past the supported optical disc size.";
    return false;
  }

  const uint32_t sector_count = static_cast<uint32_t>(
      std::min<uint64_t>(kCacheBlockSectors,
                         max_sectors - block_start_sector));
  std::vector<uint8_t> data(size_t(sector_count) * sector_size_);
  if (!ReadSectors(block_start_sector, sector_count, data.data(), error)) {
    return false;
  }

  target->block_index = block_index;
  target->sector_count = sector_count;
  target->last_use = ++cache_clock_;
  target->data = std::move(data);
  *out_data = target->data.data();
  *out_size = static_cast<uint32_t>(target->data.size());
  return true;
}

bool OpticalDiscReader::Read(uint64_t byte_offset, void* buffer,
                             size_t length, std::string* error) {
  if (!length) {
    return true;
  }
  if (byte_offset > kOpticalDiscMaxSize ||
      length > kOpticalDiscMaxSize - byte_offset) {
    *error = "Read is past the supported optical disc size.";
    return false;
  }

  std::lock_guard<std::mutex> lock(cache_mutex_);

  const uint64_t cache_block_bytes =
      uint64_t(kCacheBlockSectors) * sector_size_;
  uint64_t position = byte_offset;
  size_t remaining = length;
  uint8_t* output = reinterpret_cast<uint8_t*>(buffer);

  while (remaining) {
    const uint64_t block_index = position / cache_block_bytes;
    const uint32_t block_offset =
        static_cast<uint32_t>(position % cache_block_bytes);
    const uint8_t* block_data = nullptr;
    uint32_t block_size = 0;
    if (!GetCachedBlock(block_index, &block_data, &block_size, error)) {
      return false;
    }
    if (block_offset >= block_size) {
      *error = "Optical disc read beyond the end of the disc.";
      return false;
    }

    const size_t copy_size =
        std::min(remaining, size_t(block_size - block_offset));
    std::memcpy(output, block_data + block_offset, copy_size);
    output += copy_size;
    position += copy_size;
    remaining -= copy_size;
  }

  return true;
}

OpticalDiscDevice::OpticalDiscDevice(const std::string_view mount_path,
                                     const std::filesystem::path& host_path)
    : Device(mount_path), name_("GDFX"), host_path_(host_path) {}

OpticalDiscDevice::~OpticalDiscDevice() = default;

bool OpticalDiscDevice::IsDrivePath(const std::filesystem::path& path) {
#if !XE_PLATFORM_WIN32
  (void)path;
  return false;
#else
  wchar_t letter = 0;
  bool explicit_device = false;
  if (!ParseDriveLetter(path, &letter, &explicit_device)) {
    return false;
  }
  if (explicit_device) {
    return true;
  }
  const wchar_t root[] = {letter, L':', L'\\', 0};
  return GetDriveTypeW(root) == DRIVE_CDROM;
#endif  // XE_PLATFORM_WIN32
}

std::vector<std::string> OpticalDiscDevice::GetAvailableDrivePaths() {
  std::vector<std::string> drives;
#if XE_PLATFORM_WIN32
  for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
    const wchar_t root[] = {letter, L':', L'\\', 0};
    if (GetDriveTypeW(root) == DRIVE_CDROM) {
      drives.push_back(fmt::format("{}:", static_cast<char>(letter)));
    }
  }
#endif  // XE_PLATFORM_WIN32
  return drives;
}

std::string OpticalDiscDevice::GetVolumeIdentity(
    const std::filesystem::path& path) {
#if !XE_PLATFORM_WIN32
  (void)path;
  return {};
#else
  wchar_t letter = 0;
  bool explicit_device = false;
  if (!ParseDriveLetter(path, &letter, &explicit_device)) {
    return {};
  }
  const wchar_t root[] = {letter, L':', L'\\', 0};

  wchar_t label[256] = {};
  DWORD serial_number = 0;
  DWORD max_component_length = 0;
  DWORD file_system_flags = 0;
  if (!GetVolumeInformationW(root, label, xe::countof(label), &serial_number,
                             &max_component_length, &file_system_flags, nullptr,
                             0)) {
    return {};
  }

  ULARGE_INTEGER total_size = {};
  GetDiskFreeSpaceExW(root, nullptr, &total_size, nullptr);

  return fmt::format("{}|{}", serial_number, total_size.QuadPart);
#endif  // XE_PLATFORM_WIN32
}

bool OpticalDiscDevice::IsXbox360Disc(const std::filesystem::path& path) {
  OpticalDiscReader reader;
  std::string error;
  if (!reader.Open(path, &error)) {
    return false;
  }

  uint8_t volume_descriptor[kXESectorSize];
  for (uint64_t game_offset : kLikelyGameOffsets) {
    if (!reader.Read(game_offset + (32 * kXESectorSize), volume_descriptor,
                     sizeof(volume_descriptor), &error)) {
      continue;
    }
    if (std::memcmp(volume_descriptor, "MICROSOFT*XBOX*MEDIA", 20) == 0) {
      return true;
    }
  }
  return false;
}

bool OpticalDiscDevice::Initialize() {
  reader_ = std::make_unique<OpticalDiscReader>();

  std::string error;
  if (!reader_->Open(host_path_, &error)) {
    last_error_ = std::move(error);
    XELOGE("Failed to open optical disc '{}': {}", xe::path_to_utf8(host_path_),
           last_error_);
    return false;
  }

  media_size_ = kOpticalDiscMaxSize;
  ParseState state = {};
  state.size = media_size_;
  if (!Verify(&state)) {
    last_error_ =
        "No Xbox 360 game partition was found on the disc. The drive may only "
        "be able to read the DVD-Video partition, or the inserted disc is not "
        "an Xbox 360 game.";
    XELOGE("Failed to find an Xbox 360 game partition on '{}': {}",
           xe::path_to_utf8(host_path_), last_error_);
    return false;
  }

  game_offset_ = state.game_offset;
  if (!ReadAllEntries(&state)) {
    last_error_ =
        "The Xbox 360 game partition was found, but its filesystem could not "
        "be read. The disc may be damaged.";
    XELOGE("Failed to read the XDVDFS filesystem from '{}': {}",
           xe::path_to_utf8(host_path_), last_error_);
    return false;
  }

  XELOGI(
      "Mounted optical disc from {}: XDVDFS at 0x{:X}, root sector {}, root "
      "size {} bytes.",
      reader_->device_path(), game_offset_, state.root_sector,
      state.root_size);
  return true;
}

void OpticalDiscDevice::Dump(StringBuffer* string_buffer) {
  auto global_lock = global_critical_region_.Acquire();
  root_entry_->Dump(string_buffer, 0);
}

Entry* OpticalDiscDevice::ResolvePath(const std::string_view path) {
  XELOGFS("OpticalDiscDevice::ResolvePath({})", path);
  return root_entry_->ResolvePath(path);
}

bool OpticalDiscDevice::Read(uint64_t byte_offset, void* buffer,
                             size_t length) {
  if (!reader_) {
    return false;
  }

  std::string error;
  if (!reader_->Read(byte_offset, buffer, length, &error)) {
    last_error_ = std::move(error);
    XELOGE("Optical disc read failed at byte 0x{:X} ({} bytes): {}",
           byte_offset, length, last_error_);
    return false;
  }
  return true;
}

bool OpticalDiscDevice::VerifyMagic(ParseState* state, uint64_t offset) {
  if (offset > state->size || kXESectorSize > state->size - offset) {
    return false;
  }

  uint8_t sector[kXESectorSize];
  if (!Read(offset, sector, sizeof(sector))) {
    return false;
  }

  return std::memcmp(sector, "MICROSOFT*XBOX*MEDIA", 20) == 0;
}

bool OpticalDiscDevice::Verify(ParseState* state) {
  bool read_any_partition = false;
  std::string last_read_error;

  for (uint64_t game_offset : kLikelyGameOffsets) {
    state->game_offset = game_offset;

    const uint64_t volume_descriptor_offset =
        game_offset + (32 * kXESectorSize);
    if (volume_descriptor_offset > state->size ||
        kXESectorSize > state->size - volume_descriptor_offset) {
      continue;
    }

    uint8_t sector[kXESectorSize];
    if (!Read(volume_descriptor_offset, sector, sizeof(sector))) {
      last_read_error = last_error_;
      continue;
    }
    read_any_partition = true;

    if (std::memcmp(sector, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
      continue;
    }

    state->root_sector = xe::load<uint32_t>(sector + 20);
    state->root_size = xe::load<uint32_t>(sector + 24);
    state->root_offset =
        game_offset + (state->root_sector * kXESectorSize);

    if (state->root_size < 13 || state->root_size > 32_MiB) {
      XELOGE("XDVDFS root directory has an invalid size: {}",
             state->root_size);
      continue;
    }
    if (state->root_offset > state->size ||
        state->root_size > state->size - state->root_offset) {
      XELOGE("XDVDFS root directory is outside the disc image.");
      continue;
    }

    XELOGI("Found XDVDFS at disc offset 0x{:X}.", game_offset);
    return true;
  }

  if (!read_any_partition && !last_read_error.empty()) {
    XELOGE(
        "Could not read any of the known Xbox 360 game partition offsets. The "
        "drive may not support reading past the DVD-Video lead-out (a "
        "Kreon/0800-compatible drive is required). Last error: {}",
        last_read_error);
  } else {
    XELOGE(
        "The disc does not contain the Xbox 360 XDVDFS signature at any known "
        "partition offset.");
  }
  return false;
}

bool OpticalDiscDevice::ReadAllEntries(ParseState* state) {
  std::vector<uint8_t> root_buffer(static_cast<size_t>(state->root_size));
  if (!Read(state->root_offset, root_buffer.data(), root_buffer.size())) {
    return false;
  }

  auto root_entry =
      std::make_unique<OpticalDiscEntry>(this, nullptr, std::string_view());
  root_entry->attributes_ = kFileAttributeDirectory;
  root_entry->size_ = 0;
  root_entry->allocation_size_ = 0;
  root_entry->create_timestamp_ = 10000 * 11644473600000LL;
  root_entry->access_timestamp_ = 10000 * 11644473600000LL;
  root_entry->write_timestamp_ = 10000 * 11644473600000LL;
  root_entry_ = std::move(root_entry);

  if (!ReadEntry(state, root_buffer.data(), root_buffer.size(), 0,
                 static_cast<OpticalDiscEntry*>(root_entry_.get()), 0)) {
    return false;
  }
  return true;
}

bool OpticalDiscDevice::ReadEntry(ParseState* state, const uint8_t* buffer,
                                  size_t buffer_size, uint16_t entry_ordinal,
                                  OpticalDiscEntry* parent, uint32_t depth) {
  if (depth > 4096) {
    XELOGE("XDVDFS directory tree is too deep.");
    return false;
  }

  const size_t entry_offset = size_t(entry_ordinal) * 4;
  if (entry_offset > buffer_size || (buffer_size - entry_offset) < 14) {
    XELOGE("XDVDFS directory entry is outside its directory buffer.");
    return false;
  }

  const uint8_t* p = buffer + entry_offset;
  const uint16_t node_l = xe::load<uint16_t>(p + 0);
  const uint16_t node_r = xe::load<uint16_t>(p + 2);
  const uint32_t sector = xe::load<uint32_t>(p + 4);
  const uint32_t length = xe::load<uint32_t>(p + 8);
  const uint8_t attributes = xe::load<uint8_t>(p + 12);
  const uint8_t name_length = xe::load<uint8_t>(p + 13);

  if (size_t(name_length) > buffer_size - entry_offset - 14) {
    XELOGE("XDVDFS file name is outside its directory buffer.");
    return false;
  }

  if (node_l && !ReadEntry(state, buffer, buffer_size, node_l, parent, depth + 1)) {
    return false;
  }

  // Filename is stored as Windows-1252, convert it to UTF-8.
  auto ansi_name = std::string(reinterpret_cast<const char*>(p + 14), name_length);
  auto name = xe::win1252_to_utf8(ansi_name);
  if (name.empty()) {
    name = ansi_name;
  }

  auto entry = OpticalDiscEntry::Create(this, parent, name);
  entry->attributes_ = attributes | kFileAttributeReadOnly;
  entry->size_ = length;
  entry->allocation_size_ = (length + bytes_per_sector() - 1) &
                            ~(uint64_t(bytes_per_sector()) - 1);
  entry->create_timestamp_ = 10000 * 11644473600000LL;
  entry->access_timestamp_ = 10000 * 11644473600000LL;
  entry->write_timestamp_ = 10000 * 11644473600000LL;

  const uint64_t entry_offset_bytes =
      state->game_offset + uint64_t(sector) * kXESectorSize;
  if (entry_offset_bytes > state->size || length > state->size - entry_offset_bytes) {
    XELOGE("XDVDFS entry '{}' is outside the disc image.", name);
    return false;
  }

  if (attributes & kFileAttributeDirectory) {
    entry->data_offset_ = 0;
    entry->data_size_ = 0;
    if (length) {
      if (length > 32_MiB) {
        XELOGE("XDVDFS directory '{}' is too large ({} bytes).", name, length);
        return false;
      }
      std::vector<uint8_t> directory_buffer(length);
      if (!Read(entry_offset_bytes, directory_buffer.data(), length)) {
        return false;
      }
      if (!ReadEntry(state, directory_buffer.data(), directory_buffer.size(), 0,
                     entry.get(), depth + 1)) {
        return false;
      }
    }
  } else {
    entry->data_offset_ = static_cast<size_t>(entry_offset_bytes);
    entry->data_size_ = length;
  }

  parent->children_.emplace_back(std::move(entry));

  if (node_r && !ReadEntry(state, buffer, buffer_size, node_r, parent, depth + 1)) {
    return false;
  }

  return true;
}

}  // namespace vfs
}  // namespace xe
