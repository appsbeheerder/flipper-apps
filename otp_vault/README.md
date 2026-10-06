# OTPvault

A password-protected TOTP vault for the Flipper Zero. The accounts are stored
in one encrypted file that only opens on this Flipper and only with your
password. The app types the current code over USB as a keyboard.

**Status: experimental.** Early versions, not independently reviewed. The
built-in self-test checks the crypto code against RFC test values on every
start; if it fails, the app shows an error screen and refuses to run.

Copyright (c) 2026 Eric M. Kok. GPL-3.0 license, see `LICENSE`. Credits and
third-party licenses: `../THIRD-PARTY.md`.

## Features

- TOTP with SHA-1 or SHA-256, 6 to 8 digits, 10 to 120 second periods.
- Up to 16 accounts.
- Types the code over USB (top row or numpad keys).
- Auto-lock after 15 seconds to 30 minutes of inactivity.
- English and Dutch (one `files/lang_<code>.h` per language).

## How the vault is protected

1. Your password goes through PBKDF2-HMAC-SHA256 with a random salt. The number
   of rounds is measured on the Flipper itself so one attempt takes about
   1.5 seconds (at least 10,000 rounds).
2. The result is passed through the Flipper's secure-enclave key (slot 11,
   unique to this device and never readable). The vault key therefore needs both
   the password and this Flipper.
3. The accounts are encrypted with AES-256-GCM. The file header (version,
   rounds, salt, IV) is authenticated too, so a changed file or a wrong password
   is detected.

## Where it is better than Authenticator

Compared with *Authenticator for Flipper Zero* by Alexander Kopachov
(<https://github.com/akopachov/flipper-zero_authenticator>, GPL-3.0). This is
based on reading its `crypto_v3.c` (repository state of May 2025), not on an
independent audit, and it can change in later versions of that app. No code
from it is used.

| | Authenticator (as read) | OTPvault |
|---|---|---|
| What the PIN or password does | Optional PIN. It only feeds the IV, derived with PBKDF2-SHA512 (UID + PIN + salt). The AES key itself sits in an enclave slot (default 12) and does not depend on the PIN. | The password determines the key. Without it nothing can be decrypted, not even by a modified app on the same Flipper. |
| Cipher mode | AES with CBC and zero padding, no authentication tag. A separate check blob tells whether the PIN is right. | AES-256-GCM with an authentication tag; the header is authenticated. |
| Key derivation | PBKDF2, 200 rounds. | PBKDF2-HMAC-SHA256, measured to about 1.5 s per attempt. |
| Bound to the device | Yes (enclave key). | Yes (enclave key, slot 11), plus the password. |
| Broken crypto code | No self-test known. | Self-test on every start; a failure blocks the app. |
| Import file | Not applicable. | The plain-text import file is overwritten and deleted after import. |

## Where it is behind

Authenticator has years of use by many people. OTPvault does not yet have:
SHA-512, HOTP, Steam Guard, adding or editing accounts on the Flipper, more than
16 accounts, QR import, or a phone companion. Passwords are typed one character
at a time on the Flipper, which is slow. The wait after wrong passwords is kept
in memory only. The full list is in `TODO.md`.

## Import

Put `import.txt` in `apps_data/otp_vault` on the SD card, one account per line:

```
Name;BASE32SECRET;sha1;6;30
```

Everything after the secret is optional (`sha1` or `sha256`, digits 6 to 8,
period in seconds). Lines starting with `#` are skipped. Then open the Import
screen in the app and press OK. If one line is wrong, nothing is imported.

## Build

Needs [uFBT](https://github.com/flipperdevices/flipperzero-ufbt)
(`pip install ufbt`). From the repository root: `.\build.ps1 otp_vault`, or
`.\build.ps1 otp_vault -Launch` to upload and start it. Built against firmware
API 87.1.
