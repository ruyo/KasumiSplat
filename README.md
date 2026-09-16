# KasumiSplat 0.3 beta

KasumiSplat is a UE 5.8 Gaussian splat renderer with PLY import, streamable assets, deterministic GPU effects, Scene Depth integration, and an optional Niagara bridge.

## Implemented

- `KasumiSplatRuntime`: SM6 instanced-quad renderer, GPU culling, 4096-bin, exact global radix, and tiled sorting paths, indirect draw, Scene Depth rejection, and a small-splat depth pre-cull.
- `KasumiSplatEditor`: ASCII, binary little-endian, and binary big-endian PLY import and reimport. The parser accepts a 64-bit mapped-file size, so files above 2 GB can be parsed when their point and SH arrays fit Unreal's 32-bit `TArray` limits.
- `KasumiSplatNiagara`: User Parameter bridge and a Data Interface for CPU Sim and GPU Sim. Resident point data is uploaded to its render-thread proxy only when the component revision changes.
- Version 6 assets: packed point and higher-order SH `FByteBulkData`, imported SH direction basis and profile metadata, 65,536-point Morton chunks, async range reads, moment-matched Gaussian distance LOD, resident-point and memory budgets.
- GPU effects: legacy controls plus four ordered Displace, Tint, Opacity, Scale, or Rotate layers. Sphere and box masks, Stable ID noise, Texture2D masks, and VolumeTexture masks are supported.
- Animation control: `Interp` style values, `UCurveFloat`, Blueprint setters, Material Parameter Collection inputs, and deterministic normalized Progress suitable for Sequencer scrubbing.
- View-dependent spherical harmonics through degree 3.
- Appearance controls for exposure, saturation, contrast, higher-order SH strength, and opacity density. Reference, Vivid, Dense, Soft, and Custom presets can be selected independently of the performance quality preset.
- Low, Medium, High, and Cinematic quality presets, project settings, capability checks, resident-memory queries, and bounds/chunk debug drawing.

The renderer runs at `BeforeDOF`, in HDR before depth of field, temporal upscaling, and tonemapping. Components are ordered far-to-near by their view-projected bounds. `Auto` uses exact per-component global radix sorting through 750,000 submitted splats, then uses tiled sorting when the viewport has a valid tile layout. Opaque Scene Depth is tested per pixel with projected Gaussian thickness; small fully covered candidates can also be removed during the compute cull.

## Import and use

1. Drag a `.ply` file into the Content Browser.
2. Select `Auto Detect`, `Standard 3D Gaussian Splatting`, `Luma AI`, `RGB Point Cloud (sRGB)`, or `Custom`. Auto Detect uses Luma header hints and the available Gaussian properties.
3. Set `Units To Centimeters`; the default `100` treats one source unit as one meter.
4. Place a `KasumiSplatActor` and assign the imported asset to its component.
5. Disable `Use Synthetic Fallback` for imported data.
6. Select an Appearance Preset or adjust Exposure EV, Saturation, Contrast, SH Strength, and Opacity Density. Editing any value changes the preset to Custom.
7. Adjust Style, Effect Layers, streaming budgets, and an optional Texture2D or VolumeTexture mask.
8. For Niagara, add a `Kasumi Splat` Data Interface, set `Source Actor`, and call `GetPointCount` or `GetPoint` in a CPU or GPU emitter.

`Samples/KasumiSplatTiny.ply` and the scripts under `Tests/` recreate the small validation assets. Content assets are intentionally excluded from the plugin source package.

## Design rule

Effects derive every frame from immutable source data, Stable ID, Seed, and normalized Progress. Returning Progress to zero restores the source position and attributes, so Sequencer scrubbing, reverse playback, and looping do not depend on frame history.

## Known limits

- Bucket mode uses 4096 quantized depth ranges. GlobalRadix provides exact per-component ordering. Tiled uses a budgeted variable-length tile/depth pair set. When its configured bounds permit an overflow, it prepares a GlobalRadix ordering and switches to it in the same frame if the GPU detects one.
- `Full Quality Reference` loads every point and disables LOD and small-point rejection for fixed-camera comparisons.
- `Antialiasing Mode` provides Disabled, Legacy Filter, and Area Compensated variants. The `BeforeDOF` pass uses the same jittered projection as Scene Depth and the later temporal upscaler. Resident LOD changes fade by Stable ID so points present in both snapshots are composited once.
- Screen-space Gaussian filtering defaults to `0.3 px²` and compensates opacity for the filtered footprint. HDR composition applies UE pre-exposure, and imported axis conversion is also applied to SH view directions. Reimport older assets to store the SH direction basis.
- A splat exceeding `Max Tiles Per Splat`, or a tile-pair list exceeding its memory budget, triggers the prepared same-frame GlobalRadix draw. Partial Tiled output is suppressed.
- Cross-component ordering uses view-projected component bounds rather than one merged all-point sort.
- The depth pre-cull uses full-resolution center depth. A hierarchical HZB traversal is not implemented.
- Chunk decoding, chunk concatenation, and Gaussian LOD merging run on worker threads. Applying the completed immutable snapshot and recreating a changed component GPU buffer happen on their owning threads; unchanged snapshots reuse the buffer. Subrange writes into an existing point buffer are not implemented.
- Distance LOD merges adjacent Morton-ordered Gaussians by opacity-weighted moments. It preserves mixture center, covariance, color, accumulated opacity, and SH better than representative-point decimation, but remains an approximation of the original alpha-composited set.
- The import fingerprint skips unchanged reimports and records source metadata, importer settings, and data version. Parsed PLY results are not shared through a custom DDC record.
- Win64 DX12 SM6 is the validated RHI. Linux Development and Shipping cross-builds pass; target-hardware visual and performance testing remains.
- Motion vectors are not emitted, so fast camera or object motion can show temporal artifacts.

See [ImplementationRoadmap.md](Docs/ImplementationRoadmap.md), [SupportedFormats.md](Docs/SupportedFormats.md), and [Packaging.md](Docs/Packaging.md).
Quality work and the reproducible 100k-to-2m CSV matrix are documented in [QualityAndPerformancePlan.md](Docs/QualityAndPerformancePlan.md).
