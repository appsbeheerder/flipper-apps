#pragma once

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Eric M. Kok
// AUTO-GEGENEREERD door pre_build.ps1 - niet handmatig bewerken.
// Wordt bij elke build opnieuw opgebouwd uit files/lang_*.h.

#include "files/lang_da.h"
#include "files/lang_de.h"
#include "files/lang_en.h"
#include "files/lang_es.h"
#include "files/lang_fr.h"
#include "files/lang_id.h"
#include "files/lang_it.h"
#include "files/lang_nl.h"
#include "files/lang_no.h"
#include "files/lang_pt.h"
#include "files/lang_sv.h"
#include "files/lang_tr.h"

static const LangStrings* const LANGUAGES[] = {
    &lang_da,
    &lang_de,
    &lang_en,
    &lang_es,
    &lang_fr,
    &lang_id,
    &lang_it,
    &lang_nl,
    &lang_no,
    &lang_pt,
    &lang_sv,
    &lang_tr,
};
#define LANGUAGE_COUNT (sizeof(LANGUAGES) / sizeof(LANGUAGES[0]))

