param(
    [Parameter(Mandatory)]
    [string] $Repository,
    [Parameter(Mandatory)]
    [string] $Branch,
    [Parameter(Mandatory)]
    [string] $SourceSha,
    [Parameter(Mandatory)]
    [string] $ReleaseVersion,
    [Parameter(Mandatory)]
    [string] $NextVersion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'release-tools.psm1') -Force

$released = ConvertFrom-CanonicalVersion $ReleaseVersion
$next = ConvertFrom-CanonicalVersion $NextVersion
$source = Assert-CanonicalCommitSha $SourceSha
$encodedBranch = ConvertTo-UrlComponent $Branch
$branchInfo = Invoke-GitHubApi -Endpoint "repos/$Repository/branches/$encodedBranch"
if ($branchInfo.commit.sha -ne $source) {
    throw "Bump branch '$Branch' moved from expected commit $source. Rerun the stable workflow."
}
$owner = $Repository.Split('/')[0]
$head = "$owner`:$Branch"
$encodedHead = ConvertTo-UrlComponent $head
$existing = @(Invoke-GitHubApi -Endpoint "repos/$Repository/pulls?state=open&base=master&head=$encodedHead")
if ($existing.Count -gt 1) {
    throw "More than one open pull request exists for '$Branch'."
}
if ($existing.Count -eq 1) {
    Write-Host "Pull request already exists: $($existing[0].html_url)"
} else {
    $body = @"
Prepares ``master`` for development after stable v$($released.Text).

- sets the explicit product version to ``$($next.Text)``
- keeps the DLL/F4SE numeric version independent of the Actions run number
- receives MSBuild and xmake checks through explicitly dispatched validation workflows

Wait for both checks, then review and merge normally. The merge push to ``master`` will publish the first ``v$($next.Text)-dev.RUN`` prerelease.
"@

    $pull = Invoke-GitHubApi -Endpoint "repos/$Repository/pulls" -Method POST -Body @{
        title = "chore(release): start development for v$($next.Text)"
        head = $Branch
        base = 'master'
        body = $body
    }
    Write-Host "Created pull request $($pull.html_url)."
}

# Dispatch events run with GITHUB_TOKEN and attach checks to the bump branch's commit.
foreach ($workflow in @('MSBuild.yml', 'xmake.yml')) {
    $null = Invoke-GitHubApi -Endpoint "repos/$Repository/actions/workflows/$workflow/dispatches" -Method POST -Body @{
        ref = $Branch
    }
    Write-Host "Dispatched $workflow for $Branch at $source."
}
