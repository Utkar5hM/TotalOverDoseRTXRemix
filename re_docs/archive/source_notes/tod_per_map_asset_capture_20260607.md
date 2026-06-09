# Total Overdose per-map asset capture

Date: 2026-06-07

## Objective

Build the broadest practical RTX Remix texture set for each TOD map, including
alternate LODs and resources that only become resident near a sector, interior,
vehicle, destructible, or mission-script state.

The clean solution is a union of:

1. Archive inventory: identifies the blocks that belong to each map.
2. Runtime loaded-texture enumeration: hashes and dumps everything TOD currently
   owns, across several native LOD-factor values.
3. Spatial/script traversal: causes TOD's resource controller to stream sectors
   and mission-specific objects that cannot exist from a single camera position.

## Reverse-engineering findings

### Native LOD control

Ghidra and the recovered engine source agree on:

```text
Script::LodFactor                 0x00A12090
Texture::DrawAllTextures()        0x00463850
Texture::TexturesMap              0x00A39F50
```

The script command `set_lod_factor(number)` directly writes the float at
`0x00A12090`. TOD's quadtree/resource selection uses this factor separately from
the camera far clip. Increasing the far clip alone therefore cannot expose every
LOD or instantiate every proximity-controlled resource.

`Texture::DrawAllTextures()` iterates TOD's current texture map. It is suitable
for enumerating every texture already loaded by the engine, but it does not load
unvisited sectors by itself.

The recovered command database also contains:

```text
command_instant_resources(truth)
command_force_update_resource_PH_lists
command_textureset_0 ... command_textureset_7
forcelodcalculation
```

These are not safe global switches. Ghidra shows `forcelodcalculation`
(`0x0088D0F0`) is a `Node` method that requires a valid entity instance.
`UseForceLOD` and `ForceLODLevel` are entity properties, not one map-wide
variable. The resource commands likewise require their owning script/entity
context. Calling them as raw global functions would repeat the unsafe
force-load pattern, so they are not baked into the shim.

### Resource residency

TOD has separate spatial and scripted resource allocation. Some assets only
become resident after:

- entering a spatial sector or interior;
- approaching a prop or LOD group;
- spawning/driving a vehicle;
- triggering destructibles or effects;
- advancing a mission/script state;
- entering one of the map's submaps.

Consequently, there is no truthful one-position "all map assets" capture. The
complete practical set is the union of controlled passes through those states.

### Archive inventory

`blocks.naz` was converted from a temporary copy; the installed archive was not
modified. The generated manifest is:

```text
re_docs\tod_asset_archive_manifest.json
```

It contains 756 relevant block/texture entries and identifies nine shared maps:

| Map | Main blocks | Submaps |
|---|---:|---:|
| map01 | 1 | 2 |
| map02 | 1 | 5 |
| map03 | 1 | 4 |
| map04 | 1 | 2 |
| map06 | 1 | 2 |
| map07 | 1 | 2 |
| map08 | 1 | 1 |
| map09 | 1 | 2 |
| map10 | 1 | 1 |

There is no shared `map05` block in this archive. Mission/playerdata blocks that
do not encode a map name remain in the manifest as global entries and must be
associated through runtime passes.

The recovered `AssetBlockReader` is incomplete for the shipped blocks. It builds,
but crashes with access violation `0xC0000005` on real map submaps. Therefore
`dump-blocks` is explicitly experimental and is not part of the reliable path.

## Runtime capture mode

The active shim now has an INI-controlled diagnostic mode in:

```text
scripts\TODRemixShim.ini
```

```ini
[AssetCapture]
Enabled=0
StartDelayFrames=180
SettleFrames=120
ProbeFrames=3
LodFactors=0.05,1.0,8.0
```

When `Enabled` changes from `0` to `1`, the shim:

1. Saves TOD's current `Script::LodFactor`.
2. Waits for the configured start delay.
3. Applies each configured LOD factor.
4. Waits for resource/LOD state to settle.
5. Calls TOD's own `Texture::DrawAllTextures()` for the configured probe frames.
6. Computes the same XXH3 level-0 texture hash used by the existing shim.
7. Dumps each unique DDS to
   `rtx-remix\logs\tod-forced-texture-probe`.
8. Restores the original LOD factor after completion or if capture is disabled.

Normal rendering behavior is unchanged while `Enabled=0`. No map names, texture
hashes, or content-specific rules are compiled into the ASI.

Completion/status is written to:

```text
rtx-remix\logs\tod-asset-capture.log
```

Before another probe at a new location, toggle `Enabled=0`, then back to `1`.

## Automated AssetBlock crawler

The manual traversal requirement was removed for every resource represented by
TOD's registered AssetBlocks. The shim now has an INI-controlled crawler:

```ini
[AssetCrawler]
Enabled=0
IncludeMaps=1
IncludeMissions=1
IncludeCutscenes=0
IncludePlayerdata=1
LoadSettleFrames=300
```

The crawler uses TOD's own scene/resource path rather than calling archive
readers or raw resource constructors:

1. Bootstrap `/data/Overdose_THE_GAME/Overdose.scene` through native
   `Scene::Reset`, `Scene::Destroy`, `KapowEngine::OpenScene`, and
   `Scene::Start`.
2. Enumerate the live Folder registries for the enabled block IDs.
3. Load each registered Folder through `Scene::LoadMap`.
4. Commit it through `Scene::UpdateLoadedBlocks`.
5. Wait for streaming to settle, then run the existing native LOD/texture probe.
6. Advance to the next block and recycle released COM texture slots.

While the crawler is enabled, the original `Scene::Update` is intentionally not
called. A first implementation allowed mission scripts to continue and crashed
inside gameplay script execution (`0x007C94B0`, fault near `0x007C98BF`) because
the crawler replaces the loaded block underneath the active mission. Freezing
normal scene simulation is the correct diagnostic behavior: rendering/resource
loading continues, but unrelated mission logic cannot operate on replaced
entities.

No map names, hashes, or per-mission rendering rules are compiled into the ASI.
The generated catalog is data:

```text
scripts\TODAssetCrawler.txt
```

The completed catalog contained 114 live entries across maps, submaps, missions,
arcade/intermission blocks, and playerdata. Cutscenes remained disabled.

Status is written to:

```text
rtx-remix\logs\tod-asset-crawler.log
```

The completed run ended with:

```text
completed completed=114 total=114
```

## Automated whole-game workflow

Start an isolated session and enable the crawler:

```powershell
python tools\tod_asset_capture.py begin --map all --pass automated_crawl
python tools\tod_asset_capture.py crawl-on
```

Launch TOD and leave it running. No mission playthrough is required. Monitor:

```powershell
python tools\tod_asset_capture.py crawl-status
```

After the log reports `completed completed=114 total=114`, quit TOD, disable the
crawler, and collect:

```powershell
python tools\tod_asset_capture.py crawl-off
python tools\tod_asset_capture.py collect
```

The completed crawl produced 3,390 unique textures in:

```text
.trex\asset_capture\maps\all\textures
```

After unioning prior captures, the texture pipeline has 3,554 hash-named source
DDS files.

## Per-map workflow

Generate/update the static archive manifest:

```powershell
python tools\tod_asset_capture.py archive-index
```

Show a map's exact main/submap blocks and suggested passes:

```powershell
python tools\tod_asset_capture.py plan --map map01
```

Load the target map with `[AssetCapture] Enabled=0`. Once gameplay is stable,
start an isolated pass:

```powershell
python tools\tod_asset_capture.py begin --map map01 --pass spawn
```

Start the runtime probe:

```powershell
python tools\tod_asset_capture.py probe-on
```

Wait for `completed_restored`, then disarm it before the next location:

```powershell
python tools\tod_asset_capture.py probe-status
python tools\tod_asset_capture.py probe-off
```

Continue with separate passes for:

```text
spawn
full_traversal
interiors
vehicles
destroyables
texture-set/script variants
each listed submap
mission/script variants
```

For traversal passes, move through the whole playable area and repeat the LOD
probe after entering materially different sectors. This captures proximity-only
resources after TOD has legitimately streamed them.

Finish each isolated pass with:

```powershell
python tools\tod_asset_capture.py collect
```

The manager:

- temporarily isolates pre-existing normal Remix captures and forced-probe dumps;
- preserves each source under the session's `incoming` directory;
- normalizes both sources to `HASH.dds`;
- unions unique textures into `.trex\asset_capture\maps\<map>\textures`;
- adds them to the existing texture-pipeline capture union;
- restores every pre-existing capture afterward.

Use `abort` if a pass must be discarded:

```powershell
python tools\tod_asset_capture.py abort
```

Check accumulated coverage:

```powershell
python tools\tod_asset_capture.py status
```

Run the existing upscale pipeline on one map's union:

```powershell
python tools\tod_asset_capture.py upscale --map map01
```

Run the whole-game union:

```powershell
python tools\tod_asset_capture.py upscale --map all
```

Small HUD/effect textures remain excluded by the existing pipeline unless
`--include-small` is supplied.

## Upscale and outage recovery

The pipeline is restartable at both expensive stages:

- I2M writes each completed diffuse/normal/roughness PNG directly to
  `.trex\texture_pipeline\all\output`. A restart imports existing output first
  and prepares only hashes that are still missing.
- I2M runs in bounded batches (`--infer-batch-size`, default 128). If NVIDIA
  Kit fails a batch, the unresolved subset is recursively split until a bad
  texture is isolated; other textures continue. Failed hashes are written to
  `failed_i2m.txt`.
- DDS conversion uses eight worker processes by default on this 12-thread
  system (`--workers 8`).
- NVTT writes to `*.tmp.dds`, the result is validated with Pillow, and only then
  atomically replaces the final DDS. A power loss therefore cannot leave a
  partial file that is mistaken for a completed texture.
- Import retries transient Windows `PermissionError` failures.

The interrupted run contained 3,753 valid partial PNGs (1,251 texture sets).
They were recovered instead of rerun. Final verified active output:

```text
source captures:          3554
excluded hashes:             1
I2M-complete textures:    2681
too small for I2M:         872
eligible I2M missing:        0
diffuse DDS:              2681
normal DDS:               2681
roughness DDS:            2681
alpha sources preserved:   525 / 525
I2M failed hashes:           0
USDA materials:           2681
```

## Verification

- `tools\tod_asset_capture.py` passes `python -m py_compile`.
- `archive-index`, `plan`, `status`, session isolation, and restoration were
  exercised locally.
- Isolation smoke test restored 1,175 existing normal captures and 23 forced
  probe dumps.
- `TODRemixShim.cpp` builds cleanly with MSVC `/W4`.
- Automated crawler completed all 114 registered entries.
- Whole-game crawl captured 3,390 unique textures; capture union is 3,554.
- The final atomic DDS validation pass converted zero files, confirming every
  active DDS was readable and current.
- No zero-byte or temporary DDS files remain.
- Active `scripts\TODRemixShim.asi` size: 165,888 bytes.
- Active ASI SHA256:
  `CCCA33F3CF977075C90DE70BC5B47E680F0C714099F091BE8DF719EACDE44070`

## Remaining limitation

The crawler exhausts resources represented by the 114 registered live
AssetBlocks through their proper scene/resource-controller context. It cannot
invent runtime-only variants that a mission script procedurally creates after
gameplay events, nor does it include cutscene/video blocks while
`IncludeCutscenes=0`. Targeted gameplay passes remain useful only for those
dynamic states, not for ordinary map/submap/mission/playerdata coverage.
