/*
 * Pomodoro - Flipper Zero app
 * Copyright (c) 2026 Eric M. Kok
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE in this app's folder for the full license text.
 */

#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <input/input.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>
#include <storage/storage.h>
#include <flipper_format/flipper_format.h>
#include <datetime/datetime.h>

#include <stdio.h>
#include <string.h>

#define APP_VERSION "1.02"
#define APP_START_YEAR 2026
#define APP_AUTHOR "Eric M. Kok"
#define APP_LICENSE "GPLv3"

#define DATA_DIR EXT_PATH("apps_data/pomodoro")
#define DATA_FILE DATA_DIR "/pomodoro.cfg"

#define PAGE_TIMER 0
#define PAGE_SETTINGS 1
#define PAGE_STATS 2
#define PAGE_HELP 3
#define PAGE_ABOUT 4
#define PAGE_COUNT 5

#define CYCLE_LENGTH 4

typedef enum {
    PhaseWork,
    PhaseShort,
    PhaseLong,
} Phase;

static const char* const page_titles[PAGE_COUNT] =
    {"Pomodoro", "Settings", "Statistics", "Help", "About"};
static const char* const phase_names[3] = {"WORK", "SHORT BREAK", "LONG BREAK"};
static const char* const setting_names[3] = {"Work", "Short break", "Long break"};
static const uint8_t setting_min[3] = {5, 1, 5};
static const uint8_t setting_max[3] = {60, 15, 30};
static const uint8_t setting_step[3] = {5, 1, 5};

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* queue;
    FuriMutex* mutex;
    NotificationApp* notifications;

    uint8_t page;
    Phase phase;
    bool running;
    uint32_t remaining; // seconds
    uint32_t last_tick;
    uint8_t settings_sel;
    uint8_t minutes[3]; // work, short break, long break
    uint8_t cycle; // finished pomodoros in the current cycle
    uint32_t done_today;
    uint32_t focus_today; // minutes
    uint32_t day; // yyyymmdd of the statistics
} App;

static uint32_t today_stamp(void) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    return dt.year * 10000 + dt.month * 100 + dt.day;
}

static uint32_t phase_secs(const App* app) {
    return (uint32_t)app->minutes[app->phase] * 60;
}

static void refresh_day(App* app) {
    uint32_t today = today_stamp();
    if(app->day != today) {
        app->day = today;
        app->done_today = 0;
        app->focus_today = 0;
    }
}

static void app_save(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    storage_simply_mkdir(storage, DATA_DIR);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    if(flipper_format_file_open_always(ff, DATA_FILE)) {
        uint32_t v;
        flipper_format_write_header_cstr(ff, "Pomodoro", 1);
        v = app->minutes[0];
        flipper_format_write_uint32(ff, "Work", &v, 1);
        v = app->minutes[1];
        flipper_format_write_uint32(ff, "Short", &v, 1);
        v = app->minutes[2];
        flipper_format_write_uint32(ff, "Long", &v, 1);
        flipper_format_write_uint32(ff, "Day", &app->day, 1);
        flipper_format_write_uint32(ff, "Done", &app->done_today, 1);
        flipper_format_write_uint32(ff, "Focus", &app->focus_today, 1);
        v = app->cycle;
        flipper_format_write_uint32(ff, "Cycle", &v, 1);
    }
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
}

static void app_load(App* app) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* header = furi_string_alloc();
    uint32_t version = 0;
    if(flipper_format_file_open_existing(ff, DATA_FILE) &&
       flipper_format_read_header(ff, header, &version)) {
        uint32_t v;
        for(uint8_t i = 0; i < 3; i++) {
            static const char* const keys[3] = {"Work", "Short", "Long"};
            if(flipper_format_read_uint32(ff, keys[i], &v, 1) && v >= setting_min[i] &&
               v <= setting_max[i]) {
                app->minutes[i] = v;
            }
        }
        flipper_format_read_uint32(ff, "Day", &app->day, 1);
        flipper_format_read_uint32(ff, "Done", &app->done_today, 1);
        flipper_format_read_uint32(ff, "Focus", &app->focus_today, 1);
        if(flipper_format_read_uint32(ff, "Cycle", &v, 1) && v < CYCLE_LENGTH) {
            app->cycle = v;
        }
    }
    furi_string_free(header);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    refresh_day(app);
}

static void phase_complete(App* app) {
    refresh_day(app);
    if(app->phase == PhaseWork) {
        app->done_today++;
        app->focus_today += app->minutes[PhaseWork];
        app->cycle++;
        app->phase = app->cycle >= CYCLE_LENGTH ? PhaseLong : PhaseShort;
    } else {
        if(app->phase == PhaseLong) app->cycle = 0;
        app->phase = PhaseWork;
    }
    app->remaining = phase_secs(app);
    app->last_tick = furi_get_tick();
    notification_message(app->notifications, &sequence_audiovisual_alert);
    notification_message(app->notifications, &sequence_display_backlight_on);
    app_save(app);
}

static void app_tick(App* app) {
    if(!app->running) return;
    uint32_t secs = (furi_get_tick() - app->last_tick) / 1000;
    if(secs == 0) return;
    app->last_tick += secs * 1000;
    if(secs >= app->remaining) {
        phase_complete(app);
    } else {
        app->remaining -= secs;
    }
}

//======================================================================================================================
// Drawing
//======================================================================================================================

static void draw_row(Canvas* canvas, uint8_t y, const char* label, const char* value) {
    canvas_draw_str(canvas, 2, y, label);
    canvas_draw_str(canvas, 52, y, value);
}

static void draw_callback(Canvas* canvas, void* ctx) {
    App* app = ctx;
    furi_mutex_acquire(app->mutex, FuriWaitForever);

    char buf[32];
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, page_titles[app->page]);
    canvas_set_font(canvas, FontSecondary);
    snprintf(buf, sizeof(buf), "%d/%d", app->page + 1, PAGE_COUNT);
    canvas_draw_str_aligned(canvas, 126, 2, AlignRight, AlignTop, buf);
    canvas_draw_line(canvas, 0, 13, 127, 13);

    if(app->page == PAGE_TIMER) {
        canvas_draw_str_aligned(canvas, 64, 25, AlignCenter, AlignBottom, phase_names[app->phase]);
        snprintf(buf, sizeof(buf), "%02lu:%02lu", app->remaining / 60, app->remaining % 60);
        canvas_set_font(canvas, FontBigNumbers);
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignBottom, buf);
        canvas_set_font(canvas, FontSecondary);
        const char* state = app->running ? "Running" :
                            app->remaining == phase_secs(app) ? "Ready" :
                                                                 "Paused";
        canvas_draw_str_aligned(canvas, 64, 54, AlignCenter, AlignBottom, state);
        for(uint8_t i = 0; i < CYCLE_LENGTH; i++) {
            int32_t x = 49 + i * 10;
            if(i < app->cycle) {
                canvas_draw_disc(canvas, x, 60, 2);
            } else {
                canvas_draw_circle(canvas, x, 60, 2);
            }
        }
    } else if(app->page == PAGE_SETTINGS) {
        for(uint8_t i = 0; i < 3; i++) {
            uint8_t y = 26 + i * 10;
            if(i == app->settings_sel) canvas_draw_str(canvas, 0, y, ">");
            snprintf(buf, sizeof(buf), "%u min", app->minutes[i]);
            canvas_draw_str(canvas, 8, y, setting_names[i]);
            canvas_draw_str(canvas, 80, y, buf);
        }
        canvas_draw_str(canvas, 2, 62, "Down/Up: pick   OK: change");
    } else if(app->page == PAGE_STATS) {
        snprintf(buf, sizeof(buf), "%lu", app->done_today);
        canvas_draw_str(canvas, 2, 26, "Pomodoros today");
        canvas_draw_str(canvas, 100, 26, buf);
        snprintf(buf, sizeof(buf), "%luh %02lum", app->focus_today / 60, app->focus_today % 60);
        canvas_draw_str(canvas, 2, 38, "Focus time");
        canvas_draw_str(canvas, 82, 38, buf);
        snprintf(buf, sizeof(buf), "%u/%d", app->cycle, CYCLE_LENGTH);
        canvas_draw_str(canvas, 2, 50, "Cycle");
        canvas_draw_str(canvas, 82, 50, buf);
    } else if(app->page == PAGE_HELP) {
        canvas_draw_str(canvas, 2, 24, "Right/Left: screen");
        canvas_draw_str(canvas, 2, 33, "Down/Up: in screen");
        canvas_draw_str(canvas, 2, 42, "OK: start/pause/set");
        canvas_draw_str(canvas, 2, 51, "Back: reset / exit");
        canvas_draw_str(canvas, 2, 60, "4x work = long break");
    } else if(app->page == PAGE_ABOUT) {
        DateTime dt;
        furi_hal_rtc_get_datetime(&dt);
        snprintf(buf, sizeof(buf), "v%s", APP_VERSION);
        draw_row(canvas, 26, "Version:", buf);
        draw_row(canvas, 36, "Author:", APP_AUTHOR);
        if(dt.year > APP_START_YEAR) {
            snprintf(buf, sizeof(buf), "%u-%u", APP_START_YEAR, dt.year);
        } else {
            snprintf(buf, sizeof(buf), "%u", APP_START_YEAR);
        }
        draw_row(canvas, 46, "Copyright:", buf);
        draw_row(canvas, 56, "License:", APP_LICENSE);
    }

    furi_mutex_release(app->mutex);
}

static void input_callback(InputEvent* event, void* ctx) {
    App* app = ctx;
    furi_message_queue_put(app->queue, event, 0);
}

//======================================================================================================================
// Input
//======================================================================================================================

// Returns false when the app should exit.
static bool handle_input(App* app, const InputEvent* event) {
    if(event->type == InputTypeLong && event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort) return true;

    switch(event->key) {
    case InputKeyRight:
        app->page = (app->page + 1) % PAGE_COUNT;
        break;
    case InputKeyLeft:
        app->page = (app->page - 1 + PAGE_COUNT) % PAGE_COUNT;
        break;
    case InputKeyDown:
    case InputKeyUp:
        if(app->page == PAGE_SETTINGS) {
            int step = event->key == InputKeyDown ? 1 : -1;
            app->settings_sel = (app->settings_sel + step + 3) % 3;
        }
        break;
    case InputKeyOk:
        if(app->page == PAGE_TIMER) {
            app->running = !app->running;
            app->last_tick = furi_get_tick();
        } else if(app->page == PAGE_SETTINGS) {
            uint8_t i = app->settings_sel;
            uint8_t v = app->minutes[i] + setting_step[i];
            app->minutes[i] = v > setting_max[i] ? setting_min[i] : v;
            if(!app->running && i == app->phase) app->remaining = phase_secs(app);
            app_save(app);
        }
        break;
    case InputKeyBack:
        if(app->page != PAGE_TIMER) {
            app->page = PAGE_TIMER;
        } else if(app->running || app->remaining != phase_secs(app)) {
            app->running = false;
            app->remaining = phase_secs(app);
        } else {
            return false;
        }
        break;
    default:
        break;
    }
    return true;
}

int32_t pomodoro_app(void* p) {
    UNUSED(p);

    App* app = malloc(sizeof(App));
    memset(app, 0, sizeof(App));
    app->minutes[PhaseWork] = 25;
    app->minutes[PhaseShort] = 5;
    app->minutes[PhaseLong] = 15;
    app->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->notifications = furi_record_open(RECORD_NOTIFICATION);

    app_load(app);
    app->phase = PhaseWork;
    app->remaining = phase_secs(app);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, draw_callback, app);
    view_port_input_callback_set(app->view_port, input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);

    bool run = true;
    while(run) {
        InputEvent event;
        FuriStatus status = furi_message_queue_get(app->queue, &event, 200);
        furi_mutex_acquire(app->mutex, FuriWaitForever);
        if(status == FuriStatusOk) run = handle_input(app, &event);
        app_tick(app);
        furi_mutex_release(app->mutex);
        view_port_update(app->view_port);
    }

    gui_remove_view_port(app->gui, app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_NOTIFICATION);
    view_port_free(app->view_port);
    furi_message_queue_free(app->queue);
    furi_mutex_free(app->mutex);
    free(app);
    return 0;
}
