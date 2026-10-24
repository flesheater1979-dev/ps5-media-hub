<!-- SPDX-License-Identifier: GPL-3.0-or-later -->

# Clean-room runtime shim

`libc.prx` is generated locally from the independently authored source in
`../libc_builder.cpp` plus the two manifests in this directory
(`api-surface.txt`, `imports.txt`). It contains **no** Sony runtime
implementation, proprietary SDK binary, or game file — it is a loader-satisfying
shim that forwards `malloc`/`free`/`posix_memalign` to the system
`libSceLibcInternal` and stubs/zero-fills the rest.

Distributed under GPL-3.0-or-later, the same as the rest of EVO Player.

- `api-surface.txt` / `imports.txt` — harvested from **ProsperoLight's** link and still
  as vendored (EVO has not re-harvested them; EVO's FFmpeg, OpenSSL and libass imports
  resolve against this surface or against the SDK stubs and the `libc_ext` / `evo_dns` /
  `malloc_shim` objects in `stubs/`). A `libc`-tagged symbol that EVO starts to import
  and this surface lacks becomes an unresolved import at load (a crash at address 0 on
  first call). Re-harvest with `llvm-nm -u` on the intermediate PIE
  (`output/app/.build/llvm-pie.elf`), diff, and regenerate; that breaks
  `libc.prx.sha256` below, and EVO re-pins its own digest then.
- `libc.prx.sha256` — the upstream ProsperoLight release digest.
- `libc.prx` — the generated binary. **Git-ignored**; rebuilt by
  `scripts/package-app.sh` (or `--rebuild-libc`).

## Verify

```sh
cd tools/native-app/runtime && sha256sum -c libc.prx.sha256
```
