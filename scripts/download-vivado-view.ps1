param(
    [Parameter(Mandatory=$true, Position=0)]
    [string]$Remote,

    [Parameter(Mandatory=$false, Position=1)]
    [string]$DestDir = "project-x-vivado-view",

    [ValidateSet("min", "full", "reports-only")]
    [string]$Mode = "min",

    [string]$RemoteRoot = "~/ProjectFS/Project-X",

    [string]$Device = "xilinx_u55c_gen3x16_xdma_3_202210_1",

    [string]$Variant = "hybrid"
)

$ErrorActionPreference = "Stop"

function Copy-RemotePath {
    param(
        [string]$RemotePath,
        [string]$LocalPath,
        [switch]$Recursive
    )

    if ($Recursive) {
        scp -r "${Remote}:$RemotePath" "$LocalPath"
    } else {
        scp "${Remote}:$RemotePath" "$LocalPath"
    }
}

$Dest = Resolve-Path -Path (New-Item -ItemType Directory -Force -Path $DestDir)
$RemoteBuildDir = "$RemoteRoot/build/$Variant/hw/$Device"
$RemoteReportDir = "$RemoteRoot/reports/$Variant/hw/$Device"

Write-Host "Remote: $Remote"
Write-Host "Remote root: $RemoteRoot"
Write-Host "Variant: $Variant"
Write-Host "Destination: $Dest"
Write-Host "Mode: $Mode"

if ($Mode -eq "reports-only") {
    Copy-RemotePath "$RemoteReportDir" "$Dest/reports-hw" -Recursive
    Write-Host "Downloaded reports to: $Dest/reports-hw"
    exit 0
}

if ($Mode -eq "min") {
    New-Item -ItemType Directory -Force -Path "$Dest/dcp" | Out-Null
    Copy-RemotePath "$RemoteBuildDir/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/level0_wrapper_routed.dcp" "$Dest/dcp/"
    Copy-RemotePath "$RemoteReportDir" "$Dest/reports-hw" -Recursive
    Copy-RemotePath "$RemoteRoot/docs" "$Dest/docs" -Recursive
    Copy-RemotePath "$RemoteRoot/README.md" "$Dest/"
    Write-Host "Downloaded minimal Vivado view package to: $Dest"
    Write-Host "Open DCP with: vivado $Dest/dcp/level0_wrapper_routed.dcp"
    exit 0
}

Copy-RemotePath "$RemoteBuildDir/_x_temp/link/vivado/vpl" "$Dest/vpl" -Recursive
Copy-RemotePath "$RemoteReportDir" "$Dest/reports-hw" -Recursive
Copy-RemotePath "$RemoteRoot/docs" "$Dest/docs" -Recursive
Copy-RemotePath "$RemoteRoot/README.md" "$Dest/"
Write-Host "Downloaded full Vivado project package to: $Dest"
Write-Host "Open project with: vivado $Dest/vpl/prj/prj.xpr"
