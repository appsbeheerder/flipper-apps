<#
Device Info - Flipper Zero app
Copyright (c) 2026 Eric M. Kok
SPDX-License-Identifier: MIT
See LICENSE in this app's folder for the full license text.

Genereert lang_manifest.h door alle files/lang_*.h bestanden te scannen.
Wordt automatisch aangeroepen door build.ps1 vlak voor ufbt.

Om een taal toe te voegen: zet een nieuw files/lang_<code>.h bestand neer met
daarin "static const LangStrings lang_<code> = { ... };" (zelfde patroon als
de bestaande bestanden) en bouw opnieuw. Geen wijziging in device_info.c nodig.
#>

$ErrorActionPreference = "Stop"

$appDir = $PSScriptRoot
$filesDir = Join-Path $appDir "files"
$manifestPath = Join-Path $appDir "lang_manifest.h"

$langFiles = Get-ChildItem -Path $filesDir -Filter "lang_*.h" -File | Sort-Object Name

if ($langFiles.Count -eq 0) {
    throw "Geen files/lang_*.h bestanden gevonden in '$filesDir'."
}

$varNames = $langFiles | ForEach-Object { [System.IO.Path]::GetFileNameWithoutExtension($_.Name) }

$lines = @()
$lines += "#pragma once"
$lines += ""
$lines += "// SPDX-License-Identifier: MIT"
$lines += "// Copyright (c) 2026 Eric M. Kok"
$lines += "// AUTO-GEGENEREERD door pre_build.ps1 - niet handmatig bewerken."
$lines += "// Wordt bij elke build opnieuw opgebouwd uit files/lang_*.h."
$lines += ""
foreach ($f in $langFiles) {
    $lines += "#include ""files/$($f.Name)"""
}
$lines += ""
$lines += "static const LangStrings* const LANGUAGES[] = {"
foreach ($name in $varNames) {
    $lines += "    &$name,"
}
$lines += "};"
$lines += "#define LANGUAGE_COUNT (sizeof(LANGUAGES) / sizeof(LANGUAGES[0]))"
$lines += ""

Set-Content -Path $manifestPath -Value $lines -Encoding UTF8

Write-Host "lang_manifest.h opnieuw opgebouwd uit $($langFiles.Count) taalbestand(en): $($varNames -join ', ')" -ForegroundColor Cyan
