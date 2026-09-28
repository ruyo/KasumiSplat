# Vendored SPZ dependencies

- Niantic `spz`: commit `affd0ecea7fbb4c265ee119475af7ee5b2997482`
- Facebook `zstd`: tag `v1.5.6`, commit `794ea1b0afca0f020f4e57b6732332231fb23c70`

The SPZ `.cc` translation units are renamed to `.cpp` for UnrealBuildTool discovery. Coordinate-system extensions are compiled through `SPZ_BUILD_EXTENSIONS`. Their type-ID-guarded `dynamic_pointer_cast` is replaced with `static_pointer_cast` because Unreal modules disable RTTI. `zstd.c` is generated from zstd's `build/single_file_libs/zstd-in.c` with legacy decoder sources excluded; `zstd.h` and `zstd_errors.h` are retained for the SPZ public includes. No runtime module links these sources.

See `LICENSE.spz.txt` and `LICENSE.zstd.txt` in this directory.
