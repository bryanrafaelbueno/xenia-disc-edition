# Xenia Disc Edition v1.0.1

Second release of Xenia Disc Edition, a fork of Xenia Canary with direct Xbox
360 optical disc support.

## Highlights

- **Native disc auto-swap**, controlled only by a Xenia config option.
  - No batch files, PowerShell scripts, or external launchers.
  - `disc_auto_swap = true` in `xenia-canary.config.toml` (enabled by default).
  - When a different Xbox 360 game disc is inserted, Xenia restarts itself with
    the new game using an internal wait-for-process handoff.
- **Deep XDVDFS directory trees** are now supported (recursion limit raised from
  64 to 4096). This fixes *GRID 2* and other discs that use the XGD3-style
  `0x2080000` game partition.
- Detection of both the XGD2 `0xFD90000` and XGD3 `0x2080000` game partition
  offsets.
- Existing ISO, XEX, STFS, ZAR, and folder launching remains available.

## Configuration

```toml
[General]
disc_auto_swap = true
```

Set it to `false` to use the normal manual **File → Open Disc Drive** behavior
without automatic restarts.

## Release asset

The Windows release archive contains:

- `xenia_disc.exe`
- `README.md`
- `LICENSE`
- Verification logs for the release build
- `optical-disc-support.patch` (changes against Xenia Canary)
- `BUILD-INFO.txt`
- `SHA256SUMS.txt`

No launcher scripts are included.

## Verification

Tested on Windows 11 x64 with an ATAPI `iHAS122 F` drive at `E:`:

- *Call of Duty: Black Ops* (`41560855`), XGD2 layout at `0xFD90000`.
- *Portal 2* (`45410912`), XGD2 layout at `0xFD90000`.
- *GRID 2* (`434D0844`), XGD3-style layout at `0x2080000`; this disc previously
  failed with `XDVDFS directory tree is too deep`.
- *LEGO Marvel's Avengers* and *DuckTales: Remastered* were used for the native
  auto-swap test. The log shows the change detection, the restart handoff, and
  the next title loading automatically.

## Important requirements

Xbox 360 discs hide the game partition outside the DVD-Video table of contents.
A stock optical drive will not be able to read it. Use a drive with Xbox 360
read support (Kreon, 0800-flashed Xbox 360 drive, or an equivalent iHAS-type
drive). This project does not modify drive firmware or bypass protections.

## Known limitations

- Auto-swap restarts Xenia; it does not replace a running title in place. Save
  the game before ejecting the disc.
- Multi-disc games are not continued automatically; inserting the next disc
  starts that disc as a new title.
- The disc must remain inserted during play.
- Loading is limited by the optical drive's speed.
- This release is based on Xenia Canary commit `451f915`.

## Credits

Xenia Disc Edition is based on Xenia and Xenia Canary. Thanks to Ben Vanik and
all Xenia contributors. The fork-specific disc backend and documentation are
maintained in this repository.
