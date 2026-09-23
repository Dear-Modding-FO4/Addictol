param(
    [string] $Repository = 'Dear-Modding-FO4/Addictol',
    [string] $ManifestPath = (Join-Path $PSScriptRoot '..\labels.json'),
    [switch] $WhatIf
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-Gh {
    if ($WhatIf) {
        Write-Host "gh $($args -join ' ')"
        return
    }
    & gh @args
    if ($LASTEXITCODE -ne 0) { throw "gh $($args -join ' ') failed with exit code $LASTEXITCODE." }
}

$manifest = Get-Content -Raw $ManifestPath | ConvertFrom-Json
$existing = @(& gh label list --repo $Repository --limit 500 --json name | ConvertFrom-Json | ForEach-Object name)
if ($LASTEXITCODE -ne 0) { throw "Could not list labels for $Repository." }

foreach ($label in $manifest) {
    $aliases = if ($label.PSObject.Properties['aliases']) { @($label.aliases) } else { @() }
    $alias = $aliases | Where-Object { $existing -ccontains $_ } | Select-Object -First 1

    # Renaming keeps the label on already-tagged issues.
    if ($alias -and $existing -cnotcontains $label.name) {
        Invoke-Gh label edit $alias --repo $Repository --name $label.name --color $label.color --description $label.description
    }
    else {
        Invoke-Gh label create $label.name --repo $Repository --color $label.color --description $label.description --force
    }
}

$managed = @($manifest.name) + @($manifest | Where-Object { $_.PSObject.Properties['aliases'] } | ForEach-Object { $_.aliases })
$unmanaged = $existing | Where-Object { $managed -cnotcontains $_ }
if ($unmanaged) { Write-Warning "Labels not in the manifest were left untouched: $($unmanaged -join ', ')" }
