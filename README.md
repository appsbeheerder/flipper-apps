# Flipper Zero apps

Small apps for the Flipper Zero by Eric M. Kok.

Page with downloads: <https://appsbeheerder.github.io/flipper-apps/>

| App | Folder |
|-----|--------|
| Device Info | `device_info` |
| Solar Calculator | `solar_calculator` |
| Pomodoro | `pomodoro` |
| OTPvault (experimental) | `otp_vault` |

## Build

Needs Python and [uFBT](https://github.com/flipperdevices/flipperzero-ufbt)
(`pip install ufbt`). The apps are built against the official firmware API 87.1.

```
.\build.ps1 <app>             # build, for example .\build.ps1 pomodoro
.\build.ps1 <app> -Launch     # build, upload and start on a connected Flipper
```

Close qFlipper before uploading: it keeps the USB serial port open.

## Credits

See [`THIRD-PARTY.md`](THIRD-PARTY.md) for third-party code, comparisons and copyright notices.

## License

GNU GPL version 3, Copyright (c) 2026 Eric M. Kok. See `LICENSE` and the `LICENSE`
file in each app folder. Releases up to device_info 1.12, solar_calculator 3.4.4,
pomodoro 1.01 and otp_vault 1.04 were published under the MIT license and stay
available under it. `solar_calculator` includes MIT-licensed code from others,
which keeps its own license; see its `NOTICE` and `LICENSES.md`. Provided as is,
without warranty.
