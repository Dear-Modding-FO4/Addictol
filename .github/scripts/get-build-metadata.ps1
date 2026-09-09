param(
    [Parameter(Mandatory)]
    [ValidateSet('development', 'stable', 'validation')]
    [string] $Kind,
    [string] $BuildNumber,
    [string] $ExpectedVersion,
    [Parameter(Mandatory)]
    [string] $SourceSha,
    [Parameter(Mandatory)]
    [string] $RepositoryRoot,
    [Parameter(Mandatory)]
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-tools.psm1') -Force

$source = Assert-CanonicalCommitSha $SourceSha
$version = Get-ProductVersionFromHeader (Join-Path $RepositoryRoot 'Version\resource_version2.h')
if ($ExpectedVersion) {
    $expected = ConvertFrom-CanonicalVersion $ExpectedVersion
    if ((Compare-CanonicalVersion $version $expected) -ne 0) {
        throw "Source header version $($version.Text) does not match requested version $($expected.Text)."
    }
}

switch ($Kind) {
    'development' {
        if ($BuildNumber -notmatch '\A[1-9][0-9]*\z') {
            throw "Development build number '$BuildNumber' is not a positive canonical integer."
        }
        $identity = "v$($version.Text)-dev.$BuildNumber"
    }
    'stable' {
        if (-not $ExpectedVersion) {
            throw 'Stable builds require ExpectedVersion.'
        }
        $identity = "v$($version.Text)"
    }
    default {
        $identity = "validation-$($source.Substring(0, 8))"
    }
}

$bundleName = "Addictol-$identity"
$assetName = "$bundleName.zip"
$artifactName = "package-$identity"
@(
    "product_version=$($version.Text)"
    "build_identity=$identity"
    "bundle_name=$bundleName"
    "asset_name=$assetName"
    "artifact_name=$artifactName"
) | Add-Content -LiteralPath $OutputPath -Encoding utf8

Write-Host "Product version: $($version.Text)"
Write-Host "Build identity: $identity"
