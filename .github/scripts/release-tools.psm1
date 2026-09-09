Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertFrom-CanonicalVersion {
    param(
        [Parameter(Mandatory)]
        [string] $Version,
        [switch] $AllowPackedOverflow
    )

    $match = [regex]::Match($Version, '\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z')
    if (-not $match.Success) {
        throw "Version '$Version' is not canonical major.minor.patch."
    }

    $major = [uint64]$match.Groups[1].Value
    $minor = [uint64]$match.Groups[2].Value
    $patch = [uint64]$match.Groups[3].Value
    if (-not $AllowPackedOverflow) {
        if ($major -gt 255) {
            throw "Version major field $major exceeds CommonLib/F4SE's 8-bit limit (255)."
        }
        if ($minor -gt 255) {
            throw "Version minor field $minor exceeds CommonLib/F4SE's 8-bit limit (255)."
        }
        if ($patch -gt 4095) {
            throw "Version patch field $patch exceeds CommonLib/F4SE's 12-bit limit (4095)."
        }
    }

    [pscustomobject]@{
        Major = $major
        Minor = $minor
        Patch = $patch
        Text = "$major.$minor.$patch"
    }
}

function Compare-CanonicalVersion {
    param(
        [Parameter(Mandatory)]
        [object] $Left,
        [Parameter(Mandatory)]
        [object] $Right
    )

    foreach ($field in @('Major', 'Minor', 'Patch')) {
        if ($Left.$field -lt $Right.$field) {
            return -1
        }
        if ($Left.$field -gt $Right.$field) {
            return 1
        }
    }
    return 0
}

function Get-ProductVersionFromContent {
    param(
        [Parameter(Mandatory)]
        [string] $Content
    )

    $values = @{}
    foreach ($name in @('MAJOR', 'MINOR', 'PATCH', 'REVISION')) {
        $match = [regex]::Match($Content, "(?m)^\s*#define\s+VERSION_$name\s+(\d+)\s*$")
        if (-not $match.Success) {
            throw "Could not read VERSION_$name from the version header."
        }
        $values[$name] = [uint64]$match.Groups[1].Value
    }

    $version = ConvertFrom-CanonicalVersion "$($values.MAJOR).$($values.MINOR).$($values.PATCH)"
    if ($values.REVISION -gt 15) {
        throw "Version revision field $($values.REVISION) exceeds CommonLib/F4SE's 4-bit limit (15)."
    }
    if ($values.REVISION -ne 0) {
        throw 'VERSION_REVISION must remain 0 for a major.minor.patch product version.'
    }
    return $version
}

function Get-ProductVersionFromHeader {
    param(
        [Parameter(Mandatory)]
        [string] $Path
    )

    Get-ProductVersionFromContent (Get-Content -LiteralPath $Path -Raw)
}

function Set-ProductVersionInHeader {
    param(
        [Parameter(Mandatory)]
        [string] $Path,
        [Parameter(Mandatory)]
        [object] $Version
    )

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF
    $content = [System.Text.Encoding]::UTF8.GetString($bytes, $(if ($hasBom) { 3 } else { 0 }), $bytes.Length - $(if ($hasBom) { 3 } else { 0 }))

    $replacements = @{
        MAJOR = $Version.Major
        MINOR = $Version.Minor
        PATCH = $Version.Patch
    }
    foreach ($name in $replacements.Keys) {
        $pattern = "(?m)^(\s*#define\s+VERSION_$name\s+)\d+(\s*)$"
        $match = [regex]::Match($content, $pattern)
        if (-not $match.Success) {
            throw "Could not update VERSION_$name in '$Path'."
        }
        $value = $replacements[$name]
        $content = [regex]::Replace($content, $pattern, "`${1}$value`${2}", 1)
    }

    $encoding = [System.Text.UTF8Encoding]::new($hasBom)
    [System.IO.File]::WriteAllText($Path, $content, $encoding)
}

function Assert-CanonicalCommitSha {
    param(
        [Parameter(Mandatory)]
        [string] $Sha
    )

    if ($Sha -notmatch '\A[0-9a-fA-F]{40}\z') {
        throw "Commit '$Sha' is not a full 40-character SHA."
    }
    return $Sha.ToLowerInvariant()
}

function ConvertTo-UrlComponent {
    param(
        [Parameter(Mandatory)]
        [string] $Value
    )

    return [System.Uri]::EscapeDataString($Value)
}

function Resolve-ReleaseSourceSha {
    param(
        [Parameter(Mandatory)]
        [string] $SourceSha,
        [Parameter(Mandatory)]
        [string] $SourceRef,
        [AllowNull()]
        [string] $ExistingTagCommit
    )

    $source = Assert-CanonicalCommitSha $SourceSha
    if (-not $ExistingTagCommit) {
        return $source
    }

    $existing = Assert-CanonicalCommitSha $ExistingTagCommit
    if ($SourceRef -in @('master', 'refs/heads/master')) {
        Write-Host "Resuming release from its existing tag at $existing, not the current master tip."
        return $existing
    }
    if ($source -ne $existing) {
        throw "The existing release tag points to $existing, not selected source $source. Select the original SHA or tag to recover."
    }
    return $source
}

function Assert-NextDevelopmentVersion {
    param(
        [Parameter(Mandatory)]
        [object] $NextVersion,
        [Parameter(Mandatory)]
        [object] $MasterVersion,
        [Parameter(Mandatory)]
        [bool] $RecoveringRelease
    )

    $comparison = Compare-CanonicalVersion $NextVersion $MasterVersion
    if ($comparison -lt 0 -or ($comparison -eq 0 -and -not $RecoveringRelease)) {
        throw "Next development version $($NextVersion.Text) must exceed master $($MasterVersion.Text), or equal it when recovering an existing release."
    }
}

function Get-DevelopmentPublicationDecision {
    param(
        [Parameter(Mandatory)]
        [bool] $StableTagExists,
        [Parameter(Mandatory)]
        [bool] $StableReleaseExists
    )

    if ($StableTagExists -or $StableReleaseExists) {
        return [pscustomobject]@{
            Publish = $false
            Reason = 'The matching stable product version already has a tag or release.'
        }
    }
    return [pscustomobject]@{
        Publish = $true
        Reason = 'No matching stable product tag or release exists.'
    }
}

function Get-StableVersionDecision {
    param(
        [Parameter(Mandatory)]
        [object] $RequestedVersion,
        [AllowNull()]
        [object] $HighestStableVersion,
        [Parameter(Mandatory)]
        [bool] $MatchingReleaseExists
    )

    if ($null -eq $HighestStableVersion) {
        return [pscustomobject]@{ Publish = $true; Recovery = $false }
    }

    $comparison = Compare-CanonicalVersion $RequestedVersion $HighestStableVersion
    if ($comparison -lt 0) {
        throw "Stable v$($RequestedVersion.Text) would regress Latest below v$($HighestStableVersion.Text)."
    }
    if ($comparison -eq 0 -and -not $MatchingReleaseExists) {
        throw "Stable v$($RequestedVersion.Text) is not newer than the highest existing stable release."
    }
    return [pscustomobject]@{
        Publish = -not $MatchingReleaseExists
        Recovery = $MatchingReleaseExists
    }
}

function Invoke-Process {
    param(
        [Parameter(Mandatory)]
        [string] $FilePath,
        [Parameter(Mandatory)]
        [string[]] $ArgumentList,
        [AllowNull()]
        [string] $StandardInput
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.UseShellExecute = $false
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.RedirectStandardInput = $null -ne $StandardInput
    foreach ($argument in $ArgumentList) {
        $startInfo.ArgumentList.Add($argument)
    }

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) {
        throw "Failed to start '$FilePath'."
    }
    if ($null -ne $StandardInput) {
        $process.StandardInput.Write($StandardInput)
        $process.StandardInput.Close()
    }
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()

    [pscustomobject]@{
        ExitCode = $process.ExitCode
        StdOut = $stdout.Trim()
        StdErr = $stderr.Trim()
    }
}

function Invoke-GitHubApi {
    param(
        [Parameter(Mandatory)]
        [string] $Endpoint,
        [ValidateSet('GET', 'POST', 'PATCH', 'DELETE')]
        [string] $Method = 'GET',
        [AllowNull()]
        [object] $Body,
        [switch] $AllowNotFound
    )

    $arguments = @('api', $Endpoint, '--method', $Method)
    $input = $null
    if ($null -ne $Body) {
        $arguments += @('--input', '-')
        $input = $Body | ConvertTo-Json -Depth 20 -Compress
    }
    $result = Invoke-Process -FilePath 'gh' -ArgumentList $arguments -StandardInput $input
    if ($result.ExitCode -ne 0) {
        if ($AllowNotFound -and $result.StdErr -match 'HTTP 404') {
            return $null
        }
        throw "GitHub API $Method $Endpoint failed: $($result.StdErr)"
    }
    if ([string]::IsNullOrWhiteSpace($result.StdOut)) {
        return $null
    }
    return $result.StdOut | ConvertFrom-Json -Depth 30
}

function Invoke-GitHubCli {
    param(
        [Parameter(Mandatory)]
        [string[]] $ArgumentList
    )

    $result = Invoke-Process -FilePath 'gh' -ArgumentList $ArgumentList -StandardInput $null
    if ($result.ExitCode -ne 0) {
        throw "gh $($ArgumentList -join ' ') failed: $($result.StdErr)"
    }
    return $result.StdOut
}

function Get-GitHubReleases {
    param(
        [Parameter(Mandatory)]
        [string] $Repository
    )

    $all = [System.Collections.Generic.List[object]]::new()
    for ($page = 1; ; $page++) {
        $batch = @(Invoke-GitHubApi -Endpoint "repos/$Repository/releases?per_page=100&page=$page")
        foreach ($release in $batch) {
            $all.Add($release)
        }
        if ($batch.Count -lt 100) {
            break
        }
    }
    return @($all)
}

function Get-HighestStableRelease {
    param(
        [Parameter(Mandatory)]
        [AllowEmptyCollection()]
        [object[]] $Releases,
        [AllowNull()]
        [object] $BelowVersion
    )

    $best = $null
    foreach ($release in $Releases) {
        if ($release.draft -or $release.prerelease -or $release.tag_name -notmatch '^v(.+)$') {
            continue
        }
        $tagVersion = $Matches[1]
        if ($tagVersion -notmatch '\A(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\z') {
            Write-Warning "Ignoring noncanonical stable release '$($release.tag_name)' when ordering versions."
            continue
        }
        $version = ConvertFrom-CanonicalVersion $tagVersion -AllowPackedOverflow
        if ($null -ne $BelowVersion -and (Compare-CanonicalVersion $version $BelowVersion) -ge 0) {
            continue
        }
        if ($null -eq $best -or (Compare-CanonicalVersion $version $best.Version) -gt 0) {
            $best = [pscustomobject]@{ Release = $release; Version = $version }
        }
    }
    return $best
}

function Get-GitHubTagCommit {
    param(
        [Parameter(Mandatory)]
        [string] $Repository,
        [Parameter(Mandatory)]
        [string] $Tag
    )

    $encodedTag = ConvertTo-UrlComponent $Tag
    $reference = Invoke-GitHubApi -Endpoint "repos/$Repository/git/ref/tags/$encodedTag" -AllowNotFound
    if ($null -eq $reference) {
        return $null
    }

    $object = $reference.object
    for ($depth = 0; $depth -lt 8; $depth++) {
        if ($object.type -eq 'commit') {
            return Assert-CanonicalCommitSha ([string]$object.sha)
        }
        if ($object.type -ne 'tag') {
            throw "Tag '$Tag' points to unsupported Git object type '$($object.type)'."
        }
        $tagObject = Invoke-GitHubApi -Endpoint "repos/$Repository/git/tags/$($object.sha)"
        $object = $tagObject.object
    }
    throw "Tag '$Tag' has too many nested annotated tags."
}

Export-ModuleMember -Function *
