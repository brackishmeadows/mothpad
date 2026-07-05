# Third-Party Notes

This is a practical licensing map for Mothpad's current Pico build. It is not
legal advice.

## Current Pico UF2s

There are two PicoCalc hardware paths in `c/pico`:

`mothpad_pico_legacy.uf2`:

- Verified working on hardware on 2026-07-05.
- Compiles Mothpad's C editor core.
- Uses `c/pico/mothpad_picocalc_platform_legacy.c`.
- Compiles ClockworkPi's `picocalc_helloworld/lcdspi.c` and `i2ckbd.c`.
- Links Raspberry Pi Pico SDK libraries.
- Links `pico-vfs` with FatFs for SD-card file access.

`mothpad_pico.uf2`:

- Verified working on PicoCalc hardware on 2026-07-05 through Pelrun's UF2
  Loader.
- Uses Mothpad's clean PicoCalc LCD/keyboard platform shim:
  `c/pico/mothpad_picocalc_platform.c`.
- Does not compile ClockworkPi's hello-world LCD/keyboard source.
- Uses Mothpad-authored 5x7 bitmap glyph data in
  `c/pico/mothpad_picocalc_platform.c`.

## Remaining External Code

Mothpad clean source:

- Mothpad-authored C editor core, Pico shell, clean LCD/keyboard shim, and
  bitmap glyph table.
- No third-party font file is compiled into the clean target.

Pico SDK:

- Local path: `picocalc/toolchains/pico-sdk`
- License: BSD-3-Clause style, with bundled component licenses.

`pico-vfs`:

- Local path:
  `picocalc/toolchains/PicoCalc-upstream/Code/pico_multi_booter/sd_boot/lib/pico-vfs`
- License observed locally: BSD-3-Clause in `LICENSE.md`.
- `LICENSE.md` says `src/blockdevice/sd.c` was written with reference to ARM
  Mbed OS and notes Apache-2.0 licensing for that reference source.

FatFs inside `pico-vfs`:

- Local path: `pico-vfs/vendor/ff15`
- License observed locally: permissive ChaN FatFs license.

The SD block device code in `pico-vfs` has ancestry notes pointing at Mbed OS;
keep the upstream notices with any distribution.

## ClockworkPi Source Position

ClockworkPi sample code is still present as the fallback `mothpad_pico_legacy`
target. The clean shim is the desired licensing direction and is now the
hardware-good path. Pin assignments and bus behavior are treated as hardware
facts:

- LCD SPI1: SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RST 15.
- Keyboard I2C1: SDA 6, SCL 7, address `0x1f`, key register `0x09`.
- SD SPI0: SCK 18, MOSI 19, MISO 16, CS 17, detect 22.

## If Shipping Publicly

Before a public release:

- Include Pico SDK license notices.
- Include `pico-vfs` and FatFs notices.
- Include or clearly account for the Apache-2.0 notice for the `pico-vfs`
  SD block-device Mbed OS reference note.
- Do not ship a ClockworkPi-source-linked UF2 publicly unless the license is
  clarified or replaced.
