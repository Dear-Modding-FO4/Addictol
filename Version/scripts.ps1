[int]$buildverint = Get-Content -Path "..\Version\build_version.txt"
$buildverint+1 | Out-File -FilePath "..\Version\build_version.txt" -Force

$verfile = Get-Content -Path "..\Version\resource_version2.tmp" -Encoding utf8
$verfile = $verfile -Replace "<BUILD>", $buildverint
$verfile | Out-File -FilePath "..\Version\resource_version2.h" -Force -Encoding utf8

#$verfile = Get-Content -Path ".\Version\fomod_info.tmp"
#$verfile = $verfile -Replace "<BUILD>", $buildverint
#$verfile | Out-File -FilePath "..\Build\fomod\info.xml" -Force
