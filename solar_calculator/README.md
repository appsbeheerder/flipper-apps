# Solar Calculator

A **Flipper Zero** application that calculates the **sun** and **moon** for any
location on Earth — sunrise, sunset, twilight, the sun's position in the sky,
the current moon phase and the next new/full moon. Everything is computed
**offline** on the device from your coordinates and the Flipper's clock; no
internet connection is required.

The user interface is available in **12 languages** and can be switched at any
time.

- **Version:** 3.2.2
- **Author:** Eric M. Kok
- **License:** MIT (see [`LICENSE`](LICENSE) and [`LICENSES.md`](LICENSES.md))
- **Repository:** <https://github.com/appsbeheerder/flipper-apps>

---

## How it works

The app uses well-known astronomical algorithms (Meeus / NOAA solar equations
and a synodic moon-phase model) to turn three inputs into the values below:

1. Your **latitude** and **longitude**.
2. Your **UTC offset** (time zone), so all times can be shown in your local
   clock.
3. The **date and time** from the Flipper's real-time clock (RTC).

> **Important:** make sure the Flipper's clock is set correctly, otherwise the
> results will be off. Latitude is **positive north / negative south**,
> longitude is **positive east / negative west**.

On the very first launch the app asks for your language and location; after that
it remembers them until you reset.

---

## Controls

| Button | Action |
| ------ | ------ |
| **Up / Down** | Move between pages |
| **Left / Right** | Select a row on the Settings page |
| **OK** | Context action (edit location, toggle a setting, change language, …) |
| **Back** | Leave the app |

The **boot splash** appears on every launch. By default it stays until you press
**OK**; you can switch it to auto-continue after 2.5 seconds on the Settings
page.

---

## Screens and what the values mean

### 1. Sun

Times for the current day at your location, shown in your local time.

| Value | Meaning |
| ----- | ------- |
| **Sunrise** | Moment the top edge of the sun appears above the horizon (standard altitude −0.833°, which includes atmospheric refraction). |
| **Noon** | Solar transit — the moment the sun is highest, crossing your local meridian (true solar noon, not clock noon). |
| **Sunset** | Moment the top edge of the sun drops below the horizon. |
| **Eq. of time** | Equation of time, in minutes. The difference between apparent (sundial) solar time and mean (clock) time. Positive means the sundial is ahead of the clock. |

> **What is the equation of time?**
> Our clocks assume a *mean* sun that moves across the sky at a perfectly
> constant rate (exactly 24 hours between two noons). The **real** sun does not:
> Earth's orbit is an ellipse (so we move faster in January, slower in July),
> and Earth's axis is tilted by 23.4°. Together these make **true solar noon**
> (the sun exactly due south, at its highest) fall a little earlier or later
> than clock noon on most days.
>
> The equation of time is that offset, in minutes:
> - **positive (+):** the sundial is **ahead** of the clock — true noon is
>   **earlier** than the mean noon;
> - **negative (−):** the sundial is **behind** — true noon is **later**.
>
> Over the year it ranges from about **−14 min** (mid-February) to **+16 min**
> (early November), and is exactly **0** four times a year. It is also the
> reason the earliest sunset and latest sunrise do not fall exactly on the
> shortest day.
>
> *Example:* if the app shows `Noon 12:52` and `Eq. of time +5.0 min`, the sun
> is due south at 12:52 local clock time — five minutes ahead of where the
> "average" sun would be.

### 2. Twilight

Dawn and dusk ranges for three definitions of twilight, based on how far the sun
is below the horizon. Each line shows **dawn–dusk** in local time.

| Value | Sun below horizon | Meaning |
| ----- | ----------------- | ------- |
| **Civil** | 0° to −6° | Enough natural light for most outdoor activity; brightest stars visible. |
| **Nautical** | −6° to −12° | Horizon still faintly visible at sea; sky and horizon become hard to tell apart. |
| **Astro** (astronomical) | −12° to −18° | Sky effectively fully dark; faintest objects observable. |

If the sun never reaches the required depth that day (e.g. polar summer), the
value shows **None**.

### 3. Sun position

The sun's current position in the sky, right now.

| Value | Meaning |
| ----- | ------- |
| **Azimuth** | Compass direction to the sun, in degrees (0° = north, 90° = east, 180° = south, 270° = west). |
| **Elevation** | Height of the sun above the horizon, in degrees (negative means below the horizon). Corrected for atmospheric refraction. |

### 4. Moon

The moon right now.

| Value | Meaning |
| ----- | ------- |
| **Phase name** | One of the eight principal phases (New moon, Waxing crescent, First quarter, Waxing gibbous, Full moon, Waning gibbous, Last quarter, Waning crescent). |
| **Illum.** | Illuminated fraction of the moon's disc, as a percentage (0% = new, 100% = full). |
| **Age** | Days since the last new moon (0 to ~29.5). |

### 5. Moon phases

Countdown to the next two key moon events, each with its exact local date & time.

| Value | Meaning |
| ----- | ------- |
| **New moon in … days** | Days until the next new moon, plus the date/time it occurs. |
| **Full moon in … days** | Days until the next full moon, plus the date/time it occurs. |

### 6. Location

Your stored coordinates. Press **OK** to edit them with the on-screen keypad.

| Value | Meaning |
| ----- | ------- |
| **Latitude** | Degrees north (+) or south (−), −90 to 90. |
| **Longitude** | Degrees east (+) or west (−), −180 to 180. |
| **Timezone** | Your offset from UTC in hours (e.g. `UTC+1.0`), −12 to 14. |

### 7. Settings

A small menu — use **Left/Right** to pick a row, **OK** to activate it.

| Row | Meaning |
| --- | ------- |
| **Mode** | Splash-screen behaviour: *Press OK* (stays until you press a key) or *2.5 sec* (auto-continues). |
| **Reset** | Clears the saved language and splash choice and restarts the app, so it runs its first-time setup again. |

### 8. About

App version, author, copyright years and license.

### 9. GitHub (QR)

An on-screen **QR code** of the project's GitHub URL — scan it with a phone to
open the repository.

### 10. NFC

The Flipper emulates an **NFC tag** (NTAG213) carrying the GitHub URL as an NDEF
record. Hold a phone near the Flipper and it is offered the link to open.

---

## Building

The app is built with [`ufbt`](https://pypi.org/project/ufbt/) (the micro
Flipper Build Tool).

```sh
# Build only
python -m ufbt

# Build and launch on a connected Flipper over USB
python -m ufbt launch
```

On Windows you can use the helper script, which first regenerates the language
manifest and then builds (or launches):

```bat
compileer.cmd          :: build + launch on the Flipper
compileer.cmd build    :: build only
```

Languages live in `files/lang_*.h`; `pre_build.ps1` regenerates
`lang_manifest.h` from them automatically when one changes.

---

## Languages

The interface is translated into: Nederlands, English, Deutsch, Español,
Français, Italiano, Português, Dansk, Norsk, Svenska, Bahasa Indonesia and
Türkçe.

### What the program does (per language)

- 🇬🇧 **English** — Solar Calculator computes sunrise, sunset, twilight, the
  sun's position and the moon's phases for any location, entirely offline on
  your Flipper Zero.
- 🇳🇱 **Nederlands** — Solar Calculator berekent zonsopkomst, zonsondergang,
  schemering, de zonpositie en de maanfasen voor elke locatie, volledig offline
  op je Flipper Zero.
- 🇩🇪 **Deutsch** — Solar Calculator berechnet Sonnenaufgang, Sonnenuntergang,
  Dämmerung, den Sonnenstand und die Mondphasen für jeden Ort – vollständig
  offline auf deinem Flipper Zero.
- 🇪🇸 **Español** — Solar Calculator calcula el amanecer, el atardecer, el
  crepúsculo, la posición del sol y las fases de la luna para cualquier
  ubicación, totalmente sin conexión en tu Flipper Zero.
- 🇫🇷 **Français** — Solar Calculator calcule le lever et le coucher du soleil,
  le crépuscule, la position du soleil et les phases de la lune pour n'importe
  quel lieu, entièrement hors ligne sur votre Flipper Zero.
- 🇮🇹 **Italiano** — Solar Calculator calcola alba, tramonto, crepuscolo, la
  posizione del sole e le fasi lunari per qualsiasi località, completamente
  offline sul tuo Flipper Zero.
- 🇵🇹 **Português** — Solar Calculator calcula o nascer e o pôr do sol, o
  crepúsculo, a posição do sol e as fases da lua para qualquer local, totalmente
  offline no seu Flipper Zero.
- 🇩🇰 **Dansk** — Solar Calculator beregner solopgang, solnedgang, tusmørke,
  solens position og månefaser for ethvert sted – helt offline på din Flipper
  Zero.
- 🇳🇴 **Norsk** — Solar Calculator beregner soloppgang, solnedgang, skumring,
  solens posisjon og månefaser for et hvilket som helst sted – helt offline på
  din Flipper Zero.
- 🇸🇪 **Svenska** — Solar Calculator beräknar soluppgång, solnedgång, skymning,
  solens position och månfaser för valfri plats – helt offline på din Flipper
  Zero.
- 🇮🇩 **Bahasa Indonesia** — Solar Calculator menghitung matahari terbit,
  matahari terbenam, senja, posisi matahari, dan fase bulan untuk lokasi mana
  pun, sepenuhnya offline di Flipper Zero Anda.
- 🇹🇷 **Türkçe** — Solar Calculator; herhangi bir konum için gün doğumu, gün
  batımı, alacakaranlık, güneşin konumu ve ay evrelerini tamamen çevrimdışı
  olarak Flipper Zero'nuzda hesaplar.

---

## Credits & license

Solar Calculator is released under the **MIT License**. It ports and bundles a
few MIT-licensed libraries; see [`LICENSES.md`](LICENSES.md) and
[`NOTICE`](NOTICE) for the full attribution:

- Solar-position math from [jpb10/SolarCalculator](https://github.com/jpb10/SolarCalculator).
- Moon-phase math from [CelliesProjects/moonPhase-esp32](https://github.com/CelliesProjects/moonPhase-esp32).
- QR generation from [Nayuki's QR Code generator](https://github.com/nayuki/QR-Code-generator).
