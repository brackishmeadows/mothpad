# Third-Party And Release Notes

This file is the public licensing map for Mothpad. It is not legal advice, but
it is the line in the dirt for future agents: do not blur clean source,
fallback experiments, and redistributable releases together.

## Project License

Mothpad itself does not currently have an open-source license. See
`LICENSE.md`.

Mothpad-authored material includes:

- the C editor core in `c/src/`
- the Pico shell in `c/pico/mothpad_pico_main.c`
- the clean PicoCalc LCD/keyboard shim in
  `c/pico/mothpad_picocalc_platform.c`
- the hand-authored 5x7 bitmap glyph table in that clean shim
- tests, scripts, and documentation in this repository

No third-party font file is compiled into the clean target.

## Build Targets

`mothpad_pico.uf2` is the clean target:

- Hardware-verified on PicoCalc on 2026-07-05 through Pelrun's UF2 Loader.
- Compiles Mothpad's C editor core and clean PicoCalc platform shim.
- Links Raspberry Pi Pico SDK libraries.
- Links `pico-vfs` with FatFs for SD-card file access.
- Does not compile ClockworkPi hello-world LCD/keyboard source.

`mothpad_pico_legacy.uf2` is a fallback target:

- Verified working on hardware on 2026-07-05.
- Compiles Mothpad's C editor core.
- Uses `c/pico/mothpad_picocalc_platform_legacy.c`.
- Compiles ClockworkPi's `picocalc_helloworld/lcdspi.c` and `i2ckbd.c`.
- Do not treat this as the public-release path unless ClockworkPi source
  licensing is clarified.

## External Dependencies

The dependencies below are not vendored in this repository. They are expected
in the local toolchain tree described by `docs/build-handoff.md`.

### Raspberry Pi Pico SDK

- Local path used during development: `picocalc/toolchains/pico-sdk`
- License observed locally: BSD-3-Clause style in `LICENSE.TXT`
- Public releases that include Pico SDK binaries or source should include the
  Pico SDK license notice and any bundled component notices that apply.

### pico-vfs

- Local path used during development:
  `picocalc/toolchains/PicoCalc-upstream/Code/pico_multi_booter/sd_boot/lib/pico-vfs`
- License observed locally: BSD-3-Clause in `LICENSE.md`
- Its `LICENSE.md` says `src/blockdevice/sd.c` was written with reference to
  ARM Mbed OS source and notes Apache-2.0 licensing for that reference source.
- Public releases should include the `pico-vfs` BSD-3-Clause notice and account
  for the Apache-2.0/Mbed OS reference note.

### FatFs

- Local path inside `pico-vfs`: `vendor/ff15`
- License observed locally: permissive ChaN FatFs license in `LICENSE.txt`
- Public releases that include FatFs source should retain the FatFs source
  notice. Binary-only FatFs redistribution has unusually light notice
  requirements, but include the notice anyway unless there is a reason not to.

## Hardware Facts

These values are hardware interface facts used by the clean shim. They are not
copied program logic:

- LCD SPI1: SCK 10, MOSI 11, MISO 12, CS 13, DC 14, RST 15.
- Keyboard I2C1: SDA 6, SCL 7, address `0x1f`, key register `0x09`,
  battery register `0x0b`.
- SD SPI0: SCK 18, MOSI 19, MISO 16, CS 17, detect 22.

## Public Release Checklist

Before attaching UF2 binaries or calling this a public release:

- Choose an explicit Mothpad project license, or keep `LICENSE.md` as
  all-rights-reserved on purpose.
- Release `mothpad_pico.uf2`, not `mothpad_pico_legacy.uf2`, unless
  ClockworkPi source licensing is clarified.
- Include notices for Pico SDK, `pico-vfs`, FatFs, and the Mbed OS/Apache-2.0
  reference note from `pico-vfs`.
- Note that the clean UF2 was built from the clean platform shim and does not
  compile ClockworkPi hello-world LCD/keyboard source.
