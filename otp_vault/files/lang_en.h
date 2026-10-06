#pragma once

// SPDX-License-Identifier: GPL-3.0-only
// Copyright (c) 2026 Eric M. Kok
// English
static const LangStrings lang_en = {
    .lang_name = "English",

    .title_codes = "Codes",
    .title_import = "Import",
    .title_settings = "Settings",
    .title_help = "Help",
    .title_about = "About",
    .title_credits = "Credits",
    .title_unlock = "Unlock",
    .title_create = "New password",
    .title_confirm = "Repeat password",

    .pw_hint1 = "Up/Down: pick char",
    .pw_hint2 = "Right: add  Left: del",
    .pw_hint3 = "OK: confirm",

    .msg_wrong = "Wrong password",
    .msg_mismatch = "Passwords differ",
    .msg_short = "Min. 8 characters",
    .msg_working = "Working...",
    .msg_wait = "Wait %lus",
    .err_title = "SELF-TEST FAILED",
    .err_use = "DO NOT USE",
    .err_step = "Failed step: %u",
    .err_exit = "Back: exit",
    .msg_selftest_fail = "Self-test failed",
    .msg_no_usb = "No USB host found",
    .msg_typed = "Typed",
    .msg_corrupt = "Vault file corrupt",
    .msg_save_fail = "Save failed",

    .no_accounts = "No accounts yet",
    .no_accounts_hint = "Use the Import screen",
    .clock_warn = "Set the clock first!",
    .hint_type = "OK: type via USB",

    .imp_1 = "Put import.txt in",
    .imp_2 = "apps_data/otp_vault",
    .imp_3 = "Name;SECRET;sha1;6;30",
    .imp_ok = "OK: import now",
    .imp_done = "Imported: %u",
    .imp_none = "No import.txt found",
    .imp_bad = "Bad line: %u",
    .imp_full = "Vault is full",

    .lbl_language = "Language",
    .lbl_utc = "UTC offset",
    .lbl_digits = "Typing",
    .lbl_lock = "Auto-lock",
    .digits_top = "top row",
    .digits_pad = "numpad",

    .help_1 = "Right/Left: screen",
    .help_2 = "Down/Up: in screen",
    .help_3 = "OK: type code / set",
    .help_4 = "Back: back / exit",
    .help_5 = "Set UTC offset first",

    .about_version = "Version:",
    .about_author = "Author:",
    .about_copyright = "Copyright:",
    .about_license = "License:",
    .about_selftest = "Self-test:",
    .selftest_ok = "OK",
    .selftest_fail = "FAIL",

    .credit_1 = "No third-party code.",
    .credit_2 = "Compared with the app",
    .credit_3 = "Authenticator by",
    .credit_4 = "A. Kopachov (GPL-3.0)",
    .credit_5 = "See THIRD-PARTY.md",
};
