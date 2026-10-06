# Third-party credits and licenses

All apps in this repository are licensed under the GNU General Public License
version 3 (GPL-3.0-only), Copyright (c) 2026 Eric M. Kok. Releases up to and
including device_info 1.12, solar_calculator 3.4.4, pomodoro 1.01 and
otp_vault 1.04 were published under the MIT license and stay available under
it for those who received them. This file lists what the apps owe to others. Nothing here replaces the license
texts in the individual app folders.

## OTPvault (`otp_vault`)

**No third-party source code is included.** SHA-1, SHA-256, HMAC, PBKDF2,
Base32 and TOTP are written from the published specifications and checked by a
self-test on the device:

- RFC 4226 (HOTP) and RFC 6238 (TOTP), including the TOTP test values.
- RFC 2104 (HMAC), RFC 2202 (HMAC-SHA1 test values), RFC 4231 (HMAC-SHA256 test values).
- RFC 8018 (PBKDF2). The PBKDF2-HMAC-SHA256 test values ("password" / "salt")
  were computed with Python's `hashlib`.
- RFC 4648 (Base32), FIPS 180-4 (SHA-1, SHA-256), NIST SP 800-38D (AES-GCM).

**Compared with, not copied from:** Authenticator for Flipper Zero by
Alexander Kopachov (akopachov) and contributors,
<https://github.com/akopachov/flipper-zero_authenticator>, licensed under
GPL-3.0. Its design (how the vault is encrypted) was read to find out where
OTPvault can do better; the comparison is in `otp_vault/README.md`. No code
from it is used. It
bundles wolfSSL (GPL / commercial) and a Base32 library by Google (Apache-2.0);
neither is used here.

Built with the Flipper Zero firmware SDK (GPL-3.0, Flipper Devices Inc.)
through uFBT, <https://github.com/flipperdevices/flipperzero-ufbt>. The apps
call the public firmware API (crypto enclave, GUI, storage, USB HID).

## Solar Calculator (`solar_calculator`)

Includes MIT-licensed code from others, which stays under its own MIT license
within the GPLv3 whole. Their copyright notices are in
`solar_calculator/NOTICE` and `solar_calculator/LICENSES.md`:

- jpb10, SolarCalculator, Copyright (c) 2021 jpb10 (solar position math).
  <https://github.com/jpb10/SolarCalculator>
- CelliesProjects, moonPhase-esp32, Copyright (c) 2018 Cellie (moon phase),
  itself adapted from voidware.com/phase.c.
  <https://github.com/CelliesProjects/moonPhase-esp32>
- Project Nayuki, QR Code generator library (`qrcodegen.c`, `qrcodegen.h`),
  Copyright (c) Project Nayuki.
  <https://github.com/nayuki/QR-Code-generator>

## Device Info (`device_info`) and Pomodoro (`pomodoro`)

No third-party source code. Built with the Flipper Zero firmware SDK as above.
