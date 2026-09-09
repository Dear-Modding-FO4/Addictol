param(
    [Parameter(Mandatory)]
    [string] $Repository,
    [Parameter(Mandatory)]
    [string] $SourceRef,
    [Parameter(Mandatory)]
    [string] $Version,
    [Parameter(Mandatory)]
    [string] $NextVersion,
    [Parameter(Mandatory)]
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-tools.psm1') -Force

$requested = ConvertFrom-CanonicalVersion $Version
$next = ConvertFrom-CanonicalVersion $NextVersion
if ((Compare-CanonicalVersion $next $requested) -le 0) {
    throw "Next development version $($next.Text) must be greater than release $($requested.Text)."
}

$sourceSha = Assert-CanonicalCommitSha ((git rev-parse HEAD).Trim())
if ($LASTEXITCODE -ne 0) {
    throw 'Could not resolve the checked-out release source.'
}
$existingTagCommit = Get-GitHubTagCommit -Repository $Repository -Tag "v$($requested.Text)"
$sourceSha = Resolve-ReleaseSourceSha -SourceSha $sourceSha -SourceRef $SourceRef -ExistingTagCommit $existingTagCommit
git checkout --detach $sourceSha
if ($LASTEXITCODE -ne 0) {
    throw "Could not check out pinned release source $sourceSha."
}
git fetch origin master --no-tags
if ($LASTEXITCODE -ne 0) {
    throw 'Could not fetch origin/master.'
}
$masterSha = Assert-CanonicalCommitSha ((git rev-parse origin/master).Trim())
if ($LASTEXITCODE -ne 0) {
    throw 'Could not resolve origin/master.'
}

git merge-base --is-ancestor $sourceSha $masterSha
if ($LASTEXITCODE -eq 1) {
    throw "Release source $sourceSha is not reachable from master $masterSha."
}
if ($LASTEXITCODE -ne 0) {
    throw 'Could not verify release-source reachability.'
}

$sourceVersion = Get-ProductVersionFromHeader 'Version/resource_version2.h'
if ((Compare-CanonicalVersion $sourceVersion $requested) -ne 0) {
    throw "Release source declares $($sourceVersion.Text), not requested $($requested.Text)."
}

$masterHeader = (git show "${masterSha}:Version/resource_version2.h") -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the version header from master $masterSha."
}
$masterVersion = Get-ProductVersionFromContent $masterHeader
Assert-NextDevelopmentVersion -NextVersion $next -MasterVersion $masterVersion -RecoveringRelease ($null -ne $existingTagCommit)

@(
    "source_sha=$sourceSha"
    "master_sha=$masterSha"
    "release_version=$($requested.Text)"
    "next_version=$($next.Text)"
    "master_version=$($masterVersion.Text)"
) | Add-Content -LiteralPath $OutputPath -Encoding utf8

Write-Host "Pinned release source $sourceSha (v$($requested.Text)); master was $masterSha (v$($masterVersion.Text))."
