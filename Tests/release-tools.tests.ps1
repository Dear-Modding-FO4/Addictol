Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot '..\.github\scripts\release-tools.psm1') -Force

function Assert-Equal {
    param($Expected, $Actual, [string] $Message)
    if ($Expected -ne $Actual) {
        Write-Error "$Message Expected '$Expected', got '$Actual'."
    }
}

function Assert-Throws {
    param([scriptblock] $Action, [string] $Message)
    try {
        & $Action
        Write-Error "$Message Expected an exception."
    } catch {
        if ($_.Exception.Message -like "$Message Expected*") {
            throw
        }
    }
}

$version = ConvertFrom-CanonicalVersion '1.6.0'
Assert-Equal 1 $version.Major 'major'
Assert-Equal 6 $version.Minor 'minor'
Assert-Equal 0 $version.Patch 'patch'
Assert-Equal '1.6.0' $version.Text 'canonical text'

foreach ($invalid in @('1.6', 'v1.6.0', '01.6.0', '1.06.0', '1.6.00', '1.6.0-dev.1', '256.0.0', '1.256.0', '1.0.4096', "1.6.0`n")) {
    Assert-Throws { ConvertFrom-CanonicalVersion $invalid } "reject $invalid"
}
Assert-Equal '999.0.0' (ConvertFrom-CanonicalVersion '999.0.0' -AllowPackedOverflow).Text 'parse release history outside packed bounds'

Assert-Equal -1 (Compare-CanonicalVersion (ConvertFrom-CanonicalVersion '1.6.9') (ConvertFrom-CanonicalVersion '1.7.0')) 'version ordering'
Assert-Equal 1 (Compare-CanonicalVersion (ConvertFrom-CanonicalVersion '2.0.0') (ConvertFrom-CanonicalVersion '1.255.0')) 'major ordering'
Assert-Equal 0 (Compare-CanonicalVersion (ConvertFrom-CanonicalVersion '1.6.0') (ConvertFrom-CanonicalVersion '1.6.0')) 'version equality'

$header = @'
#define VERSION_MAJOR 1
#define VERSION_MINOR 6
#define VERSION_PATCH 0
#define VERSION_REVISION 0
'@
Assert-Equal '1.6.0' (Get-ProductVersionFromContent $header).Text 'header version'
Assert-Throws { Get-ProductVersionFromContent ($header -replace 'VERSION_REVISION 0', 'VERSION_REVISION 16') } 'reject revision overflow'

$temp = Join-Path ([System.IO.Path]::GetTempPath()) "addictol-version-$([guid]::NewGuid()).h"
try {
    $encoding = [System.Text.UTF8Encoding]::new($true)
    [System.IO.File]::WriteAllText($temp, $header, $encoding)
    Set-ProductVersionInHeader -Path $temp -Version (ConvertFrom-CanonicalVersion '1.7.0')
    $bytes = [System.IO.File]::ReadAllBytes($temp)
    Assert-Equal 0xEF $bytes[0] 'BOM byte 1'
    Assert-Equal 0xBB $bytes[1] 'BOM byte 2'
    Assert-Equal 0xBF $bytes[2] 'BOM byte 3'
    Assert-Equal '1.7.0' (Get-ProductVersionFromHeader $temp).Text 'updated header'
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}

Assert-Equal $true (Get-DevelopmentPublicationDecision -StableTagExists $false -StableReleaseExists $false).Publish 'publish development'
Assert-Equal $false (Get-DevelopmentPublicationDecision -StableTagExists $true -StableReleaseExists $false).Publish 'suppress for stable tag'
Assert-Equal $false (Get-DevelopmentPublicationDecision -StableTagExists $false -StableReleaseExists $true).Publish 'suppress for stable release'

$requested = ConvertFrom-CanonicalVersion '1.6.0'
Assert-Equal $true (Get-StableVersionDecision -RequestedVersion $requested -HighestStableVersion (ConvertFrom-CanonicalVersion '1.5.395') -MatchingReleaseExists $false).Publish 'publish newer stable'
Assert-Equal $true (Get-StableVersionDecision -RequestedVersion $requested -HighestStableVersion $requested -MatchingReleaseExists $true).Recovery 'recover existing stable'
Assert-Throws {
    Get-StableVersionDecision -RequestedVersion $requested -HighestStableVersion (ConvertFrom-CanonicalVersion '1.7.0') -MatchingReleaseExists $false
} 'reject stable downgrade'
Assert-Throws {
    Get-StableVersionDecision -RequestedVersion $requested -HighestStableVersion $requested -MatchingReleaseExists $false
} 'reject duplicate stable without matching release'

$metadataScript = Join-Path $PSScriptRoot '..\.github\scripts\get-build-metadata.ps1'
$fixtureRoot = Join-Path ([System.IO.Path]::GetTempPath()) "addictol-version-$([guid]::NewGuid())"
$fixtureVersionDirectory = Join-Path $fixtureRoot 'Version'
$fixtureHeader = Join-Path $fixtureVersionDirectory 'resource_version2.h'
$metadataOutput = Join-Path $fixtureRoot 'metadata.txt'
try {
    New-Item -ItemType Directory -Path $fixtureVersionDirectory | Out-Null
    [System.IO.File]::WriteAllText($fixtureHeader, $header)
    foreach ($productVersion in @('1.6.0', '1.7.0', '1.7.2')) {
        Set-ProductVersionInHeader -Path $fixtureHeader -Version (ConvertFrom-CanonicalVersion $productVersion)
        foreach ($kind in @('development', 'stable', 'validation')) {
            & $metadataScript `
                -Kind $kind `
                -BuildNumber 7000 `
                -ExpectedVersion $productVersion `
                -SourceSha ('a' * 40) `
                -RepositoryRoot $fixtureRoot `
                -OutputPath $metadataOutput
            $metadata = @{}
            Get-Content -LiteralPath $metadataOutput | ForEach-Object {
                $key, $value = $_ -split '=', 2
                $metadata[$key] = $value
            }
            $identity = switch ($kind) {
                'development' { "v$productVersion-dev.7000" }
                'stable' { "v$productVersion" }
                'validation' { 'validation-aaaaaaaa' }
            }
            Assert-Equal $productVersion $metadata.product_version 'metadata product version'
            Assert-Equal $identity $metadata.build_identity "$kind build identity"
            Assert-Equal "Addictol-$identity.zip" $metadata.asset_name "$kind asset name"
            Remove-Item -LiteralPath $metadataOutput
        }
    }
    Assert-Throws {
        & $metadataScript `
            -Kind development `
            -BuildNumber '0615' `
            -SourceSha ('a' * 40) `
            -RepositoryRoot $fixtureRoot `
            -OutputPath $metadataOutput
    } 'reject noncanonical build identity'
    Assert-Throws {
        & $metadataScript `
            -Kind stable `
            -ExpectedVersion '1.6.0' `
            -SourceSha ('a' * 40) `
            -RepositoryRoot $fixtureRoot `
            -OutputPath $metadataOutput
    } 'reject mismatched stable version'
} finally {
    foreach ($path in @($metadataOutput, $fixtureHeader, $fixtureVersionDirectory, $fixtureRoot)) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

$original = 'a' * 40
$advanced = 'b' * 40
Assert-Throws { Assert-CanonicalCommitSha "$original`n" } 'reject noncanonical commit SHA'
Assert-Equal $advanced (Resolve-ReleaseSourceSha -SourceSha $advanced -SourceRef master -ExistingTagCommit $null) 'pin new release source'
Assert-Equal $original (Resolve-ReleaseSourceSha -SourceSha $advanced -SourceRef master -ExistingTagCommit $original) 'recover after master advances'
Assert-Equal $original (Resolve-ReleaseSourceSha -SourceSha $advanced -SourceRef refs/heads/master -ExistingTagCommit $original) 'recover explicit master ref'
Assert-Equal $original (Resolve-ReleaseSourceSha -SourceSha $original -SourceRef v1.6.0 -ExistingTagCommit $original) 'recover explicit original tag'
Assert-Throws {
    Resolve-ReleaseSourceSha -SourceSha $advanced -SourceRef $advanced -ExistingTagCommit $original
} 'reject mismatched explicit release source'

$nextVersion = ConvertFrom-CanonicalVersion '1.7.0'
Assert-NextDevelopmentVersion -NextVersion $nextVersion -MasterVersion $requested -RecoveringRelease $false
Assert-NextDevelopmentVersion -NextVersion $nextVersion -MasterVersion $nextVersion -RecoveringRelease $true
Assert-Throws {
    Assert-NextDevelopmentVersion -NextVersion $nextVersion -MasterVersion $nextVersion -RecoveringRelease $false
} 'require next version to advance for new releases'
Assert-Throws {
    Assert-NextDevelopmentVersion -NextVersion $nextVersion -MasterVersion (ConvertFrom-CanonicalVersion '1.8.0') -RecoveringRelease $true
} 'reject bump below newer master version'

Assert-Equal $null (Get-HighestStableRelease -Releases @() -BelowVersion $null) 'empty release history'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
foreach ($workflow in Get-ChildItem -LiteralPath (Join-Path $repositoryRoot '.github\workflows') -Filter '*.yml') {
    $content = Get-Content -LiteralPath $workflow.FullName -Raw
    foreach ($reference in [regex]::Matches($content, '\.github/scripts/([a-z-]+\.ps1)')) {
        $scriptPath = Join-Path $repositoryRoot ".github\scripts\$($reference.Groups[1].Value)"
        Assert-Equal $true (Test-Path -LiteralPath $scriptPath -PathType Leaf) "$($workflow.Name) script reference"
    }
}

Write-Host 'Release tooling tests passed.'
