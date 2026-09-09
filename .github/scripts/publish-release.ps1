param(
    [Parameter(Mandatory)]
    [ValidateSet('development', 'stable')]
    [string] $Channel,
    [Parameter(Mandatory)]
    [string] $Repository,
    [Parameter(Mandatory)]
    [string] $ProductVersion,
    [Parameter(Mandatory)]
    [string] $BuildIdentity,
    [Parameter(Mandatory)]
    [string] $SourceSha,
    [Parameter(Mandatory)]
    [string] $AssetPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-tools.psm1') -Force

$version = ConvertFrom-CanonicalVersion $ProductVersion
$source = Assert-CanonicalCommitSha $SourceSha
$asset = (Resolve-Path -LiteralPath $AssetPath).Path
$assetName = Split-Path -Leaf $asset
$stableTag = "v$($version.Text)"
$tag = if ($Channel -eq 'stable') { $stableTag } else { $BuildIdentity }
if ($Channel -eq 'stable' -and $BuildIdentity -ne $stableTag) {
    throw "Stable build identity '$BuildIdentity' must equal '$stableTag'."
}
if ($Channel -eq 'development' -and $BuildIdentity -notmatch "\Av$([regex]::Escape($version.Text))-dev\.[1-9][0-9]*\z") {
    throw "Development build identity '$BuildIdentity' does not match product version $($version.Text)."
}

$encodedStableTag = ConvertTo-UrlComponent $stableTag
$encodedTag = ConvertTo-UrlComponent $tag
$stableRelease = Invoke-GitHubApi -Endpoint "repos/$Repository/releases/tags/$encodedStableTag" -AllowNotFound
$stableTagCommit = Get-GitHubTagCommit -Repository $Repository -Tag $stableTag
if ($Channel -eq 'development') {
    $decision = Get-DevelopmentPublicationDecision `
        -StableTagExists ($null -ne $stableTagCommit) `
        -StableReleaseExists ($null -ne $stableRelease)
    if (-not $decision.Publish) {
        Write-Host "::notice::Suppressed $BuildIdentity publication. $($decision.Reason)"
        exit 0
    }
}

$releases = @(Get-GitHubReleases -Repository $Repository)
$highestStable = Get-HighestStableRelease -Releases $releases -BelowVersion $null
$latestStable = if ($Channel -eq 'stable') {
    Invoke-GitHubApi -Endpoint "repos/$Repository/releases/latest" -AllowNotFound
} else {
    $null
}
$existingRelease = Invoke-GitHubApi -Endpoint "repos/$Repository/releases/tags/$encodedTag" -AllowNotFound
$existingTagCommit = Get-GitHubTagCommit -Repository $Repository -Tag $tag

if ($null -ne $existingTagCommit -and $existingTagCommit -ne $source) {
    throw "Tag '$tag' already points to $existingTagCommit instead of pinned source $source."
}
if ($null -ne $existingRelease) {
    if ($null -eq $existingTagCommit) {
        throw "Release '$tag' exists without a matching Git tag."
    }
    if ($existingRelease.draft) {
        throw "Release '$tag' exists as a draft and will not be overwritten."
    }
    $expectedPrerelease = $Channel -eq 'development'
    if ([bool]$existingRelease.prerelease -ne $expectedPrerelease) {
        throw "Release '$tag' has the wrong prerelease state and will not be modified."
    }
}

if ($Channel -eq 'stable') {
    if ($null -ne $latestStable) {
        if ($latestStable.tag_name -notmatch '^v(.+)$') {
            throw "Current Latest release tag '$($latestStable.tag_name)' is not canonical vMAJOR.MINOR.PATCH."
        }
        try {
            $latestVersion = ConvertFrom-CanonicalVersion $Matches[1] -AllowPackedOverflow
        } catch {
            throw "Current Latest release tag '$($latestStable.tag_name)' is not comparable as canonical SemVer: $($_.Exception.Message)"
        }
        if ((Compare-CanonicalVersion $version $latestVersion) -lt 0) {
            throw "Stable v$($version.Text) would regress Latest below $($latestStable.tag_name)."
        }
    }
    $highestVersion = if ($null -eq $highestStable) { $null } else { $highestStable.Version }
    $null = Get-StableVersionDecision `
        -RequestedVersion $version `
        -HighestStableVersion $highestVersion `
        -MatchingReleaseExists ($null -ne $existingRelease)
}

if ($null -eq $existingTagCommit) {
    $null = Invoke-GitHubApi -Endpoint "repos/$Repository/git/refs" -Method POST -Body @{
        ref = "refs/tags/$tag"
        sha = $source
    }
    Write-Host "Created tag $tag at $source."
}

if ($null -eq $existingRelease) {
    $previousStable = Get-HighestStableRelease -Releases $releases -BelowVersion $version
    $notesRequest = @{
        tag_name = $tag
        target_commitish = $source
    }
    if ($null -ne $previousStable) {
        $notesRequest.previous_tag_name = [string]$previousStable.Release.tag_name
    }
    $generated = Invoke-GitHubApi -Endpoint "repos/$Repository/releases/generate-notes" -Method POST -Body $notesRequest
    $provenance = "Built from ``$source``.`n`nBuild identity: ``$BuildIdentity``."
    $body = if ([string]::IsNullOrWhiteSpace([string]$generated.body)) {
        $provenance
    } else {
        "$provenance`n`n$($generated.body)"
    }
    $existingRelease = Invoke-GitHubApi -Endpoint "repos/$Repository/releases" -Method POST -Body @{
        tag_name = $tag
        target_commitish = $source
        name = $BuildIdentity
        body = $body
        draft = $false
        prerelease = ($Channel -eq 'development')
        make_latest = $(if ($Channel -eq 'stable') { 'true' } else { 'false' })
    }
    Write-Host "Created $Channel release $tag."
} elseif ($Channel -eq 'stable') {
    $existingRelease = Invoke-GitHubApi -Endpoint "repos/$Repository/releases/$($existingRelease.id)" -Method PATCH -Body @{
        make_latest = 'true'
    }
    Write-Host "Verified existing stable release $tag and marked it Latest."
} else {
    Write-Host "Verified existing development release $tag."
}

$assetNames = @($existingRelease.assets | ForEach-Object { [string]$_.name })
if ($assetNames -notcontains $assetName) {
    $null = Invoke-GitHubCli -ArgumentList @('release', 'upload', $tag, $asset, '--repo', $Repository)
    Write-Host "Uploaded missing asset $assetName."
} else {
    Write-Host "Asset $assetName already exists; preserving it without replacement."
}
