# Direct optical disc support

This document describes the physical-disc backend added in Xenia Disc Edition.

## Overview

Xenia's VFS layer normally mounts a host file (ISO, ZAR, STFS, or XEX) and
presents guest paths such as `GAME:\default.xex`. The optical backend adds a
new `vfs::Device` implementation that presents the same XDVDFS tree while
reading sectors from a Windows optical drive.

The drive is **not** mounted as a Windows filesystem for game data. Windows
exposes only the disc's DVD-Video volume, and the game partition is outside its
advertised table of contents. Instead, the device issues SCSI `READ(12)`
commands directly to `\\.\X:`.

## Disc layout

An Xbox 360 game disc has two relevant areas:

| Area | LBA | Byte offset | Contents |
| --- | ---: | ---: | --- |
| DVD-Video partition | 0 | 0 | Normal DVD-Video data and the Xbox 360 logo video |
| Game partition | `0x1FB20` | `0xFD90000` | XDVDFS filesystem containing `default.xex` and game data |

The XDVDFS volume descriptor is at sector 32 of the game partition:
`0xFD90000 + 32 * 2048 = 0xFEA0000`. It starts with the 20-byte signature
`MICROSOFT*XBOX*MEDIA`. The root directory sector and size follow at offsets
20 and 24.

A trimmed ISO may start at sector 0 of the game partition, so the device also
probes the offsets used by upstream Xenia's `DiscImageDevice`:

```text
0x00000000
0x0000FB20
0x00020600
0x02080000
0x0FD90000
```

## Reading the drive

`OpticalDiscReader` opens `\\.\X:` with `GENERIC_READ | GENERIC_WRITE`.
Pass-through IOCTLs require write access to the device object even though the
commands used here are read-only.

Each request uses `SCSI_PASS_THROUGH_DIRECT` and a 12-byte `READ(12)` CDB:

```text
CDB[0] = 0xA8             READ(12)
CDB[2..5]  = LBA          big-endian
CDB[6..9]  = block count  big-endian, in 2048-byte logical blocks
```

The data buffer is aligned to 4096 bytes. The Windows storage stack requires
alignment for direct pass-through transfers. Sense data is placed directly
after the `SCSI_PASS_THROUGH_DIRECT` header and is decoded into a diagnostic
message if the command fails.

The implemented reader caches 32 sectors (64 KiB) per block, with 64 blocks
(4 MiB total), using a simple least-recently-used replacement policy. Reads that
cross block boundaries are split transparently. File reads can be at arbitrary
byte offsets; only the sector reads are aligned.

## XDVDFS parsing

`OpticalDiscDevice` mirrors the parsing rules used by
`DiscImageDevice`:

- The root directory is read into memory. XDVDFS directory entries are a
  binary tree; `node_l` and `node_r` are stored as 4-byte ordinal offsets.
- File and directory sector numbers are relative to the game partition, so
  the absolute byte offset is `game_offset + sector * 2048`.
- File names are Windows-1252 and are converted to UTF-8 with the same helper
  used by the ISO backend.
- Files are exposed as read-only entries. Because the image is not mapped,
  `can_map()` is false and file reads go through `ReadSync`.
- A 32 MiB cap on directory buffers and a 4096-entry recursion depth limit
  guard against malformed directory tables. The higher limit is needed for
  discs such as *GRID 2*, which uses the `0x2080000` layout and has deeper
  XDVDFS directory trees.

The device reports `GDFX` as its name and 2048-byte sectors, matching the disc
image device so guest code sees a normal optical disc.

## Launcher integration

- `Emulator::IsOpticalDrivePath` recognizes bare drive roots (`E:`, `E:\`) and
  explicit device paths (`\\.\E:`, `\\?\E:`).
- Bare drive letters are accepted only when Windows reports the drive type as
  `DRIVE_CDROM`; explicit device paths are accepted regardless.
- `Emulator::GetOpticalDrivePaths` enumerates optical drives for the
  **File → Open Disc Drive** submenu.
- `Emulator::CreateVfsDevice` special-cases optical paths before trying to
  inspect a file header.
- `Emulator::GetFileSignature` returns XISO for drive roots so the existing
  disc-image launch path is reused.
- `EmulatorWindow::RunTitle` avoids resolving a drive root relative to the
  current directory and shows a disc-specific error message when mounting
  fails.

## Native disc auto-swap

`EmulatorWindow` starts a watcher after a successful launch from an optical
drive when the `disc_auto_swap` config option is enabled (it is enabled by
default). The watcher polls a cheap media identity — volume serial number and
total size — without issuing raw disc reads during gameplay.

When the identity changes, the watcher verifies that the new media contains an
Xbox 360 game partition at one of the known offsets. If it does, Xenia starts a
new instance of itself with `--target=<drive> --wait_for_pid=<current pid>` and
requests the current window to close. The new instance waits for the old
process to exit before initializing, so only one Xenia instance uses the
configuration and cache at a time. This is a native restart path, not an
external script.

Set `disc_auto_swap = false` in `xenia-canary.config.toml` to disable the
behavior and use the normal **File → Open Disc Drive** flow.

## Errors and diagnostics

The following messages are written to `xenia.log`:

- `Opened optical drive ...` — the drive was opened and probed successfully.
- `Found XDVDFS at disc offset 0x...` — the game partition was located.
- `Mounted optical disc from ...` — the filesystem was parsed.
- `SCSI read failed at LBA ...` — the drive rejected a read. This is the
  expected result for a stock drive attempting to read past the DVD-Video
  lead-out.
- `Could not read any of the known Xbox 360 game partition offsets` — the
  drive could not read the game area, or the inserted disc is not an Xbox 360
  game.

If a read fails after the game has started, the file operation returns an
error and Xenia logs the failed byte offset. Keep the disc inserted and avoid
ejecting it during play.

## Verification

The first release was tested with *Call of Duty: Black Ops* on an ATAPI
`iHAS122 F` at `E:`. The log showed:

```text
Opened optical drive \\.\E: (2048 byte sectors, reported media size 7838695424).
Found XDVDFS at disc offset 0xFD90000.
Mounted optical disc from \\.\E:: XDVDFS at 0xFD90000, root sector 7631, root size 4096 bytes.
Loading module GAME:\default.xex
Title name: Call of Duty Black Ops
```

The game then read `images.pak`, `snd.all.pak`, and other files through
`OpticalDiscDevice` while the disc remained in the drive.
