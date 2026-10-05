/*
 * Solar Calculator - Flipper Zero app
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: MIT
 * See LICENSE in this app's folder for the full license text.
 *
 * The solar-position math below is a C port of the algorithms in
 * jpb10/SolarCalculator (https://github.com/jpb10/SolarCalculator),
 * Copyright (c) 2021 jpb10, MIT License. See NOTICE for details.
 *
 * Ported to single-precision float throughout, matching this firmware's
 * Cortex-M4 single-precision FPU (and -Wdouble-promotion -Werror build
 * flags) - the original library's own docs note it was designed to work
 * fine at single precision.
 *
 * The moon-phase math is a C port of CelliesProjects/moonPhase-esp32
 * (https://github.com/CelliesProjects/moonPhase-esp32), Copyright (c) 2018
 * Cellie, MIT License (itself adapted from voidware.com/phase.c). See NOTICE.
 *
 * The QR code uses Nayuki's QR Code generator library (qrcodegen.c/.h),
 * Copyright (c) Project Nayuki, MIT License. See LICENSES.md / NOTICE.
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <datetime/datetime.h>
#include <loader/loader.h>
#include <nfc/nfc.h>
#include <nfc/nfc_listener.h>
#include <nfc/protocols/mf_ultralight/mf_ultralight.h>
#include <nfc/protocols/iso14443_3a/iso14443_3a.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "qrcodegen.h"

#define APP_VERSION "3.4.4"
#define APP_START_YEAR 2026
#define APP_AUTHOR "Eric M. Kok"
#define APP_LICENSE "MIT"

#define SCREEN_W 128
#define SCREEN_H 64

#define REFRESH_MS 1000
#define SPLASH_MS 2500 // how long the boot splash stays up before auto-advancing

#define PAGE_SUN 0
#define PAGE_TWILIGHT 1
#define PAGE_POSITION 2
#define PAGE_MOON 3
#define PAGE_MOON_EVENTS 4
#define PAGE_LOCATION 5
#define PAGE_SETTINGS 6
#define PAGE_ABOUT 7
#define PAGE_QR 8
#define PAGE_NFC 9
#define PAGE_COUNT 10

// Selectable rows on the combined Settings page.
#define SETTINGS_SPLASH 0
#define SETTINGS_RESET 1
#define SETTINGS_COUNT 2

// Selectable rows on the Location page (edit coordinates vs. timezone).
#define LOCATION_COORDS 0
#define LOCATION_TZ 1
#define LOCATION_COUNT 2

// GitHub URL shared by the QR page (and later the NFC page).
#define APP_GITHUB_URL "https://github.com/appsbeheerder/flipper-apps"
// Cap the QR version so the module buffer stays small (URL fits well under it).
#define QR_MAX_VERSION 6

#define MAX_LINES 6
#define LINE_LENGTH 40

#define LANGUAGE_VISIBLE 4

#define KEYPAD_BUFFER_SIZE 24
#define KEYPAD_ROWS 4
#define KEYPAD_MAX_COLS 4

//======================================================================================================================
// Solar math - ported from jpb10/SolarCalculator (MIT). All calculations
// assume time inputs in Coordinated Universal Time (UTC).
//======================================================================================================================

#define SOLAR_PI 3.14159265358979323846f

#define SUNRISESET_STD_ALTITUDE -0.8333f
#define CIVIL_DAWNDUSK_STD_ALTITUDE -6.0f
#define NAUTICAL_DAWNDUSK_STD_ALTITUDE -12.0f
#define ASTRONOMICAL_DAWNDUSK_STD_ALTITUDE -18.0f

typedef struct {
    float JD; // Julian day at 0h UT (JD ending in .5)
    float m; // Fractional day, 0h to 24h (decimal number between 0 and 1)
} JulianDay;

static JulianDay julian_day_from_utc(uint32_t utc) {
    JulianDay jd;
    jd.JD = (float)(utc / 86400) + 2440587.5f;
    jd.m = (utc % 86400) / 86400.0f;
    return jd;
}

static float deg2rad(float deg) {
    return deg * SOLAR_PI / 180.0f;
}

static float rad2deg(float rad) {
    return rad * 180.0f / SOLAR_PI;
}

static float wrap360(float angle) {
    angle = fmodf(angle, 360.0f);
    if(angle < 0) angle += 360.0f;
    return angle;
}

static float wrap180(float angle) {
    return wrap360(angle + 180.0f) - 180.0f;
}

static float calc_julian_cent(JulianDay jd) {
    return (jd.JD - 2451545.0f + jd.m) / 36525.0f;
}

static float calc_geom_mean_long_sun(float T) {
    return wrap360(280.46646f + T * 36000.76983f);
}

static float calc_geom_mean_anomaly_sun(float T) {
    return wrap360(357.52911f + T * 35999.05029f);
}

static float calc_sun_eq_of_center(float T) {
    float M = calc_geom_mean_anomaly_sun(T);
    return sinf(deg2rad(M)) * (1.914602f - 0.004817f * T) + sinf(2.0f * deg2rad(M)) * 0.019993f;
}

static float calc_mean_obliquity(float T) {
    return 23.4392911f - T * 0.0130042f;
}

static void calc_solar_coordinates(float T, float* ra, float* dec) {
    float L0 = calc_geom_mean_long_sun(T);
    float C = calc_sun_eq_of_center(T);
    float L = L0 + C - 0.00569f;

    float eps = calc_mean_obliquity(T);
    *ra = rad2deg(atan2f(cosf(deg2rad(eps)) * sinf(deg2rad(L)), cosf(deg2rad(L))));
    *dec = rad2deg(asinf(sinf(deg2rad(eps)) * sinf(deg2rad(L))));
}

static float calc_gr_mean_sidereal_time(JulianDay jd) {
    float GMST0 = wrap360(100.46061837f + 0.98564736629f * (jd.JD - 2451545.0f));
    return wrap360(GMST0 + 360.985647f * jd.m);
}

static void equatorial_to_horizontal(float H, float dec, float lat, float* az, float* el) {
    float xhor =
        cosf(deg2rad(H)) * cosf(deg2rad(dec)) * sinf(deg2rad(lat)) -
        sinf(deg2rad(dec)) * cosf(deg2rad(lat));
    float yhor = sinf(deg2rad(H)) * cosf(deg2rad(dec));
    float zhor =
        cosf(deg2rad(H)) * cosf(deg2rad(dec)) * cosf(deg2rad(lat)) +
        sinf(deg2rad(dec)) * sinf(deg2rad(lat));

    *az = rad2deg(atan2f(yhor, xhor));
    *el = rad2deg(atan2f(zhor, sqrtf(xhor * xhor + yhor * yhor)));
}

// Hour angle at sunrise or sunset, returns NaN if circumpolar.
static float calc_hour_angle_rise_set(float dec, float lat, float h0) {
    return rad2deg(acosf(
        (sinf(deg2rad(h0)) - sinf(deg2rad(lat)) * sinf(deg2rad(dec))) /
        (cosf(deg2rad(lat)) * cosf(deg2rad(dec)))));
}

static float calc_refraction(float el) {
    if(el < -0.575f) {
        return -20.774f / tanf(deg2rad(el)) / 3600.0f;
    } else {
        return 1.02f / tanf(deg2rad(el + 10.3f / (el + 5.11f))) / 60.0f;
    }
}

// Equation of time, in minutes of time.
static void calc_equation_of_time(JulianDay jd, float* E) {
    float T = calc_julian_cent(jd);
    float L0 = calc_geom_mean_long_sun(T);

    float ra, dec;
    calc_solar_coordinates(T, &ra, &dec);

    *E = 4.0f * wrap180(L0 - 0.00569f - ra);
}

// Sun's topocentric horizontal coordinates, corrected for refraction, in degrees.
static void calc_horizontal_coordinates(
    JulianDay jd,
    float latitude,
    float longitude,
    float* azimuth,
    float* elevation) {
    float T = calc_julian_cent(jd);
    float GMST = calc_gr_mean_sidereal_time(jd);

    float ra, dec;
    calc_solar_coordinates(T, &ra, &dec);

    float H = GMST + longitude - ra;
    equatorial_to_horizontal(H, dec, latitude, azimuth, elevation);

    *azimuth += 180.0f;
    *elevation += calc_refraction(*elevation);
}

// Find the times of sunrise, transit, and sunset, in hours (UTC).
static void calc_sunrise_sunset(
    JulianDay jd,
    float latitude,
    float longitude,
    float* transit,
    float* sunrise,
    float* sunset,
    float altitude) {
    float m[3];
    m[0] = 0.5f - longitude / 360.0f;
    m[1] = m[0];
    m[2] = m[0];
    const int iterations = 1;

    for(int i = 0; i <= iterations; i++) {
        for(int event = 0; event < 3; event++) {
            jd.m = m[event];
            float T = calc_julian_cent(jd);
            float GMST = calc_gr_mean_sidereal_time(jd);

            float ra, dec;
            calc_solar_coordinates(T, &ra, &dec);

            float m0 = jd.m + wrap180(ra - longitude - GMST) / 360.0f;
            float d0 = calc_hour_angle_rise_set(dec, latitude, altitude) / 360.0f;

            if(event == 0) m[0] = m0;
            if(event == 1 || i == 0) m[1] = m0 - d0;
            if(event == 2 || i == 0) m[2] = m0 + d0;
            if(i == 0) break;
        }
    }

    *transit = m[0] * 24.0f;
    *sunrise = m[1] * 24.0f;
    *sunset = m[2] * 24.0f;
}

//======================================================================================================================
// Moon phase - ported from CelliesProjects/moonPhase-esp32 (MIT, Copyright (c)
// 2018 Cellie / Marcel Timmer), itself adapted from voidware.com/phase.c.
// Ported to single-precision float: the Kepler iteration uses a float-reachable
// tolerance with an iteration cap (the original's 1e-12 double tolerance would
// never be met in float), and the Julian day subtracts the large epoch constant
// before adding the fractional day to preserve precision. See NOTICE.
//======================================================================================================================

// Reference epoch used by the moon library: JD 2444238.5 (1980-01-00.0).
#define MOON_EPOCH_JD 2444238.5f
#define MOON_SYNODIC_MONTH 29.530588853f

typedef struct {
    float elongation; // Moon-Sun elongation in degrees, 0..360 (0=new, 180=full)
    float illumination; // Illuminated fraction, 0..1
    float age_days; // Days since new moon, 0..~29.53
} MoonPhase;

// Days since the moon-library epoch for a given Julian day, computed so the
// fractional day survives single-precision rounding.
static float moon_days_since_epoch(JulianDay jd) {
    return (jd.JD - MOON_EPOCH_JD) + jd.m;
}

// Sun's ecliptic longitude (degrees) in the moon library's own frame.
static float moon_sun_position(float j) {
    float n = fmodf(360.0f / 365.2422f * j, 360.0f);
    float x = n - 3.762863f;
    if(x < 0.0f) x += 360.0f;
    x = deg2rad(x);
    float e = x;
    for(int i = 0; i < 20; i++) {
        float dl = e - 0.016718f * sinf(e) - x;
        e = e - dl / (1.0f - 0.016718f * cosf(e));
        if(fabsf(dl) < 1e-6f) break;
    }
    float v = 360.0f / SOLAR_PI * atanf(1.01686011182f * tanf(e / 2.0f));
    float l = fmodf(v + 282.596403f, 360.0f);
    if(l < 0.0f) l += 360.0f;
    return l;
}

// Moon's ecliptic longitude (degrees), given the sun's longitude ls.
static float moon_moon_position(float j, float ls) {
    float ms = 0.985647332099f * j - 3.762863f;
    if(ms < 0.0f) ms += 360.0f;
    float l = fmodf(13.176396f * j + 64.975464f, 360.0f);
    if(l < 0.0f) l += 360.0f;
    float mm = fmodf(l - 0.1114041f * j - 349.383063f, 360.0f);
    float ev = 1.2739f * sinf(deg2rad(2.0f * (l - ls) - mm));
    float sms = sinf(deg2rad(ms));
    float ae = 0.1858f * sms;
    mm += ev - ae - 0.37f * sms;
    float ec = 6.2886f * sinf(deg2rad(mm));
    l += ev + ec - ae + 0.214f * sinf(deg2rad(2.0f * mm));
    l = 0.6583f * sinf(deg2rad(2.0f * (l - ls))) + l;
    return l;
}

// Moon-Sun elongation in degrees (0..360) for a given day count j.
static float moon_elongation(float j) {
    float ls = moon_sun_position(j);
    float lm = moon_moon_position(j, ls);
    float d = fmodf(lm - ls, 360.0f);
    if(d < 0.0f) d += 360.0f;
    return d;
}

static void calc_moon_phase(JulianDay jd, MoonPhase* mp) {
    float d = moon_elongation(moon_days_since_epoch(jd));
    mp->elongation = d;
    mp->illumination = (1.0f - cosf(deg2rad(d))) / 2.0f;
    mp->age_days = d / 360.0f * MOON_SYNODIC_MONTH;
}

// Index 0..7 into the eight principal phase names, from the elongation.
static int moon_phase_index(float elongation) {
    float d = elongation + 22.5f;
    if(d >= 360.0f) d -= 360.0f;
    int idx = (int)(d / 45.0f);
    if(idx < 0) idx = 0;
    if(idx > 7) idx = 7;
    return idx;
}

// Signed offset (-180..180] of the elongation from a target angle.
static float moon_phase_offset(uint32_t utc, float target) {
    JulianDay jd = julian_day_from_utc(utc);
    return wrap180(moon_elongation(moon_days_since_epoch(jd)) - target);
}

// UTC timestamp of the next moment the moon reaches the target elongation
// (0 = new moon, 180 = full moon), searching forward from start_utc. Returns 0
// if not found within ~40 days (should not happen).
static uint32_t moon_next_phase(uint32_t start_utc, float target) {
    const uint32_t step = 6u * 3600u;
    const uint32_t max_span = 40u * 24u * 3600u;
    uint32_t t0 = start_utc;
    float g0 = moon_phase_offset(t0, target);
    for(uint32_t elapsed = 0; elapsed < max_span; elapsed += step) {
        uint32_t t1 = t0 + step;
        float g1 = moon_phase_offset(t1, target);
        if(g0 < 0.0f && g1 >= 0.0f) {
            uint32_t lo = t0, hi = t1;
            for(int i = 0; i < 24; i++) {
                uint32_t mid = lo + (hi - lo) / 2;
                if(moon_phase_offset(mid, target) < 0.0f)
                    lo = mid;
                else
                    hi = mid;
            }
            return hi;
        }
        t0 = t1;
        g0 = g1;
    }
    return 0;
}

//======================================================================================================================
// Language strings
//======================================================================================================================

typedef struct {
    const char* lang_name;

    const char* title_sun;
    const char* title_twilight;
    const char* title_position;
    const char* title_location;
    const char* title_reset;
    const char* title_about;
    const char* title_language;

    const char* lbl_sunrise;
    const char* lbl_transit;
    const char* lbl_sunset;
    const char* lbl_eot;

    const char* lbl_civil;
    const char* lbl_nautical;
    const char* lbl_astronomical;
    const char* msg_circumpolar;

    const char* lbl_azimuth;
    const char* lbl_elevation;
    const char* lbl_degrees; // unit word for angles, e.g. "deg"/"graden"

    const char* title_moon;
    const char* title_moon_events;
    const char* lbl_illumination;
    const char* lbl_moon_age;
    const char* lbl_new_moon;
    const char* lbl_full_moon;
    const char* moon_new;
    const char* moon_waxing_crescent;
    const char* moon_first_quarter;
    const char* moon_waxing_gibbous;
    const char* moon_full;
    const char* moon_waning_gibbous;
    const char* moon_last_quarter;
    const char* moon_waning_crescent;

    // Countdown label "<moon> <conn> N <day>", e.g. "New moon in 4 days".
    // conn may be "" for languages that postfix the unit (e.g. Turkish
    // "Yeni ay 4 gun sonra"). moon_day_one/many are the singular/plural unit.
    const char* moon_conn;
    const char* moon_day_one;
    const char* moon_day_many;

    const char* kbd_save; // on-screen keypad "Save" button label

    const char* lbl_coordinates;
    const char* lbl_latitude;
    const char* lbl_longitude;
    const char* lbl_timezone;
    const char* location_hint;

    const char* lbl_current_lang;
    const char* reset_hint;

    const char* wizard_lat_header;
    const char* wizard_lon_header;
    const char* wizard_tz_header;
    const char* wizard_invalid;

    const char* about_version_label;
    const char* about_author_label;
    const char* about_license_label;
    const char* about_hint;

    // Combined Settings page (splash-mode row + reset row).
    const char* title_settings;
    const char* lbl_splash_mode;
    const char* splash_mode_ok; // value when the splash waits for OK
    const char* splash_mode_auto; // value when the splash auto-advances
    const char* splash_hint;

    // Share/export pages: QR (screen 9) and NFC (screen 10).
    const char* qr_cta; // call-to-action, e.g. "Scan for GitHub sources"
    const char* nfc_desc; // explains the NFC tap opens this app's source code

    // Three-letter month abbreviations (Jan..Dec) for the boot-splash date.
    const char* months[12];
} LangStrings;

// Generated by pre_build.ps1 from files/lang_*.h - see that script to add a
// language without touching this file.
#include "lang_manifest.h"

//======================================================================================================================
// App state
//======================================================================================================================

typedef struct {
    float latitude;
    float longitude;
    int tz_index; // index into TIMEZONES (named zone with automatic DST)
} Location;

typedef enum {
    ScreenSplash,
    ScreenLanguagePicker,
    ScreenLocationKeypad,
    ScreenTimezonePicker,
    ScreenInfo,
} AppScreen;

//======================================================================================================================
// Named timezones with automatic daylight-saving time (DST). The user picks a
// named zone (e.g. CET); the app derives the current UTC offset from the zone's
// standard offset plus its DST rule applied to the Flipper's local date, so
// e.g. CET automatically becomes CEST (+1h) in summer.
//======================================================================================================================

typedef enum {
    DstNone,
    DstEu, // last Sun Mar .. last Sun Oct (Europe)
    DstUs, // 2nd Sun Mar .. 1st Sun Nov (North America)
    DstAu, // 1st Sun Oct .. 1st Sun Apr (SE Australia)
    DstNz, // last Sun Sep .. 1st Sun Apr (New Zealand)
} DstRule;

typedef struct {
    const char* std; // standard-time abbreviation, e.g. "CET"
    const char* dst; // summer-time abbreviation, e.g. "CEST" (NULL if none)
    int16_t std_min; // standard UTC offset in minutes
    DstRule rule;
} TimeZone;

static const TimeZone TIMEZONES[] = {
    {"HST", NULL, -600, DstNone}, // Hawaii
    {"AKST", "AKDT", -540, DstUs}, // Alaska
    {"PST", "PDT", -480, DstUs}, // US Pacific
    {"MST", "MDT", -420, DstUs}, // US Mountain
    {"MST-AZ", NULL, -420, DstNone}, // Arizona (no DST)
    {"CST", "CDT", -360, DstUs}, // US Central
    {"EST", "EDT", -300, DstUs}, // US Eastern
    {"AST", "ADT", -240, DstUs}, // Atlantic Canada
    {"BRT", NULL, -180, DstNone}, // Brazil
    {"UTC", NULL, 0, DstNone},
    {"GMT", "BST", 0, DstEu}, // UK / Ireland
    {"WET", "WEST", 0, DstEu}, // Portugal
    {"CET", "CEST", 60, DstEu}, // Central Europe
    {"EET", "EEST", 120, DstEu}, // Eastern Europe
    {"MSK", NULL, 180, DstNone}, // Moscow
    {"GST", NULL, 240, DstNone}, // Gulf
    {"IST", NULL, 330, DstNone}, // India (+5:30)
    {"ICT", NULL, 420, DstNone}, // Indochina
    {"CN", NULL, 480, DstNone}, // China Standard Time
    {"JST", NULL, 540, DstNone}, // Japan
    {"AEST", "AEDT", 600, DstAu}, // SE Australia
    {"NZST", "NZDT", 720, DstNz}, // New Zealand
};
#define TZ_COUNT ((int)(sizeof(TIMEZONES) / sizeof(TIMEZONES[0])))
#define TZ_UTC_INDEX 9 // index of "UTC" above - the safe default
#define TZ_VISIBLE 4

static int tz_days_in_month(int y, int m) {
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if(m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

// 0=Sunday .. 6=Saturday for a calendar date (1970-01-01 was a Thursday).
static int tz_day_of_week(int y, int m, int d) {
    DateTime dt = {0};
    dt.year = (uint16_t)y;
    dt.month = (uint8_t)m;
    dt.day = (uint8_t)d;
    dt.hour = 12;
    uint32_t ts = datetime_datetime_to_timestamp(&dt);
    return (int)((ts / 86400u + 4u) % 7u);
}

static int tz_last_sunday(int y, int m) {
    int dim = tz_days_in_month(y, m);
    return dim - tz_day_of_week(y, m, dim);
}

static int tz_nth_sunday(int y, int m, int n) {
    int dow1 = tz_day_of_week(y, m, 1);
    int first = 1 + ((7 - dow1) % 7);
    return first + (n - 1) * 7;
}

// Is summer time in effect for this rule on the given local date? Transitions
// are resolved at day granularity, which is plenty for a solar calculator.
static bool tz_dst_active(DstRule rule, int y, int m, int d) {
    switch(rule) {
    case DstEu:
        if(m < 3 || m > 10) return false;
        if(m > 3 && m < 10) return true;
        if(m == 3) return d >= tz_last_sunday(y, 3);
        return d < tz_last_sunday(y, 10);
    case DstUs:
        if(m < 3 || m > 11) return false;
        if(m > 3 && m < 11) return true;
        if(m == 3) return d >= tz_nth_sunday(y, 3, 2);
        return d < tz_nth_sunday(y, 11, 1);
    case DstAu: // southern-hemisphere summer straddles the new year
        if(m == 4) return d < tz_nth_sunday(y, 4, 1);
        if(m == 10) return d >= tz_nth_sunday(y, 10, 1);
        return (m < 4 || m > 10);
    case DstNz:
        if(m == 4) return d < tz_nth_sunday(y, 4, 1);
        if(m == 9) return d >= tz_last_sunday(y, 9);
        return (m < 4 || m > 9);
    case DstNone:
    default:
        return false;
    }
}

// True if the timezone at index is currently on summer time (per the RTC date).
static bool tz_is_dst_now(int index) {
    if(index < 0 || index >= TZ_COUNT) return false;
    const TimeZone* tz = &TIMEZONES[index];
    if(tz->rule == DstNone || tz->dst == NULL) return false;
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return tz_dst_active(tz->rule, dt.year, dt.month, dt.day);
}

// Current effective UTC offset (hours) for the timezone at index.
static float tz_current_offset(int index) {
    if(index < 0 || index >= TZ_COUNT) index = TZ_UTC_INDEX;
    float off = TIMEZONES[index].std_min / 60.0f;
    if(tz_is_dst_now(index)) off += 1.0f;
    return off;
}

// Current abbreviation (standard or summer) for the timezone at index.
static const char* tz_current_abbr(int index) {
    if(index < 0 || index >= TZ_COUNT) index = TZ_UTC_INDEX;
    return tz_is_dst_now(index) ? TIMEZONES[index].dst : TIMEZONES[index].std;
}

// Index of a timezone by its standard abbreviation, or UTC if not found.
static int tz_index_by_name(const char* name) {
    for(int i = 0; i < TZ_COUNT; i++) {
        if(strcmp(TIMEZONES[i].std, name) == 0) return i;
    }
    return TZ_UTC_INDEX;
}

typedef enum {
    KeypadFieldLatitude,
    KeypadFieldLongitude,
    KeypadFieldTimezone,
} KeypadField;

typedef struct {
    FuriMutex* mutex;
    int page;
    int lang_index;
    int lang_selection;
    bool language_confirmed;
    Location location;
    AppScreen screen;
    ViewPort* view_port;

    // Boot splash: shown on every launch, then auto-advances to post_splash.
    uint32_t splash_start; // tick when the splash appeared
    AppScreen post_splash; // screen to enter once the splash finishes
    bool splash_auto; // false: wait for OK; true: auto-advance after SPLASH_MS
    int settings_sel; // selected row on the Settings page (SETTINGS_*)
    int location_sel; // selected row on the Location page (LOCATION_*)

    // Location entry keypad (ScreenLocationKeypad)
    KeypadField keypad_field;
    char keypad_buffer[KEYPAD_BUFFER_SIZE];
    int keypad_row;
    int keypad_col;
    bool keypad_error;
    bool keypad_mandatory;
    bool wizard_tz; // keypad's longitude step continues into the timezone picker
    Location keypad_location;

    // Timezone picker (ScreenTimezonePicker).
    int tz_sel;

    // Pre-rendered QR code of APP_GITHUB_URL, computed once at startup.
    uint8_t qr_data[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    int qr_size; // module count per side, 0 if unavailable

    // NFC sharing: an emulated NTAG213 carrying the GitHub URL as NDEF.
    Nfc* nfc;
    MfUltralightData* nfc_data;
    NfcListener* nfc_listener; // non-NULL while emulating
} SolarApp;

//======================================================================================================================
// Settings persistence (APP_DATA_PATH, writable per-app SD folder)
//======================================================================================================================

// lang_index may be -1 to mean "no language chosen" (used by the language
// reset action); it is stored as a uint32 sentinel out of LANGUAGE_COUNT's
// range, which load_settings() below correctly rejects.
static void
    save_settings(Storage* storage, int lang_index, const Location* location, bool splash_auto) {
    FuriString* path = furi_string_alloc_printf(APP_DATA_PATH("settings.txt"));
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    do {
        if(!flipper_format_file_open_always(ff, furi_string_get_cstr(path))) break;
        if(!flipper_format_write_header_cstr(ff, "Solar Calculator Settings", 1)) break;
        uint32_t lang = (uint32_t)lang_index;
        flipper_format_write_uint32(ff, "Language", &lang, 1);
        float lat = location->latitude;
        float lon = location->longitude;
        flipper_format_write_float(ff, "Latitude", &lat, 1);
        flipper_format_write_float(ff, "Longitude", &lon, 1);
        int tzi = location->tz_index;
        if(tzi < 0 || tzi >= TZ_COUNT) tzi = TZ_UTC_INDEX;
        flipper_format_write_string_cstr(ff, "Timezone", TIMEZONES[tzi].std);
        uint32_t splash = splash_auto ? 1u : 0u;
        flipper_format_write_uint32(ff, "SplashAuto", &splash, 1);
    } while(false);
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_string_free(path);
}

static void load_settings(
    Storage* storage,
    int* out_lang_index,
    bool* out_have_lang,
    Location* out_location,
    bool* out_have_location,
    bool* out_splash_auto) {
    *out_have_lang = false;
    *out_have_location = false;
    *out_splash_auto = false; // default: splash waits for OK (first-run behaviour)

    FuriString* path = furi_string_alloc_printf(APP_DATA_PATH("settings.txt"));
    storage_common_resolve_path_and_ensure_app_directory(storage, path);

    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* filetype = furi_string_alloc();
    do {
        if(!flipper_format_file_open_existing(ff, furi_string_get_cstr(path))) break;
        uint32_t version = 0;
        if(!flipper_format_read_header(ff, filetype, &version)) break;

        uint32_t lang = 0;
        if(flipper_format_read_uint32(ff, "Language", &lang, 1) && lang < LANGUAGE_COUNT) {
            *out_lang_index = (int)lang;
            *out_have_lang = true;
        }

        float lat = 0, lon = 0;
        bool have_lat = flipper_format_read_float(ff, "Latitude", &lat, 1);
        bool have_lon = flipper_format_read_float(ff, "Longitude", &lon, 1);
        if(have_lat && have_lon) {
            out_location->latitude = lat;
            out_location->longitude = lon;
            // Named timezone (falls back to UTC when absent, e.g. old settings).
            FuriString* tzname = furi_string_alloc();
            if(flipper_format_read_string(ff, "Timezone", tzname)) {
                out_location->tz_index = tz_index_by_name(furi_string_get_cstr(tzname));
            } else {
                out_location->tz_index = TZ_UTC_INDEX;
            }
            furi_string_free(tzname);
            *out_have_location = true;
        }

        uint32_t splash = 0;
        if(flipper_format_read_uint32(ff, "SplashAuto", &splash, 1)) {
            *out_splash_auto = (splash != 0);
        }
    } while(false);
    furi_string_free(filetype);
    flipper_format_file_close(ff);
    flipper_format_free(ff);
    furi_string_free(path);
}

// Asks the loader to relaunch this .fap after the current process exits.
static void request_app_restart(void) {
    Loader* loader = furi_record_open(RECORD_LOADER);
    FuriString* path = furi_string_alloc();
    if(!loader_get_application_launch_path(loader, path)) {
        furi_string_set_str(path, "solar_calculator");
    }
    loader_enqueue_launch(loader, furi_string_get_cstr(path), NULL, LoaderDeferredLaunchFlagNone);
    furi_string_free(path);
    furi_record_close(RECORD_LOADER);
}

//======================================================================================================================
// Location entry keypad (custom on-screen numeric pad: 0-9 . - Del Save)
//======================================================================================================================

typedef struct {
    int cols;
    const char* labels[KEYPAD_MAX_COLS];
} KeypadRow;

static const KeypadRow KEYPAD_LAYOUT[KEYPAD_ROWS] = {
    {4, {"1", "2", "3", "Del"}},
    {4, {"4", "5", "6", "-"}},
    {4, {"7", "8", "9", "."}},
    {2, {"0", "Save"}},
};

static int keypad_row_cols(int row) {
    return KEYPAD_LAYOUT[row].cols;
}

static const char* keypad_label(int row, int col) {
    return KEYPAD_LAYOUT[row].labels[col];
}

static void keypad_field_range(KeypadField field, float* min, float* max) {
    if(field == KeypadFieldLatitude) {
        *min = -90.0f;
        *max = 90.0f;
    } else if(field == KeypadFieldLongitude) {
        *min = -180.0f;
        *max = 180.0f;
    } else {
        *min = -12.0f;
        *max = 14.0f;
    }
}

static bool keypad_parse(const char* text, float min, float max, float* out) {
    if(text[0] == '\0') return false;
    char* endptr = NULL;
    float value = strtof(text, &endptr);
    if(endptr == text || *endptr != '\0' || value < min || value > max) return false;
    *out = value;
    return true;
}

static const char* keypad_header(const LangStrings* s, KeypadField field) {
    if(field == KeypadFieldLatitude) return s->wizard_lat_header;
    if(field == KeypadFieldLongitude) return s->wizard_lon_header;
    return s->wizard_tz_header;
}

//======================================================================================================================
// Time helpers
//======================================================================================================================

// Current UTC unix timestamp, derived from the Flipper's local-time RTC and
// the user-entered UTC offset (fractional hours).
static uint32_t get_utc_timestamp(float tz_offset) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    uint32_t local_as_utc = datetime_datetime_to_timestamp(&dt);
    int64_t offset_seconds = (int64_t)(tz_offset * 3600.0f);
    return (uint32_t)((int64_t)local_as_utc - offset_seconds);
}

// Formats a UTC hour-of-day value (may be outside [0,24) or NaN) as local
// HH:MM, applying the timezone offset and wrapping into [0,24).
static void format_local_time(float utc_hours, float tz_offset, char* out, size_t out_size) {
    if(isnan(utc_hours)) {
        snprintf(out, out_size, "--:--");
        return;
    }
    float local = fmodf(utc_hours + tz_offset, 24.0f);
    if(local < 0) local += 24.0f;
    int h = (int)local;
    int m = (int)roundf((local - h) * 60.0f);
    if(m == 60) {
        m = 0;
        h = (h + 1) % 24;
    }
    snprintf(out, out_size, "%02d:%02d", h, m);
}

// Formats a UTC timestamp as a local "DD-MM-YYYY HH:MM" string (or "--" if
// unset).
static void format_moon_date(uint32_t utc, float tz_offset, char* out, size_t out_size) {
    if(utc == 0) {
        snprintf(out, out_size, "--");
        return;
    }
    int64_t local = (int64_t)utc + (int64_t)(tz_offset * 3600.0f);
    DateTime dt;
    datetime_timestamp_to_datetime((uint32_t)local, &dt);
    snprintf(
        out, out_size, "%02u-%02u-%04u %02u:%02u", dt.day, dt.month, dt.year, dt.hour, dt.minute);
}

//======================================================================================================================
// Page content
//======================================================================================================================

// Builds a moon-event label like "New moon in 4 days" / "New moon in 1 day"
// into out (LINE_LENGTH bytes). Falls back to just the name if event is unset.
static void build_moon_event_label(
    const LangStrings* s,
    const char* name,
    uint32_t now,
    uint32_t event,
    char* out) {
    if(event == 0) {
        snprintf(out, LINE_LENGTH, "%s", name);
        return;
    }
    int days = (int)lroundf((float)(event - now) / 86400.0f);
    if(days < 1) days = 1;
    const char* unit = (days == 1) ? s->moon_day_one : s->moon_day_many;
    if(s->moon_conn[0] == '\0') {
        snprintf(out, LINE_LENGTH, "%s %d %s", name, days, unit);
    } else {
        snprintf(out, LINE_LENGTH, "%s %s %d %s", name, s->moon_conn, days, unit);
    }
}

static size_t build_page_lines(
    const LangStrings* s,
    const Location* loc,
    int page,
    char lines[MAX_LINES][LINE_LENGTH],
    char values[MAX_LINES][LINE_LENGTH]) {
    size_t n = 0;
    for(size_t i = 0; i < MAX_LINES; i++) {
        values[i][0] = '\0';
    }

    // Effective UTC offset (hours) of the selected named timezone right now,
    // including automatic summer time.
    float off = tz_current_offset(loc->tz_index);

    if(page == PAGE_SUN) {
        uint32_t utc = get_utc_timestamp(off);
        JulianDay jd = julian_day_from_utc(utc);
        float transit, sunrise, sunset, eot;
        calc_sunrise_sunset(
            jd, loc->latitude, loc->longitude, &transit, &sunrise, &sunset, SUNRISESET_STD_ALTITUDE);
        calc_equation_of_time(jd, &eot);

        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_sunrise);
        format_local_time(sunrise, off, values[n], LINE_LENGTH);
        n++;
        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_transit);
        format_local_time(transit, off, values[n], LINE_LENGTH);
        n++;
        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_sunset);
        format_local_time(sunset, off, values[n], LINE_LENGTH);
        n++;
        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_eot);
        snprintf(values[n], LINE_LENGTH, "%+.1f min", (double)eot);
        n++;
    } else if(page == PAGE_TWILIGHT) {
        uint32_t utc = get_utc_timestamp(off);
        JulianDay jd = julian_day_from_utc(utc);

        struct {
            const char* label;
            float altitude;
        } rows[3] = {
            {s->lbl_civil, CIVIL_DAWNDUSK_STD_ALTITUDE},
            {s->lbl_nautical, NAUTICAL_DAWNDUSK_STD_ALTITUDE},
            {s->lbl_astronomical, ASTRONOMICAL_DAWNDUSK_STD_ALTITUDE},
        };

        for(int i = 0; i < 3; i++) {
            float transit, dawn, dusk;
            calc_sunrise_sunset(
                jd, loc->latitude, loc->longitude, &transit, &dawn, &dusk, rows[i].altitude);

            snprintf(lines[n], LINE_LENGTH, "%s", rows[i].label);
            if(isnan(dawn) || isnan(dusk)) {
                snprintf(values[n], LINE_LENGTH, "%s", s->msg_circumpolar);
            } else {
                char dawn_str[8], dusk_str[8];
                format_local_time(dawn, off, dawn_str, sizeof(dawn_str));
                format_local_time(dusk, off, dusk_str, sizeof(dusk_str));
                snprintf(values[n], LINE_LENGTH, "%s-%s", dawn_str, dusk_str);
            }
            n++;
        }
    } else if(page == PAGE_POSITION) {
        uint32_t utc = get_utc_timestamp(off);
        JulianDay jd = julian_day_from_utc(utc);
        float azimuth, elevation;
        calc_horizontal_coordinates(jd, loc->latitude, loc->longitude, &azimuth, &elevation);

        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_azimuth);
        snprintf(values[n], LINE_LENGTH, "%.1f %s", (double)azimuth, s->lbl_degrees);
        n++;
        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_elevation);
        snprintf(values[n], LINE_LENGTH, "%.1f %s", (double)elevation, s->lbl_degrees);
        n++;
    } else if(page == PAGE_MOON) {
        uint32_t utc = get_utc_timestamp(off);
        JulianDay jd = julian_day_from_utc(utc);
        MoonPhase mp;
        calc_moon_phase(jd, &mp);

        const char* names[8] = {
            s->moon_new,
            s->moon_waxing_crescent,
            s->moon_first_quarter,
            s->moon_waxing_gibbous,
            s->moon_full,
            s->moon_waning_gibbous,
            s->moon_last_quarter,
            s->moon_waning_crescent,
        };
        // Phase name on its own line (no right-hand value), so long names fit.
        snprintf(lines[n++], LINE_LENGTH, "%s", names[moon_phase_index(mp.elongation)]);

        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_illumination);
        snprintf(values[n], LINE_LENGTH, "%d%%", (int)roundf(mp.illumination * 100.0f));
        n++;
        snprintf(lines[n], LINE_LENGTH, "%s", s->lbl_moon_age);
        snprintf(values[n], LINE_LENGTH, "%.1f d", (double)mp.age_days);
        n++;
    } else if(page == PAGE_MOON_EVENTS) {
        uint32_t utc = get_utc_timestamp(off);
        char buf[24];

        // Countdown label ("New moon in 4 days") on its own proportional line,
        // then the date on the next line in monospace (see draw_info_screen)
        // so both dates line up.
        uint32_t new_utc = moon_next_phase(utc, 0.0f);
        build_moon_event_label(s, s->moon_new, utc, new_utc, lines[n++]);
        format_moon_date(new_utc, off, buf, sizeof(buf));
        snprintf(lines[n++], LINE_LENGTH, "%s", buf);

        uint32_t full_utc = moon_next_phase(utc, 180.0f);
        build_moon_event_label(s, s->moon_full, utc, full_utc, lines[n++]);
        format_moon_date(full_utc, off, buf, sizeof(buf));
        snprintf(lines[n++], LINE_LENGTH, "%s", buf);
        // Location (PAGE_LOCATION) and Settings (PAGE_SETTINGS) have their own
        // draw functions, not this one.
    } else if(page == PAGE_ABOUT) {
        DateTime datetime;
        furi_hal_rtc_get_datetime(&datetime);

        snprintf(lines[n], LINE_LENGTH, "%s", s->about_version_label);
        snprintf(values[n], LINE_LENGTH, "Solar Calc. v%s", APP_VERSION);
        n++;

        snprintf(lines[n], LINE_LENGTH, "%s", s->about_author_label);
        snprintf(values[n], LINE_LENGTH, "%s", APP_AUTHOR);
        n++;

        snprintf(lines[n], LINE_LENGTH, "Copyright:");
        if(datetime.year > APP_START_YEAR) {
            snprintf(values[n], LINE_LENGTH, "%u-%u", APP_START_YEAR, datetime.year);
        } else {
            snprintf(values[n], LINE_LENGTH, "%u", APP_START_YEAR);
        }
        n++;

        snprintf(lines[n], LINE_LENGTH, "%s", s->about_license_label);
        snprintf(values[n], LINE_LENGTH, "%s", APP_LICENSE);
        n++;
        // about_hint is drawn as a footer in draw_info_screen.
    }

    return n;
}

//======================================================================================================================
// Drawing
//======================================================================================================================

// Boot splash: the "Sun & Moon Lab / Solaris Calc" title card with an orbital
// sun-moon-planet graphic and today's date, shown for a couple of seconds on
// every launch before the app auto-advances to its first screen. The date uses
// the app language's month names and the full year.
static void draw_splash_screen(Canvas* canvas, const LangStrings* s) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 9, AlignCenter, AlignBottom, "SUN & MOON LAB");

    char subtitle[32];
    snprintf(subtitle, sizeof(subtitle), "SOLARIS CALC v%s", APP_VERSION);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 18, AlignCenter, AlignBottom, subtitle);

    const int32_t cx = SCREEN_W / 2;
    const int32_t cy = 33;
    const float rx = 44.0f;
    const float ry = 10.0f;

    // Dotted orbit ellipse (the canvas has no ellipse primitive).
    for(int a = 0; a < 360; a += 8) {
        float r = a * SOLAR_PI / 180.0f;
        canvas_draw_dot(canvas, cx + (int)(rx * cosf(r)), cy + (int)(ry * sinf(r)));
    }

    // Sun at the centre with eight radiating rays.
    canvas_draw_disc(canvas, cx, cy, 6);
    for(int k = 0; k < 8; k++) {
        float r = k * 45.0f * SOLAR_PI / 180.0f;
        float c = cosf(r), s = sinf(r);
        canvas_draw_line(
            canvas, cx + (int)(8 * c), cy + (int)(8 * s), cx + (int)(11 * c), cy + (int)(11 * s));
    }

    // A planet (filled) on the right of the orbit, a moon (outline) on the left.
    canvas_draw_disc(canvas, cx + 40, cy + 4, 2);
    canvas_draw_circle(canvas, cx - 40, cy - 4, 3);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 54, AlignCenter, AlignBottom, "CALCULATOR READY");

    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    int mon = (dt.month >= 1 && dt.month <= 12) ? dt.month - 1 : 0;
    char date[24];
    snprintf(date, sizeof(date), "%u %s %u", dt.day, s->months[mon], dt.year);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, SCREEN_W / 2, 63, AlignCenter, AlignBottom, date);
}

static void draw_info_screen(Canvas* canvas, const LangStrings* s, const Location* loc, int page) {
    char lines[MAX_LINES][LINE_LENGTH];
    char values[MAX_LINES][LINE_LENGTH];
    size_t line_count = build_page_lines(s, loc, page, lines, values);

    const char* titles[PAGE_COUNT] = {
        s->title_sun,
        s->title_twilight,
        s->title_position,
        s->title_moon,
        s->title_moon_events,
        s->title_location,
        s->title_settings,
        s->title_about,
    };

    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, titles[page]);
    if(page != PAGE_ABOUT) {
        canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);
    }

    uint32_t y = page == PAGE_ABOUT ? 20 : 24;
    uint32_t y_step = page == PAGE_ABOUT ? 9 : 10;
    // On the Sun and Twilight pages the right-hand values (times) are drawn in
    // the monospace keyboard font so the digits line up; labels stay proportional.
    bool mono_values = (page == PAGE_SUN || page == PAGE_TWILIGHT);
    for(size_t i = 0; i < line_count; i++) {
        if(page == PAGE_MOON_EVENTS) {
            // Even lines are labels (proportional); odd lines are dates, drawn
            // in the monospace keyboard font (device_info convention) so the two
            // dates line up under each other.
            canvas_set_font(canvas, (i % 2 == 1) ? FontKeyboard : FontSecondary);
            canvas_draw_str(canvas, 2, y, lines[i]);
        } else {
            canvas_set_font(canvas, FontSecondary);
            canvas_draw_str(canvas, 2, y, lines[i]);
            if(values[i][0] != '\0') {
                if(mono_values) canvas_set_font(canvas, FontKeyboard);
                canvas_draw_str_aligned(
                    canvas, SCREEN_W - 2, y, AlignRight, AlignBottom, values[i]);
            }
        }
        y += y_step;
    }

    // Footer line: the "OK = ..." action hint bottom-left, page indicator
    // bottom-right, sharing the last row.
    const char* hint = NULL;
    if(page == PAGE_ABOUT) hint = s->about_hint;

    canvas_set_font(canvas, FontSecondary);
    if(hint) {
        canvas_draw_str_aligned(canvas, 2, SCREEN_H - 2, AlignLeft, AlignBottom, hint);
    }

    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", page + 1, PAGE_COUNT);
    canvas_draw_str_aligned(
        canvas, SCREEN_W - 2, SCREEN_H - 2, AlignRight, AlignBottom, page_indicator);
}

// Combined Settings page: a two-row menu (splash mode + reset). Left/Right move
// the selection cursor, OK activates the highlighted row (SETTINGS_*).
static void draw_settings_screen(Canvas* canvas, const LangStrings* s, bool splash_auto, int sel) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, s->title_settings);
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);

    // Row 0: splash mode (label left, current value right).
    canvas_draw_str(canvas, 10, 28, s->lbl_splash_mode);
    canvas_draw_str_aligned(
        canvas,
        SCREEN_W - 2,
        28,
        AlignRight,
        AlignBottom,
        splash_auto ? s->splash_mode_auto : s->splash_mode_ok);

    // Row 1: reset.
    canvas_draw_str(canvas, 10, 40, s->title_reset);

    // Selection cursor on the active row.
    canvas_draw_str(canvas, 2, sel == SETTINGS_RESET ? 40 : 28, ">");

    // Footer: hint for the selected row bottom-left, page indicator right.
    const char* hint = (sel == SETTINGS_RESET) ? s->reset_hint : s->splash_hint;
    canvas_draw_str_aligned(canvas, 2, SCREEN_H - 2, AlignLeft, AlignBottom, hint);

    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", PAGE_SETTINGS + 1, PAGE_COUNT);
    canvas_draw_str_aligned(
        canvas, SCREEN_W - 2, SCREEN_H - 2, AlignRight, AlignBottom, page_indicator);
}

// Location page: a two-row menu - edit the coordinates, or pick the timezone.
// Left/Right move the cursor, OK opens the selected editor (LOCATION_*).
static void
    draw_location_screen(Canvas* canvas, const LangStrings* s, const Location* loc, int sel) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, s->title_location);
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);

    // Row 0: coordinates (latitude, longitude summary).
    char coords[24];
    snprintf(coords, sizeof(coords), "%.2f,%.2f", (double)loc->latitude, (double)loc->longitude);
    canvas_draw_str(canvas, 10, 28, s->lbl_coordinates);

    // Row 1: timezone (current abbreviation + effective offset, incl. DST).
    char tz[24];
    snprintf(
        tz,
        sizeof(tz),
        "%s %+.1f",
        tz_current_abbr(loc->tz_index),
        (double)tz_current_offset(loc->tz_index));
    canvas_draw_str(canvas, 10, 40, s->lbl_timezone);

    // Right-hand values in the monospace font so the digits line up.
    canvas_set_font(canvas, FontKeyboard);
    canvas_draw_str_aligned(canvas, SCREEN_W - 2, 28, AlignRight, AlignBottom, coords);
    canvas_draw_str_aligned(canvas, SCREEN_W - 2, 40, AlignRight, AlignBottom, tz);
    canvas_set_font(canvas, FontSecondary);

    // Selection cursor on the active row.
    canvas_draw_str(canvas, 2, sel == LOCATION_TZ ? 40 : 28, ">");

    canvas_draw_str_aligned(canvas, 2, SCREEN_H - 2, AlignLeft, AlignBottom, s->location_hint);

    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", PAGE_LOCATION + 1, PAGE_COUNT);
    canvas_draw_str_aligned(
        canvas, SCREEN_W - 2, SCREEN_H - 2, AlignRight, AlignBottom, page_indicator);
}

static void draw_language_picker(Canvas* canvas, int selection, int active) {
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, LANGUAGES[selection]->title_language);
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);

    int scroll = 0;
    if(selection >= LANGUAGE_VISIBLE) scroll = selection - LANGUAGE_VISIBLE + 1;
    if(scroll > (int)LANGUAGE_COUNT - LANGUAGE_VISIBLE) scroll = (int)LANGUAGE_COUNT - LANGUAGE_VISIBLE;
    if(scroll < 0) scroll = 0;

    uint32_t y = 24;
    for(int i = scroll; i < scroll + LANGUAGE_VISIBLE && i < (int)LANGUAGE_COUNT; i++) {
        char line[LINE_LENGTH];
        snprintf(
            line,
            LINE_LENGTH,
            "%s%s%s",
            i == selection ? "> " : "  ",
            LANGUAGES[i]->lang_name,
            i == active ? " *" : "");
        canvas_draw_str(canvas, 2, y, line);
        y += 10;
    }
}

// Timezone picker: a scrollable list of UTC offsets, reached after longitude
// entry. The cursor starts on the proposed default (the current setting).
static void draw_timezone_picker(Canvas* canvas, const LangStrings* s, int selection) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, s->wizard_tz_header);
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);

    int scroll = 0;
    if(selection >= TZ_VISIBLE) scroll = selection - TZ_VISIBLE + 1;
    if(scroll > TZ_COUNT - TZ_VISIBLE) scroll = TZ_COUNT - TZ_VISIBLE;
    if(scroll < 0) scroll = 0;

    uint32_t y = 24;
    for(int i = scroll; i < scroll + TZ_VISIBLE && i < TZ_COUNT; i++) {
        char line[32];
        float std = TIMEZONES[i].std_min / 60.0f;
        // Show the zone name, its standard UTC offset, and a "+DST" marker for
        // zones that switch to summer time.
        snprintf(
            line,
            sizeof(line),
            "%s %-6s UTC%+.1f%s",
            i == selection ? ">" : " ",
            TIMEZONES[i].std,
            (double)std,
            TIMEZONES[i].dst ? " +DST" : "");
        canvas_draw_str(canvas, 2, y, line);
        y += 10;
    }
}

static void draw_location_keypad(
    Canvas* canvas,
    const LangStrings* s,
    KeypadField field,
    const char* buffer,
    bool error,
    int cursor_row,
    int cursor_col) {
    canvas_clear(canvas);

    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, keypad_header(s, field));
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);
    if(error) {
        canvas_draw_str_aligned(canvas, SCREEN_W / 2, 21, AlignCenter, AlignBottom, s->wizard_invalid);
    } else {
        canvas_draw_str_aligned(
            canvas,
            SCREEN_W / 2,
            21,
            AlignCenter,
            AlignBottom,
            buffer[0] ? buffer : "0");
    }

    const int32_t grid_top = 23;
    const int32_t row_h = (SCREEN_H - grid_top) / KEYPAD_ROWS;

    for(int row = 0; row < KEYPAD_ROWS; row++) {
        int cols = keypad_row_cols(row);
        int32_t col_w = SCREEN_W / cols;
        for(int col = 0; col < cols; col++) {
            int32_t x = col * col_w;
            int32_t y = grid_top + row * row_h;
            bool selected = (row == cursor_row && col == cursor_col);

            canvas_set_color(canvas, ColorBlack);
            if(selected) {
                canvas_draw_box(canvas, x, y, col_w, row_h);
                canvas_set_color(canvas, ColorWhite);
            } else {
                canvas_draw_frame(canvas, x, y, col_w, row_h);
            }
            const char* label = keypad_label(row, col);
            int32_t cx = x + col_w / 2;
            int32_t cy = y + row_h / 2;
            if(strcmp(label, "Del") == 0) {
                // Draw a backspace (left-arrow) icon instead of the "Del" text.
                canvas_draw_line(canvas, cx - 5, cy, cx + 5, cy);
                canvas_draw_line(canvas, cx - 5, cy, cx - 1, cy - 4);
                canvas_draw_line(canvas, cx - 5, cy, cx - 1, cy + 4);
            } else if(strcmp(label, "Save") == 0) {
                // "Save" is an internal id; show the localized word.
                canvas_draw_str_aligned(canvas, cx, cy, AlignCenter, AlignCenter, s->kbd_save);
            } else {
                canvas_draw_str_aligned(canvas, cx, cy, AlignCenter, AlignCenter, label);
            }
        }
    }
    canvas_set_color(canvas, ColorBlack);
}

// Draws text word-wrapped within max_w, starting at (x, y) and advancing by
// line_h per line. Returns the y just past the last drawn line.
static int draw_wrapped_text(
    Canvas* canvas,
    int x,
    int y,
    int max_w,
    int line_h,
    const char* text) {
    char line[64];
    size_t line_len = 0;
    line[0] = '\0';
    uint16_t space_w = canvas_string_width(canvas, " ");
    const char* p = text;
    while(*p) {
        while(*p == ' ') p++;
        const char* start = p;
        while(*p && *p != ' ') p++;
        size_t wlen = (size_t)(p - start);
        if(wlen == 0) break;
        if(wlen > 30) wlen = 30;
        char word[31];
        memcpy(word, start, wlen);
        word[wlen] = '\0';

        uint16_t word_w = canvas_string_width(canvas, word);
        size_t need = line_len + (line_len ? 1 : 0) + wlen;
        bool overflow = line_len != 0 &&
                        (need >= sizeof(line) ||
                         (int)(canvas_string_width(canvas, line) + space_w + word_w) > max_w);
        if(overflow) {
            canvas_draw_str(canvas, x, y, line);
            y += line_h;
            memcpy(line, word, wlen);
            line[wlen] = '\0';
            line_len = wlen;
        } else {
            if(line_len != 0) line[line_len++] = ' ';
            memcpy(line + line_len, word, wlen);
            line_len += wlen;
            line[line_len] = '\0';
        }
    }
    if(line_len != 0) {
        canvas_draw_str(canvas, x, y, line);
        y += line_h;
    }
    return y;
}

// QR page: the code on the left, a localized call-to-action and the page
// indicator on the right. qr_data is the pre-rendered module bitmap, qr_size
// its side length.
static void draw_qr_screen(
    Canvas* canvas,
    const LangStrings* s,
    const uint8_t* qr_data,
    int qr_size) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    if(qr_size <= 0) {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str(canvas, 2, 20, "QR unavailable");
        return;
    }

    int scale = SCREEN_H / qr_size;
    if(scale < 1) scale = 1;
    int qpx = qr_size * scale;
    int ox = 2;
    int oy = (SCREEN_H - qpx) / 2;

    // White quiet zone behind the code so it stays scannable.
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, ox - 1, oy - 1, qpx + 2, qpx + 2);
    canvas_set_color(canvas, ColorBlack);
    for(int y = 0; y < qr_size; y++) {
        for(int x = 0; x < qr_size; x++) {
            if(qrcodegen_getModule(qr_data, x, y)) {
                canvas_draw_box(canvas, ox + x * scale, oy + y * scale, scale, scale);
            }
        }
    }

    int tx = ox + qpx + 4;
    int tw = SCREEN_W - tx - 2;
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, tx, 12, "GitHub");
    canvas_set_font(canvas, FontSecondary);
    draw_wrapped_text(canvas, tx, 24, tw, 10, s->qr_cta);

    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", PAGE_QR + 1, PAGE_COUNT);
    canvas_draw_str_aligned(
        canvas, SCREEN_W - 2, SCREEN_H - 2, AlignRight, AlignBottom, page_indicator);
}

// NFC page: a localized explanation while the NTAG emulation runs.
static void draw_nfc_screen(Canvas* canvas, const LangStrings* s) {
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, "NFC");
    canvas_draw_line(canvas, 0, 13, SCREEN_W, 13);

    canvas_set_font(canvas, FontSecondary);
    draw_wrapped_text(canvas, 2, 25, SCREEN_W - 4, 11, s->nfc_desc);

    char page_indicator[16];
    snprintf(page_indicator, sizeof(page_indicator), "%d/%d", PAGE_NFC + 1, PAGE_COUNT);
    canvas_draw_str_aligned(
        canvas, SCREEN_W - 2, SCREEN_H - 2, AlignRight, AlignBottom, page_indicator);
}

static void draw_callback(Canvas* canvas, void* context) {
    SolarApp* app = context;

    AppScreen screen;
    int page, lang_index, lang_selection;
    Location loc;
    KeypadField keypad_field;
    char keypad_buffer[KEYPAD_BUFFER_SIZE];
    bool keypad_error;
    int keypad_row, keypad_col;
    int qr_size;
    bool splash_auto;
    int settings_sel;
    int location_sel;
    int tz_sel;
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    screen = app->screen;
    page = app->page;
    splash_auto = app->splash_auto;
    settings_sel = app->settings_sel;
    location_sel = app->location_sel;
    tz_sel = app->tz_sel;
    lang_index = app->lang_index;
    lang_selection = app->lang_selection;
    loc = app->location;
    keypad_field = app->keypad_field;
    memcpy(keypad_buffer, app->keypad_buffer, sizeof(keypad_buffer));
    keypad_error = app->keypad_error;
    keypad_row = app->keypad_row;
    keypad_col = app->keypad_col;
    qr_size = app->qr_size;
    furi_mutex_release(app->mutex);

    if(screen == ScreenSplash) {
        draw_splash_screen(canvas, LANGUAGES[lang_index]);
    } else if(screen == ScreenLanguagePicker) {
        draw_language_picker(canvas, lang_selection, lang_index);
    } else if(screen == ScreenLocationKeypad) {
        draw_location_keypad(
            canvas,
            LANGUAGES[lang_index],
            keypad_field,
            keypad_buffer,
            keypad_error,
            keypad_row,
            keypad_col);
    } else if(screen == ScreenTimezonePicker) {
        draw_timezone_picker(canvas, LANGUAGES[lang_index], tz_sel);
    } else if(page == PAGE_QR) {
        draw_qr_screen(canvas, LANGUAGES[lang_index], app->qr_data, qr_size);
    } else if(page == PAGE_NFC) {
        draw_nfc_screen(canvas, LANGUAGES[lang_index]);
    } else if(page == PAGE_LOCATION) {
        draw_location_screen(canvas, LANGUAGES[lang_index], &loc, location_sel);
    } else if(page == PAGE_SETTINGS) {
        draw_settings_screen(canvas, LANGUAGES[lang_index], splash_auto, settings_sel);
    } else {
        draw_info_screen(canvas, LANGUAGES[lang_index], &loc, page);
    }
}

static void input_callback(InputEvent* input_event, void* context) {
    FuriMessageQueue* event_queue = context;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

static void timer_callback(void* context) {
    SolarApp* app = context;
    // Auto-advance off the boot splash once it has been up long enough - but
    // only in auto mode; otherwise it waits for the user to press a key.
    furi_mutex_acquire(app->mutex, FuriWaitForever);
    if(app->screen == ScreenSplash && app->splash_auto &&
       (furi_get_tick() - app->splash_start) >= furi_ms_to_ticks(SPLASH_MS)) {
        app->screen = app->post_splash;
    }
    furi_mutex_release(app->mutex);
    view_port_update(app->view_port);
}

//======================================================================================================================
// Entry point
//======================================================================================================================

// Opens the coordinate keypad (latitude first). with_tz=true chains into the
// timezone picker after longitude (the first-run wizard); false commits the
// coordinates on their own (editing coordinates from the Location page).
static void keypad_enter(SolarApp* app, bool mandatory, bool with_tz) {
    app->keypad_field = KeypadFieldLatitude;
    app->keypad_location = app->location;
    snprintf(
        app->keypad_buffer, sizeof(app->keypad_buffer), "%.4f", (double)app->keypad_location.latitude);
    app->keypad_row = 0;
    app->keypad_col = 0;
    app->keypad_error = false;
    app->keypad_mandatory = mandatory;
    app->wizard_tz = with_tz;
    app->screen = ScreenLocationKeypad;
}

//======================================================================================================================
// NFC sharing - emulate an NTAG213 holding an NDEF URI record for APP_GITHUB_URL
//======================================================================================================================

static NfcCommand nfc_listener_callback(NfcGenericEvent event, void* context) {
    UNUSED(event);
    UNUSED(context);
    return NfcCommandContinue;
}

// Builds an NTAG213 in app->nfc_data with an NDEF URI record pointing to
// APP_GITHUB_URL, so a phone tapped to the Flipper is offered the URL.
static void nfc_build_ndef(SolarApp* app) {
    MfUltralightData* data = mf_ultralight_alloc();
    app->nfc_data = data;
    data->type = MfUltralightTypeNTAG213;
    data->pages_total = 45; // NTAG213: pages 0..44
    data->pages_read = 45;

    const MfUltralightVersion ver = {
        .header = 0x00,
        .vendor_id = 0x04,
        .prod_type = 0x04,
        .prod_subtype = 0x02,
        .prod_ver_major = 0x01,
        .prod_ver_minor = 0x00,
        .storage_size = 0x0F,
        .protocol_type = 0x03,
    };
    data->version = ver;

    // ISO14443-3A layer: 7-byte UID, ATQA 0x0044, SAK 0x00 (NTAG).
    const uint8_t uid[7] = {0x04, 0x53, 0x4f, 0x4c, 0x41, 0x52, 0x01};
    iso14443_3a_set_uid(data->iso14443_3a_data, uid, sizeof(uid));
    const uint8_t atqa[2] = {0x44, 0x00};
    iso14443_3a_set_atqa(data->iso14443_3a_data, atqa);
    iso14443_3a_set_sak(data->iso14443_3a_data, 0x00);

    // Pages 0..3: UID + BCC + lock bytes, then the NDEF capability container.
    const uint8_t bcc0 = 0x88 ^ uid[0] ^ uid[1] ^ uid[2];
    const uint8_t bcc1 = uid[3] ^ uid[4] ^ uid[5] ^ uid[6];
    const uint8_t page0[4] = {uid[0], uid[1], uid[2], bcc0};
    const uint8_t page1[4] = {uid[3], uid[4], uid[5], uid[6]};
    const uint8_t page2[4] = {bcc1, 0x48, 0x00, 0x00};
    const uint8_t page3[4] = {0xE1, 0x10, 0x12, 0x00}; // CC: NDEF v1.0, 144 bytes, R/W
    memcpy(data->page[0].data, page0, 4);
    memcpy(data->page[1].data, page1, 4);
    memcpy(data->page[2].data, page2, 4);
    memcpy(data->page[3].data, page3, 4);

    // NDEF message TLV with one URI record, from page 4. URI prefix 0x04 is
    // "https://", so the stored URI omits that scheme.
    const char* uri = APP_GITHUB_URL + 8; // skip "https://"
    const uint8_t uri_len = (uint8_t)strlen(uri);
    uint8_t tlv[64];
    int n = 0;
    tlv[n++] = 0x03; // NDEF message TLV tag
    tlv[n++] = (uint8_t)(uri_len + 5); // TLV length (= NDEF record length)
    tlv[n++] = 0xD1; // record header: MB, ME, SR, TNF = NFC well-known
    tlv[n++] = 0x01; // type length
    tlv[n++] = (uint8_t)(uri_len + 1); // payload length (prefix byte + URI)
    tlv[n++] = 0x55; // record type 'U' (URI)
    tlv[n++] = 0x04; // URI identifier code "https://"
    memcpy(&tlv[n], uri, uri_len);
    n += uri_len;
    tlv[n++] = 0xFE; // terminator TLV
    for(int i = 0; i < n; i++) {
        data->page[4 + i / 4].data[i % 4] = tlv[i];
    }
}

// Starts or stops NTAG emulation. Idempotent.
static void nfc_set_emulating(SolarApp* app, bool on) {
    if(on && app->nfc_listener == NULL) {
        app->nfc_listener = nfc_listener_alloc(app->nfc, NfcProtocolMfUltralight, app->nfc_data);
        nfc_listener_start(app->nfc_listener, nfc_listener_callback, NULL);
    } else if(!on && app->nfc_listener != NULL) {
        nfc_listener_stop(app->nfc_listener);
        nfc_listener_free(app->nfc_listener);
        app->nfc_listener = NULL;
    }
}

// Encodes APP_GITHUB_URL into app->qr_data once at startup.
static void qr_init(SolarApp* app) {
    uint8_t temp[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)];
    bool ok = qrcodegen_encodeText(
        APP_GITHUB_URL,
        temp,
        app->qr_data,
        qrcodegen_Ecc_LOW,
        qrcodegen_VERSION_MIN,
        QR_MAX_VERSION,
        qrcodegen_Mask_AUTO,
        true);
    app->qr_size = ok ? qrcodegen_getSize(app->qr_data) : 0;
}

int32_t solar_calculator_app(void* p) {
    UNUSED(p);

    SolarApp app = {0};
    app.mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool have_lang = false;
    bool have_location = false;
    int saved_lang = 0;
    Location saved_location = {0};
    bool saved_splash_auto = false;
    load_settings(
        storage, &saved_lang, &have_lang, &saved_location, &have_location, &saved_splash_auto);
    furi_record_close(RECORD_STORAGE);

    app.lang_index = have_lang ? saved_lang : 0;
    app.language_confirmed = have_lang;
    app.lang_selection = app.lang_index;
    app.location = have_location ? saved_location : (Location){0.0f, 0.0f, TZ_UTC_INDEX};
    app.splash_auto = saved_splash_auto;
    app.page = PAGE_SUN;
    qr_init(&app);
    app.nfc = nfc_alloc();
    nfc_build_ndef(&app);

    bool location_set = have_location;

    // The boot splash always shows first; post_splash is where we land after it.
    if(!have_lang) {
        app.post_splash = ScreenLanguagePicker;
    } else if(!location_set) {
        // Returning user whose settings file is missing the location half
        // (shouldn't normally happen, but be defensive): ask for it now,
        // using their already-chosen language. keypad_enter prepares the keypad
        // state; we override the screen back to the splash below.
        keypad_enter(&app, true, true);
        app.post_splash = ScreenLocationKeypad;
    } else {
        app.post_splash = ScreenInfo;
    }
    app.screen = ScreenSplash;
    app.splash_start = furi_get_tick();

    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));

    ViewPort* view_port = view_port_alloc();
    app.view_port = view_port;
    view_port_draw_callback_set(view_port, draw_callback, &app);
    view_port_input_callback_set(view_port, input_callback, event_queue);

    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    FuriTimer* timer = furi_timer_alloc(timer_callback, FuriTimerTypePeriodic, &app);
    furi_timer_start(timer, furi_ms_to_ticks(REFRESH_MS));

    InputEvent event;
    bool running = true;
    while(running) {
        FuriStatus status = furi_message_queue_get(event_queue, &event, FuriWaitForever);
        if(status != FuriStatusOk || event.type != InputTypeShort) continue;

        // Deferred I/O: state mutations happen under the mutex, but the actual
        // blocking storage write (and app relaunch) run after releasing it, so
        // the GUI draw thread never blocks waiting on file I/O.
        bool do_save = false;
        bool do_restart = false;
        int save_lang = 0;
        Location save_location = {0};
        bool save_splash = false;

        furi_mutex_acquire(app.mutex, FuriWaitForever);

        if(app.screen == ScreenSplash) {
            if(event.key == InputKeyBack) {
                running = false;
            } else {
                // Any other key skips the splash straight to the first screen.
                app.screen = app.post_splash;
            }
        } else if(app.screen == ScreenLanguagePicker) {
            if(event.key == InputKeyDown) {
                app.lang_selection = (app.lang_selection + 1) % (int)LANGUAGE_COUNT;
            } else if(event.key == InputKeyUp) {
                app.lang_selection =
                    (app.lang_selection - 1 + (int)LANGUAGE_COUNT) % (int)LANGUAGE_COUNT;
            } else if(event.key == InputKeyOk) {
                app.lang_index = app.lang_selection;
                app.language_confirmed = true;

                if(!location_set) {
                    // First-run: language is now known, so ask for the
                    // location (coordinates + timezone) before any info page.
                    keypad_enter(&app, true, true);
                } else {
                    do_save = true;
                    save_lang = app.lang_index;
                    save_location = app.location;
                    save_splash = app.splash_auto;
                    app.screen = ScreenInfo;
                }
            } else if(event.key == InputKeyBack) {
                if(app.language_confirmed) {
                    app.lang_selection = app.lang_index;
                    app.screen = ScreenInfo;
                }
            }
        } else if(app.screen == ScreenLocationKeypad) {
            int cols = keypad_row_cols(app.keypad_row);
            if(event.key == InputKeyRight) {
                app.keypad_col = (app.keypad_col + 1) % cols;
            } else if(event.key == InputKeyLeft) {
                app.keypad_col = (app.keypad_col - 1 + cols) % cols;
            } else if(event.key == InputKeyDown) {
                app.keypad_row = (app.keypad_row + 1) % KEYPAD_ROWS;
                int new_cols = keypad_row_cols(app.keypad_row);
                if(app.keypad_col >= new_cols) app.keypad_col = new_cols - 1;
            } else if(event.key == InputKeyUp) {
                app.keypad_row = (app.keypad_row - 1 + KEYPAD_ROWS) % KEYPAD_ROWS;
                int new_cols = keypad_row_cols(app.keypad_row);
                if(app.keypad_col >= new_cols) app.keypad_col = new_cols - 1;
            } else if(event.key == InputKeyOk) {
                const char* label = keypad_label(app.keypad_row, app.keypad_col);
                size_t len = strlen(app.keypad_buffer);

                if(strcmp(label, "Del") == 0) {
                    app.keypad_error = false;
                    if(len > 0) app.keypad_buffer[len - 1] = '\0';
                } else if(strcmp(label, "Save") == 0) {
                    float min, max, value;
                    keypad_field_range(app.keypad_field, &min, &max);
                    if(keypad_parse(app.keypad_buffer, min, max, &value)) {
                        app.keypad_error = false;
                        app.keypad_row = 0;
                        app.keypad_col = 0;

                        if(app.keypad_field == KeypadFieldLatitude) {
                            app.keypad_location.latitude = value;
                            app.keypad_field = KeypadFieldLongitude;
                            snprintf(
                                app.keypad_buffer,
                                sizeof(app.keypad_buffer),
                                "%.4f",
                                (double)app.keypad_location.longitude);
                        } else {
                            app.keypad_location.longitude = value;
                            if(app.wizard_tz) {
                                // First-run wizard: continue into the timezone
                                // picker, proposing the current zone.
                                app.tz_sel = app.keypad_location.tz_index;
                                app.screen = ScreenTimezonePicker;
                            } else {
                                // Editing coordinates only: commit now.
                                app.location = app.keypad_location;
                                location_set = true;
                                do_save = true;
                                save_lang = app.lang_index;
                                save_location = app.location;
                                save_splash = app.splash_auto;
                                app.screen = ScreenInfo;
                            }
                        }
                    } else {
                        app.keypad_error = true;
                    }
                } else {
                    // Digit, '.', or '-'.
                    app.keypad_error = false;
                    if(len + 1 < sizeof(app.keypad_buffer)) {
                        app.keypad_buffer[len] = label[0];
                        app.keypad_buffer[len + 1] = '\0';
                    }
                }
            } else if(event.key == InputKeyBack) {
                if(app.keypad_field == KeypadFieldTimezone) {
                    app.keypad_field = KeypadFieldLongitude;
                    snprintf(
                        app.keypad_buffer,
                        sizeof(app.keypad_buffer),
                        "%.4f",
                        (double)app.keypad_location.longitude);
                    app.keypad_error = false;
                } else if(app.keypad_field == KeypadFieldLongitude) {
                    app.keypad_field = KeypadFieldLatitude;
                    snprintf(
                        app.keypad_buffer,
                        sizeof(app.keypad_buffer),
                        "%.4f",
                        (double)app.keypad_location.latitude);
                    app.keypad_error = false;
                } else if(!app.keypad_mandatory) {
                    app.screen = ScreenInfo;
                }
            }
        } else if(app.screen == ScreenTimezonePicker) {
            if(event.key == InputKeyDown) {
                if(app.tz_sel < TZ_COUNT - 1) app.tz_sel++;
            } else if(event.key == InputKeyUp) {
                if(app.tz_sel > 0) app.tz_sel--;
            } else if(event.key == InputKeyOk) {
                app.keypad_location.tz_index = app.tz_sel;
                app.location = app.keypad_location;
                location_set = true;
                do_save = true;
                save_lang = app.lang_index;
                save_location = app.location;
                save_splash = app.splash_auto;
                app.screen = ScreenInfo;
            } else if(event.key == InputKeyBack) {
                if(app.wizard_tz) {
                    // Part of the coordinate wizard: step back to longitude.
                    app.keypad_field = KeypadFieldLongitude;
                    snprintf(
                        app.keypad_buffer,
                        sizeof(app.keypad_buffer),
                        "%.4f",
                        (double)app.keypad_location.longitude);
                    app.keypad_row = 0;
                    app.keypad_col = 0;
                    app.keypad_error = false;
                    app.screen = ScreenLocationKeypad;
                } else {
                    // Editing the timezone only: back to the info pages.
                    app.screen = ScreenInfo;
                }
            }
        } else {
            if(event.key == InputKeyBack) {
                running = false;
            } else if(event.key == InputKeyRight) {
                app.page = (app.page + 1) % PAGE_COUNT;
                if(app.page == PAGE_SETTINGS) app.settings_sel = SETTINGS_SPLASH;
                if(app.page == PAGE_LOCATION) app.location_sel = LOCATION_COORDS;
            } else if(event.key == InputKeyLeft) {
                app.page = (app.page - 1 + PAGE_COUNT) % PAGE_COUNT;
                if(app.page == PAGE_SETTINGS) app.settings_sel = SETTINGS_SPLASH;
                if(app.page == PAGE_LOCATION) app.location_sel = LOCATION_COORDS;
            } else if(event.key == InputKeyDown || event.key == InputKeyUp) {
                // Move the selection cursor on the two menu pages.
                int step = event.key == InputKeyDown ? 1 : -1;
                if(app.page == PAGE_SETTINGS)
                    app.settings_sel = (app.settings_sel + step + SETTINGS_COUNT) % SETTINGS_COUNT;
                else if(app.page == PAGE_LOCATION)
                    app.location_sel = (app.location_sel + step + LOCATION_COUNT) % LOCATION_COUNT;
            } else if(event.key == InputKeyOk) {
                if(app.page == PAGE_QR || app.page == PAGE_NFC) {
                    // No action on the QR/NFC pages.
                } else if(app.page == PAGE_LOCATION) {
                    if(app.location_sel == LOCATION_COORDS) {
                        keypad_enter(&app, false, false); // edit coordinates only
                    } else {
                        // Edit the timezone only: straight to the picker.
                        app.keypad_location = app.location;
                        app.tz_sel = app.location.tz_index;
                        app.wizard_tz = false;
                        app.screen = ScreenTimezonePicker;
                    }
                } else if(app.page == PAGE_SETTINGS) {
                    if(app.settings_sel == SETTINGS_SPLASH) {
                        // Toggle the splash mode and persist it for next launch.
                        app.splash_auto = !app.splash_auto;
                        do_save = true;
                        save_lang = app.lang_index;
                        save_location = app.location;
                        save_splash = app.splash_auto;
                    } else {
                        // Reset: clear language + splash choice, then relaunch.
                        do_save = true;
                        save_lang = -1;
                        save_location = app.location;
                        save_splash = false;
                        app.splash_auto = false;
                        do_restart = true;
                        running = false;
                    }
                } else {
                    app.lang_selection = app.lang_index;
                    app.screen = ScreenLanguagePicker;
                }
            }
        }

        furi_mutex_release(app.mutex);

        if(do_save) {
            Storage* s = furi_record_open(RECORD_STORAGE);
            save_settings(s, save_lang, &save_location, save_splash);
            furi_record_close(RECORD_STORAGE);
        }
        if(do_restart) {
            request_app_restart();
        }

        // Emulate the NFC tag only while the NFC page is showing.
        nfc_set_emulating(&app, app.screen == ScreenInfo && app.page == PAGE_NFC);

        view_port_update(view_port);
    }

    nfc_set_emulating(&app, false);
    if(app.nfc_data) mf_ultralight_free(app.nfc_data);
    if(app.nfc) nfc_free(app.nfc);

    furi_timer_free(timer);
    gui_remove_view_port(gui, view_port);
    furi_record_close(RECORD_GUI);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_mutex_free(app.mutex);

    return 0;
}
