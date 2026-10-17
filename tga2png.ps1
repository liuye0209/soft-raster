Add-Type -AssemblyName System.Drawing
Get-ChildItem -Path (Join-Path $PSScriptRoot 'out') -Filter *.bmp | ForEach-Object {
    $pngPath = [System.IO.Path]::ChangeExtension($_.FullName, '.png')
    $img = [System.Drawing.Image]::FromFile($_.FullName)
    $img.Save($pngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $img.Dispose()
    Write-Host "$($_.Name) -> $([System.IO.Path]::GetFileName($pngPath))"
}
