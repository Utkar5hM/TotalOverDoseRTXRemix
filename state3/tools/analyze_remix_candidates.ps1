param(
  [string]$CandidatePath = "rtx-remix\logs\tod-remix-candidates.tsv",
  [string]$OutPath = "re_docs\rtx_remix_candidate_report.md"
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $CandidatePath)) {
  throw "Candidate log not found: $CandidatePath"
}

$rows = Import-Csv -LiteralPath $CandidatePath -Delimiter "`t" |
  Where-Object { $_.role -and $_.role -ne "role" }

function Normalize-HashToken {
  param([string]$Token)

  if ([string]::IsNullOrWhiteSpace($Token)) {
    return $null
  }

  $trimmed = $Token.Trim()
  $negative = $trimmed.StartsWith("-")
  if ($negative) {
    $trimmed = $trimmed.Substring(1)
  }
  if ($trimmed.StartsWith("0x", [System.StringComparison]::OrdinalIgnoreCase)) {
    $trimmed = $trimmed.Substring(2)
  }
  if ($trimmed.Length -eq 0) {
    return $null
  }

  return "0x$($trimmed.ToUpperInvariant().PadLeft(16, '0'))"
}

$generated = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
$lines = New-Object System.Collections.Generic.List[string]

$lines.Add("# RTX Remix Candidate Report")
$lines.Add("")
$lines.Add("Generated: $generated")
$lines.Add("")
$lines.Add("Source: ``$CandidatePath``")
$lines.Add("")
$lines.Add("Important: ``rtxTextureHash`` is computed with the normal dxvk-remix texture upload hash path for common 2D formats: XXH3 over mip 0's packed CPU upload buffer. It should match ``rtx.uiTextures`` unless ``rtx.useObsoleteHashOnTextureUpload`` is enabled or the texture uses an unsupported/converted edge case.")
$lines.Add("")

if ($rows.Count -eq 0) {
  $lines.Add("No candidate rows were found.")
} else {
  $lines.Add("Total rows: $($rows.Count)")
  $lines.Add("")

  foreach ($role in @("ui_candidate", "hud_candidate", "effect_candidate", "video_candidate", "world_candidate", "world_unshadowed_candidate")) {
    $roleRows = @($rows | Where-Object { $_.role -eq $role })
    $lines.Add("## $role")
    $lines.Add("")

    if ($roleRows.Count -eq 0) {
      $lines.Add("No rows.")
      $lines.Add("")
      continue
    }

    $groups = $roleRows |
      Group-Object role, width, height, format, levels, rtxTextureHash |
      Sort-Object Count -Descending |
      Select-Object -First 40

    $lines.Add("| Count | Size | Format | Levels | RTX Texture Hash | First Draw | Texture |")
    $lines.Add("|---:|---|---|---:|---|---|---|")

    foreach ($group in $groups) {
      $first = $group.Group[0]
      $size = "$($first.width)x$($first.height)"
      $draw = "$($first.draw) #$($first.drawCount)"
      $lines.Add("| $($group.Count) | $size | $($first.format) | $($first.levels) | ``$($first.rtxTextureHash)`` | $draw | ``$($first.texture)`` |")
    }

    $lines.Add("")
  }
}

$allUiHashes = @($rows |
  Where-Object { $_.role -eq "ui_candidate" -and $_.rtxTextureHash -and $_.rtxTextureHash -ne "0x0000000000000000" } |
  ForEach-Object { Normalize-HashToken $_.rtxTextureHash } |
  Where-Object { $_ } |
  Select-Object -Unique)

$nonUiHashes = @($rows |
  Where-Object { $_.role -ne "ui_candidate" -and $_.rtxTextureHash -and $_.rtxTextureHash -ne "0x0000000000000000" } |
  ForEach-Object { Normalize-HashToken $_.rtxTextureHash } |
  Where-Object { $_ } |
  Select-Object -Unique)

$highConfidenceUiHashes = @($rows |
  Where-Object {
    $_.role -eq "ui_candidate" -and
    $_.rtxTextureHash -and
    $_.rtxTextureHash -ne "0x0000000000000000" -and
    $_.levels -eq "1" -and
    (
      ($_.format -eq "A8R8G8B8" -and (
        ($_.width -eq "1024" -and $_.height -eq "128") -or
        ($_.width -eq "512" -and $_.height -eq "32")
      )) -or
      ($_.format -eq "DXT1" -and $_.width -eq "1024" -and $_.height -eq "512") -or
      ($_.format -eq "DXT1" -and $_.width -eq "512" -and $_.height -eq "512")
    )
  } |
  ForEach-Object { Normalize-HashToken $_.rtxTextureHash } |
  Where-Object { $_ -and ($nonUiHashes -notcontains $_) } |
  Select-Object -Unique)

$configPath = "rtx.conf"
$existingPositiveHashes = @()
$existingNegativeHashes = @()
if (Test-Path -LiteralPath $configPath) {
  $configText = Get-Content -LiteralPath $configPath -Raw
  $match = [regex]::Match($configText, "(?m)^\s*rtx\.uiTextures\s*=\s*(.+)$")
  if ($match.Success) {
    $tokens = @($match.Groups[1].Value -split "," | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    $existingPositiveHashes = @($tokens | Where-Object { -not $_.StartsWith("-") } | ForEach-Object { Normalize-HashToken $_ } | Where-Object { $_ } | Select-Object -Unique)
    $existingNegativeHashes = @($tokens | Where-Object { $_.StartsWith("-") } | ForEach-Object { Normalize-HashToken $_ } | Where-Object { $_ } | Select-Object -Unique)
    $lines.Add("## Current Remix UI Hashes")
    $lines.Add("")
    $lines.Add("Positive entries currently active as UI: $($existingPositiveHashes.Count)")
    $lines.Add("")
    foreach ($hash in $existingPositiveHashes) {
      $lines.Add("- ``$hash``")
    }
    $lines.Add("")
    $lines.Add("Negative entries currently remove hashes from UI classification: $($existingNegativeHashes.Count)")
    $lines.Add("")
    foreach ($hash in $existingNegativeHashes) {
      $lines.Add("- ``-$hash``")
    }
    $lines.Add("")
  }
}

if ($allUiHashes.Count -gt 0) {
  $newHighConfidence = @($highConfidenceUiHashes | Where-Object { $existingPositiveHashes -notcontains $_ })
  $combinedPositiveHashes = @($existingPositiveHashes + $highConfidenceUiHashes | Select-Object -Unique)
  $combinedNegativeHashes = @($existingNegativeHashes | Where-Object { $combinedPositiveHashes -notcontains $_ })
  $configuredHashes = @($existingPositiveHashes + $existingNegativeHashes | Select-Object -Unique)

  $lines.Add("## Suggested UI Config")
  $lines.Add("")
  $lines.Add("Raw unique UI candidate hashes found this run: $($allUiHashes.Count)")
  $lines.Add("High-confidence atlas/font hashes from this run: $($highConfidenceUiHashes.Count)")
  $lines.Add("New high-confidence hashes not already positive: $($newHighConfidence.Count)")
  $lines.Add("")
  if ($newHighConfidence.Count -gt 0) {
    $lines.Add("New high-confidence UI hashes:")
    $lines.Add("")
    foreach ($hash in $newHighConfidence) {
      $lines.Add("- ``$hash``")
    }
    $lines.Add("")
  }

  $mergedEntries = @($combinedPositiveHashes + ($combinedNegativeHashes | ForEach-Object { "-$_" }))
  $lines.Add("Merged config snippet:")
  $lines.Add("")
  $lines.Add("````ini")
  $lines.Add("rtx.uiTextures = $($mergedEntries -join ', ')")
  $lines.Add("````")
  $lines.Add("")

  $reviewRows = @($rows |
    Where-Object {
      $_.role -eq "ui_candidate" -and
      $_.rtxTextureHash -and
      $_.rtxTextureHash -ne "0x0000000000000000" -and
      ($configuredHashes -notcontains (Normalize-HashToken $_.rtxTextureHash))
    } |
    Group-Object rtxTextureHash, width, height, format, levels |
    Sort-Object Count -Descending |
    Select-Object -First 80)

  if ($reviewRows.Count -gt 0) {
    $lines.Add("## UI Candidates For Visual Review")
    $lines.Add("")
    $lines.Add("These are not automatically added. Inspect the matching DDS dumps after a run with HUD/pause visible.")
    $lines.Add("")
    $lines.Add("| Count | Size | Format | Levels | Hash | Expected DDS Name |")
    $lines.Add("|---:|---|---|---:|---|---|")
    foreach ($group in $reviewRows) {
      $first = $group.Group[0]
      $hash = Normalize-HashToken $first.rtxTextureHash
      $size = "$($first.width)x$($first.height)"
      $dds = "ui_candidate_$($hash.Substring(2))_$($first.width)x$($first.height)_$($first.format).dds"
      $lines.Add("| $($group.Count) | $size | $($first.format) | $($first.levels) | ``$hash`` | ``$dds`` |")
    }
    $lines.Add("")
  }
}

$hudRows = @($rows |
  Where-Object {
    $_.role -eq "hud_candidate" -and
    $_.rtxTextureHash -and
    $_.rtxTextureHash -ne "0x0000000000000000"
  } |
  Group-Object rtxTextureHash, width, height, format, levels |
  Sort-Object Count -Descending |
  Select-Object -First 80)

if ($hudRows.Count -gt 0) {
  $lines.Add("## In-Game HUD Candidates For Visual Review")
  $lines.Add("")
  $lines.Add("These are screen-space (RHW) draws issued after the 3D world was drawn in the frame (``worldDrawn = 1``) - the in-game HUD overlay layer, as opposed to menu/fullscreen UI drawn before any world geometry. Inspect the matching DDS dumps in ``rtx-remix\logs\tod-candidate-hud`` after a run with the HUD visible, then add only confirmed HUD atlas/font/icon hashes to ``rtx.uiTextures`` (Remix rasterizes tagged textures instead of skipping them as pre-transformed).")
  $lines.Add("")
  $lines.Add("| Count | Size | Format | Levels | First FrameDrawIndex | ProjOrtho | Hash | Expected DDS Name |")
  $lines.Add("|---:|---|---|---:|---:|:-:|---|---|")
  foreach ($group in $hudRows) {
    $first = $group.Group[0]
    $hash = Normalize-HashToken $first.rtxTextureHash
    $size = "$($first.width)x$($first.height)"
    $dds = "hud_candidate_$($hash.Substring(2))_$($first.width)x$($first.height)_$($first.format).dds"
    $lines.Add("| $($group.Count) | $size | $($first.format) | $($first.levels) | $($first.frameDrawIndex) | $($first.projOrtho) | ``$hash`` | ``$dds`` |")
  }
  $lines.Add("")
}

$worldRows = @($rows |
  Where-Object {
    $_.role -eq "world_candidate" -and
    $_.rtxTextureHash -and
    $_.rtxTextureHash -ne "0x0000000000000000"
  } |
  Group-Object rtxTextureHash, width, height, format, levels |
  Sort-Object Count -Descending |
  Select-Object -First 200)

if ($worldRows.Count -gt 0) {
  $lines.Add("## World LOD Candidates For Visual Review")
  $lines.Add("")
  $lines.Add("These are 3D fixed-function world draws (not screen-space, not overlay sprites). Distant LOD / billboard impostors live here. When Remix cannot reconstruct a billboard quad it raytraces the raw degenerate geometry, producing spikes (the mission-3 artifact). Inspect the matching DDS dumps in ``rtx-remix\logs\tod-candidate-world`` after a run in the affected area, identify the skyline/foliage/impostor atlas, and add only those confirmed hashes to ``rtx.hideInstanceTextures`` in the Remix config (this removes the broken instances from raytracing). ``AlphaBlend`` / low ``primitiveCount`` rows are the most billboard-like.")
  $lines.Add("")
  $lines.Add("| Count | Size | Format | Levels | AlphaBlend | ZWrite | PrimType | Hash | Expected DDS Name |")
  $lines.Add("|---:|---|---|---:|:-:|:-:|---:|---|---|")
  foreach ($group in $worldRows) {
    $first = $group.Group[0]
    $hash = Normalize-HashToken $first.rtxTextureHash
    $size = "$($first.width)x$($first.height)"
    $dds = "world_candidate_$($hash.Substring(2))_$($first.width)x$($first.height)_$($first.format).dds"
    $lines.Add("| $($group.Count) | $size | $($first.format) | $($first.levels) | $($first.alphaBlendEnable) | $($first.zWriteEnable) | $($first.primitiveType) | ``$hash`` | ``$dds`` |")
  }
  $lines.Add("")
}

$outDir = Split-Path -Parent $OutPath
if ($outDir -and -not (Test-Path -LiteralPath $outDir)) {
  New-Item -ItemType Directory -Path $outDir | Out-Null
}

Set-Content -LiteralPath $OutPath -Value $lines -Encoding ASCII
Write-Host "Wrote $OutPath"
