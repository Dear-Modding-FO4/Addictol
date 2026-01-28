[int]$buildverint = Get-Content -Path ".\version\build_version.txt"
$buildverint+1 | out-file -FilePath ".\version\build_version.txt" -Force

$verfile = Get-Content -Path ".\version\resource_version2.tmp"
$verfile = $verfile -Replace "<BUILD>", $buildverint
$verfile | out-file -FilePath ".\version\resource_version2.h" -Force

$verfile = Get-Content -Path ".\version\fomod_info.tmp"
$verfile = $verfile -Replace "<BUILD>", $buildverint
$verfile | out-file -FilePath "..\Build\fomod\info.xml" -Force