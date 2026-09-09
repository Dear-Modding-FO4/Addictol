param(
    [Parameter(Mandatory)]
    [string] $Repository,
    [Parameter(Mandatory)]
    [string] $ReleaseVersion,
    [Parameter(Mandatory)]
    [string] $NextVersion,
    [Parameter(Mandatory)]
    [string] $OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-tools.psm1') -Force

$released = ConvertFrom-CanonicalVersion $ReleaseVersion
$next = ConvertFrom-CanonicalVersion $NextVersion
if ((Compare-CanonicalVersion $next $released) -le 0) {
    throw "Next development version $($next.Text) must be greater than released version $($released.Text)."
}

$branch = "release-prepare-v$($next.Text)"
git fetch origin master --no-tags
if ($LASTEXITCODE -ne 0) {
    throw 'Could not fetch origin/master before preparing the version bump.'
}

$masterSha = Assert-CanonicalCommitSha ((git rev-parse origin/master).Trim())
$masterHeader = (git show "${masterSha}:Version/resource_version2.h") -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the version header from master $masterSha."
}
$masterVersion = Get-ProductVersionFromContent $masterHeader
$comparison = Compare-CanonicalVersion $masterVersion $next
if ($comparison -gt 0) {
    throw "Master already declares $($masterVersion.Text), which is newer than requested next version $($next.Text)."
}
if ($comparison -eq 0) {
    @(
        'bump_sha='
        'bump_branch='
        'bump_required=false'
    ) | Add-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Master already declares next development version $($next.Text); no bump PR is needed."
    exit 0
}

$encodedBranch = ConvertTo-UrlComponent $branch
$branchInfo = Invoke-GitHubApi -Endpoint "repos/$Repository/branches/$encodedBranch" -AllowNotFound
if ($null -ne $branchInfo) {
    $branchSha = Assert-CanonicalCommitSha ([string]$branchInfo.commit.sha)
    git fetch origin "refs/heads/${branch}:refs/remotes/origin/${branch}" --no-tags
    if ($LASTEXITCODE -ne 0) {
        throw "Could not fetch existing bump branch '$branch'."
    }
    $branchVersionContent = (git show "${branchSha}:Version/resource_version2.h") -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw "Could not read the version header from existing bump branch '$branch'."
    }
    $branchVersion = Get-ProductVersionFromContent $branchVersionContent
    if ((Compare-CanonicalVersion $branchVersion $next) -ne 0) {
        throw "Existing branch '$branch' declares $($branchVersion.Text), not $($next.Text)."
    }
    $parentSha = (git rev-parse "${branchSha}^").Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "Existing bump branch '$branch' has no parent commit."
    }
    git merge-base --is-ancestor $parentSha origin/master
    if ($LASTEXITCODE -eq 1) {
        throw "Existing bump branch '$branch' is not based on a commit reachable from current master."
    }
    if ($LASTEXITCODE -ne 0) {
        throw "Could not validate the base of existing bump branch '$branch'."
    }
    $changed = @((git diff --name-only $parentSha $branchSha) | Where-Object { $_ })
    if ($LASTEXITCODE -ne 0 -or $changed.Count -ne 1 -or $changed[0] -ne 'Version/resource_version2.h') {
        throw "Existing bump branch '$branch' is not the expected one-file version change."
    }
    @(
        "bump_sha=$branchSha"
        "bump_branch=$branch"
        'bump_required=true'
    ) | Add-Content -LiteralPath $OutputPath -Encoding utf8
    Write-Host "Reusing existing bump branch $branch at $branchSha."
    exit 0
}

git checkout -B $branch $masterSha
if ($LASTEXITCODE -ne 0) {
    throw "Could not create bump branch '$branch' from master $masterSha."
}
Set-ProductVersionInHeader -Path 'Version/resource_version2.h' -Version $next
git add -- Version/resource_version2.h
$changed = @((git diff --cached --name-only) | Where-Object { $_ })
if ($LASTEXITCODE -ne 0 -or $changed.Count -ne 1 -or $changed[0] -ne 'Version/resource_version2.h') {
    throw 'Version bump unexpectedly changed files other than Version/resource_version2.h.'
}
git -c user.name='github-actions[bot]' -c user.email='41898282+github-actions[bot]@users.noreply.github.com' `
    commit -m "chore(release): start development for v$($next.Text)"
if ($LASTEXITCODE -ne 0) {
    throw 'Could not commit the next development version.'
}
$bumpSha = Assert-CanonicalCommitSha ((git rev-parse HEAD).Trim())
git push origin "HEAD:refs/heads/$branch"
if ($LASTEXITCODE -ne 0) {
    throw "Could not push bump branch '$branch'."
}

@(
    "bump_sha=$bumpSha"
    "bump_branch=$branch"
    'bump_required=true'
) | Add-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Prepared bump branch $branch at $bumpSha."
