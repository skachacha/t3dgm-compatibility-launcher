param(
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [Parameter(Mandatory=$true)][string]$BuildDirectory,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [switch]$UnpackedOnly
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$repository=[IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\','/')
$build=[IO.Path]::GetFullPath($BuildDirectory)
$output=[IO.Path]::GetFullPath($OutputDirectory)
foreach($path in @($build,$output)) {
    if($path.Equals($repository,[StringComparison]::OrdinalIgnoreCase) -or
       $path.StartsWith($repository+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Build and package outputs must be outside the source repository.'
    }
}
$resource=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'launcher.rc'))
$match=[regex]::Match($resource,'VALUE\s+"ProductVersion",\s*"([^"]+)"')
if(-not $match.Success) {throw 'ProductVersion is missing.'}
$version=$match.Groups[1].Value
if($version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {throw 'ProductVersion must use semantic versioning.'}
$name='T3DGM-Compatibility-'+$version
$folder=Join-Path $output $name
$archive=Join-Path $output ($name+'.zip')
if((Test-Path -LiteralPath $folder) -or (Test-Path -LiteralPath $archive)) {
    throw 'A package with this version already exists in the output directory. Use a new output directory.'
}
$readme=Join-Path $repository 'README.md'
$license=Join-Path $repository 'LICENSE'
$licences=Join-Path $repository 'LICENSES'
if(-not (Test-Path -LiteralPath $readme -PathType Leaf)) {throw 'README.md is missing.'}
if(-not (Test-Path -LiteralPath $license -PathType Leaf)) {throw 'LICENSE is missing.'}
if(-not (Test-Path -LiteralPath $licences -PathType Container)) {throw 'LICENSES is missing.'}
& (Join-Path $PSScriptRoot 'build.ps1') -Toolchain $Toolchain -Output $build
$executable=Join-Path $build 'T3DGM-Compatibility.exe'
$metadata=[Diagnostics.FileVersionInfo]::GetVersionInfo($executable)
if($metadata.ProductVersion -ne $version -or $metadata.FileVersion -ne $version) {
    throw 'Built executable version does not match the package version.'
}
[IO.Directory]::CreateDirectory($folder) | Out-Null
[IO.File]::Copy($executable,(Join-Path $folder 'T3DGM-Compatibility.exe'),$false)
[IO.File]::Copy($readme,(Join-Path $folder 'README.md'),$false)
[IO.File]::Copy($license,(Join-Path $folder 'LICENSE'),$false)
Copy-Item -LiteralPath $licences -Destination (Join-Path $folder 'LICENSES') -Recurse
if(-not $UnpackedOnly) {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($folder,$archive,[IO.Compression.CompressionLevel]::Optimal,$true)
}
[pscustomobject]@{Version=$version;Folder=$folder;Archive=$(if($UnpackedOnly){$null}else{$archive})}
