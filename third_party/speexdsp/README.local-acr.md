# SpeexDSP vendoring note

Local ACR vendors three unmodified files from the hash-pinned SpeexDSP 1.2.1
archive recorded in `third_party/dependencies.lock.json`:

- `include/speex/speex_resampler.h` → `include/speex_resampler.h`
- `libspeexdsp/arch.h` → `src/arch.h`
- `libspeexdsp/resample.c` → `src/resample.c`

The standalone resampler is compiled as Float32 with `OUTSIDE_SPEEX`, SIMD
disabled, and `RANDOM_PREFIX=lacr_speex` so its C symbols cannot collide with
another SpeexDSP copy in an embedding application. The upstream license is in
`third_party/notices/speexdsp-COPYING`.
