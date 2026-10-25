# Xenia Disc Edition v1.0.0

First public release of Xenia Disc Edition, a fork of Xenia Canary with direct
Xbox 360 optical disc support.

## Highlights

- Launch games directly from a compatible optical drive.
- New `OpticalDiscDevice` VFS backend using raw SCSI `READ(12)` requests.
- Automatic detection of the full-disc XGD game partition at `0xFD90000`
  (LBA `0x1FB20`).
- On-demand XDVDFS parsing with a bounded 4 MiB sector cache.
- **File → Open Disc Drive → drive letter** menu.
- Command-line drive targets (`E:`, `E:\`, `\\.\E:`).
- Existing ISO, XEX, STFS, ZAR, and folder launching remains available.

## Release asset

`Xenia-Disc-Edition-v1.0.0-win64.zip` contains:

- `xenia_disc.exe`
- `Launch-Disc.bat`
- `README.md`
- `LICENSE`
- `disc-test.log`
- `optical-disc-support.patch`

SHA-256:

```text
94AAB852D63D61D7CC02B0BECA3362B995EC6210BA673128738AA7499413EB13
```

## Verification

Tested on Windows 11 x64 with:

- Drive: ATAPI `iHAS122 F` at `E:`
- Disc: *Call of Duty: Black Ops* (title ID `41560855`)
- Graphics backend: Direct3D 12

The emulator logged:

```text
Opened optical drive \\.\E: (2048 byte sectors, reported media size 7838695424).
Found XDVDFS at disc offset 0xFD90000.
Mounted optical disc from \\.\E:: XDVDFS at 0xFD90000, root sector 7631, root size 4096 bytes.
Loading module GAME:\default.xex
Title name: Call of Duty Black Ops
```

The title opened and read its data files directly from the disc.

## Important requirements

Xbox 360 discs hide the game partition outside the DVD-Video table of contents.
A stock optical drive will not be able to read it. Use a drive with Xbox 360
read support (Kreon, 0800-flashed Xbox 360 drive, or an equivalent iHAS-type
drive). This project does not modify drive firmware or bypass protections.

## Known limitations

- Multi-disc swapping through an in-game prompt is not implemented.
- The disc must remain inserted during play.
- Loading is limited by the optical drive's speed.
- This release is based on Xenia Canary commit `451f915`.

## Credits

Xenia Disc Edition is based on Xenia and Xenia Canary. Thanks to Ben Vanik and
all Xenia contributors. The fork-specific disc backend and documentation are
maintained in this repository.
