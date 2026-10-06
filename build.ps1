# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Eric M. Kok
<#
Bouwt een Flipper Zero FAP-app met ufbt.

Gebruik:
    .\build.ps1 <appmap> [-Launch]

Voorbeeld:
    .\build.ps1 hello_world
    .\build.ps1 hello_world -Launch   # bouwt en uploadt/start direct op de Flipper via USB
#>
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$App,

    [switch]$Launch
)

$ErrorActionPreference = "Stop"

$appDir = Join-Path $PSScriptRoot $App
$famFile = Join-Path $appDir "application.fam"

if (-not (Test-Path $famFile)) {
    Write-Error "Geen application.fam gevonden in '$appDir'. Geef de mapnaam van de app op, bv.: .\build.ps1 hello_world"
    exit 1
}

Push-Location $appDir
try {
    $preBuildScript = Join-Path $appDir "pre_build.ps1"
    if (Test-Path $preBuildScript) {
        & $preBuildScript
    }

    Write-Host "Compilatie starten..." -ForegroundColor Cyan

    if ($Launch) {
        python -m ufbt launch
    }
    else {
        python -m ufbt
    }

    if ($LASTEXITCODE -ne 0) {
        throw "ufbt build mislukt (exit code $LASTEXITCODE)"
    }

    $fapFiles = Get-ChildItem -Path (Join-Path $appDir "dist") -Filter "*.fap" -File -ErrorAction SilentlyContinue
    if ($fapFiles) {
        Write-Host ""
        foreach ($f in $fapFiles) {
            Write-Host "Gebouwd: $($f.FullName)" -ForegroundColor Green
        }
    }
}
finally {
    Pop-Location
}
