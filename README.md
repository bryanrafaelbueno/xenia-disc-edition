<p align="center">
  <a href="https://github.com/xenia-canary/xenia-canary">
    <img height="200px" src="assets/icon/256.png" alt="Xenia Disc Edition" />
  </a>
</p>

<h1 align="center">Xenia Disc Edition</h1>

<p align="center">
  A fork of <a href="https://github.com/xenia-canary/xenia-canary">Xenia Canary</a>
  that can launch Xbox 360 games directly from a compatible optical disc drive.
</p>

## What is this?

Xenia Disc Edition is an experimental Xbox 360 emulator derived from Xenia
Canary. The original Xenia loads games from disc images or extracted folders.
This fork adds a physical-disc backend:

- It reads the Xbox 360 game partition directly from an optical drive.
- It does not require an ISO, GOD package, or a multi-gigabyte temporary dump.
- The disc is kept in the drive while the game runs.

The feature was developed and verified against a retail Xbox 360 disc on
Windows using an ATAPI `iHAS122 F` drive. See `RELEASE_NOTES.md` for the tested
title and build details.

> **This is not an official Xenia release.** It is an independent fork. The
> emulator core, graphics backends, kernel, and tooling come from Xenia Canary
> and its contributors. Please report issues with this fork here, not in the
> upstream issue tracker, unless the problem also reproduces with an upstream
> build.

## Features

- **Direct optical disc support** through a new `OpticalDiscDevice` VFS backend.
- Raw 2048-byte SCSI `READ(12)` sector reads, using
  `SCSI_PASS_THROUGH_DIRECT`.
- Detection of the Xbox 360 game partition at the full-disc XGD2 offset
  `0xFD90000` (LBA `0x1FB20`), the XGD3 offset `0x2080000`, and the offsets
  used by trimmed images.
- On-demand XDVDFS parsing with a 4 MiB sector cache, so game files are read
  from the disc as needed.
- Deep XDVDFS directory trees supported (tested with *GRID 2*, which uses the
  `0x2080000` layout).
- **File → Open Disc Drive → <letter>** menu in the emulator.
- Command-line drive targets: `E:`, `E:\`, or `\\.\E:`.
- **Native disc auto-swap** controlled by a Xenia config option, with no
  external launcher, batch, or PowerShell scripts.
- Existing ISO, XEX, STFS, ZAR, and folder launching still work.

## Requirements

- 64-bit Windows 10 or Windows 11.
- Visual Studio 2022 or Build Tools 2022 to build from source.
- A Vulkan or Direct3D 12 capable GPU for the emulator.
- **A drive that can read the game partition of Xbox 360 discs.**

Xbox 360 discs advertise only their DVD-Video partition in the normal DVD
table of contents. The game data starts at LBA `0x1FB20`, outside that
advertised range. A stock PC DVD drive can usually read the video partition but
will reject reads at the game partition with a SCSI error.

Drives with Xbox 360 read support include Kreon-compatible drives, Xbox 360
drives flashed in 0800 mode, and certain iHAS/BurnerMax-style drives. This fork
does not modify firmware, bypass drive protections, or decrypt disc content.

## Download

Release builds are attached to the
[GitHub Releases page](https://github.com/bryanrafaelbueno/xenia-disc-edition/releases).
The Windows release archive contains:

- `xenia_disc.exe`
- `README.md`
- `LICENSE`
- Verification logs for the release build
- `optical-disc-support.patch` (the changes against Xenia Canary)

No launcher scripts are required or included.

If you build from source, the executable is produced as
`build/bin/Windows/Release/xenia_disc.exe`.

## Usage

1. Insert an Xbox 360 game disc and let the drive spin up.
2. Start `xenia_disc.exe`.
3. Choose **File → Open Disc Drive → E:** (use your drive letter).

Or launch directly:

```bat
xenia_disc.exe --target=E:
```

The target can also be passed as a positional argument:

```bat
xenia_disc.exe E:
```

Keep the disc inserted while playing. The game reads files from the disc on
demand.

### Native disc auto-swap

Auto-swap is enabled by default for games launched from an optical drive. When
a different Xbox 360 game disc is inserted, Xenia detects the new media and
restarts itself with that game. This is implemented inside Xenia; no script is
involved.

The option is stored in `xenia-canary.config.toml`:

```toml
[General]
disc_auto_swap = true
```

Set it to `false` to use the normal manual behavior (**File → Open Disc Drive**)
without automatic restarts.

Auto-swap intentionally restarts Xenia because the emulator does not support
replacing a running title in place. Save the game before ejecting the disc. In
multi-disc games, inserting the next disc starts that disc as a new title; it
does not continue the campaign automatically.

## Building from source

```bat
git clone --recursive https://github.com/bryanrafaelbueno/xenia-disc-edition.git
cd xenia-disc-edition
xb setup
xb build --config=release
```

The Windows build requires the Vulkan SDK (for the SPIR-V shader tools) and a
Visual Studio installation with the C++ x64 workload. See
`docs/building.md` for the upstream build notes.

A small fix is included in `xenia-build.py` so Visual Studio discovery prefers
an installation that actually contains the MSVC x64 toolset. This is useful on
machines that have both Visual Studio Community and Build Tools installed.

## How it works

The drive is opened as `\\.\X:` and read with SCSI pass-through commands rather
than through the mounted Windows filesystem. Each request reads 2048-byte
sectors. The device probes the known game-partition offsets, verifies the
`MICROSOFT*XBOX*MEDIA` signature, reads the XDVDFS root directory, and exposes
the files through Xenia's virtual filesystem. A small sector cache serves
repeated and sequential reads; the cache is intentionally bounded so the
emulator does not need a full disc image in memory.

See [`docs/disc-drive.md`](docs/disc-drive.md) for the implementation details
and the SCSI/XGD layout.

## Verified configuration

The first release was tested with:

- Drive: `ATAPI iHAS122 F` at `E:`
- Disc: *Call of Duty: Black Ops* (title ID `41560855`)
- Backend: Direct3D 12
- Result: Xenia found XDVDFS at `0xFD90000`, loaded `default.xex`, resolved the
  title, and read game data files directly from the disc.

```text
Opened optical drive \\.\E: (2048 byte sectors, reported media size 7838695424).
Found XDVDFS at disc offset 0xFD90000.
Mounted optical disc from \\.\E:: XDVDFS at 0xFD90000, root sector 7631, root size 4096 bytes.
Loading module GAME:\default.xex
Title name: Call of Duty Black Ops
```

## Limitations

- A standard DVD drive without Xbox 360 read support will not work. The
  emulator writes the SCSI error to `xenia.log`.
- The disc must stay inserted while the game is running.
- Multi-disc swapping through an in-game prompt is not implemented yet.
  Close the emulator, insert the next disc, and launch the drive again.
- Optical loading is slower than loading an ISO from an SSD. The cache helps,
  but the first read of each file still comes from the disc.
- The fork does not create disc backups and does not include any game data.

## License and credits

Xenia Disc Edition is distributed under the BSD license, the same license as
Xenia. See [`LICENSE`](LICENSE).

Xenia was created by Ben Vanik and is developed by the Xenia and Xenia Canary
contributors. This fork builds on their work. The direct-disc implementation
and this README are the changes maintained here.
