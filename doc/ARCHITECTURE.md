# libcdio-paranoia Architecture

This document describes how the pieces of libcdio-paranoia fit together,
the internal state of the paranoia read loop, and the meaning of the
`paranoia_mode_t` / `paranoia_cb_mode_t` values applications see through
the callback.

## 1. Module layout

```
src/cd-paranoia.c        CLI front end: option parsing, drive selection,
                          span/track parsing, output file writing, and the
                          progress-bar callback.
src/cachetest.c           "-A" drive-analysis mode. Talks directly to
                          lib/cdda_interface (cdda_read_timed()) and never
                          touches the paranoia state machine at all; see
                          section 6.
src/header.c              WAV/AIFF/AIFC header writers used by cd-paranoia.c.
src/report.c/.h            report()/reportC()/printC()/logC() logging macros.

lib/cdda_interface/        Low-level CD-DA access ("libcdio_cdda").
  interface.c               cdda_identify()/cdda_open()/cdda_close(), speed
                          and read-size (nsectors) negotiation.
  common_interface.c        cdda_read()/cdda_read_timed(): issue one or
                          more low-level reads of up to d->nsectors
                          sectors, optionally timing them (used both by
                          paranoia's cache jiggling and by cachetest.c).
  drive_exceptions.c        Per-model quirks tables.
  toc.c                     Table-of-contents/track boundary lookups.

lib/paranoia/               The paranoia error-correction library
                          ("libcdio_paranoia"), built on top of
                          lib/cdda_interface.
  paranoia.c                cdio_paranoia_init/_read_limited/_free: the
                          state machine described below.
  p_block.c/.h              c_block_t (raw read buffer + per-word flags),
                          v_fragment_t (a verified run of samples), and
                          root_block (the single "verified/reconstructed"
                          output buffer), plus the linked lists that hold
                          them.
  overlap.c                 Sample-accurate cross-correlation between two
                          buffers (i_paranoia_overlap/2) used to find how
                          two reads of the same audio line up despite
                          jitter/drift in where the drive actually started
                          the read.
  gap.c                     Silence ("continent/island") detection and
                          alignment fallback used when overlap correlation
                          can't find a match (see the "Silence" comment
                          block at the top of paranoia.c).
  isort.c                   Sorted-position index used to quickly find
                          candidate overlap matches across many cached
                          blocks instead of comparing against everything.
```

`cd-paranoia.c` is a *client* of `lib/paranoia`, which is itself a client
of `lib/cdda_interface`. `cachetest.c` bypasses `lib/paranoia` entirely and
is a second, independent client of `lib/cdda_interface`.

## 2. Key data structures

- **`cdrom_drive_t`** (`include/cdio/paranoia/cdda.h`) — one opened CD-ROM
  device: the underlying `CdIo_t*`, endianness (`bigendianp`), the
  negotiated low-level read size (`nsectors`), the TOC, and the
  driver-specific `read_audio`/`set_speed` function pointers.

- **`cdrom_paranoia_t`** (`lib/paranoia/p_block.h`, `struct
  cdrom_paranoia_s`) — one paranoia session over a `cdrom_drive_t*`:
  - `root` (`root_block`): the single buffer of **verified,
    reconstructed** audio that `paranoia_read_limited()` actually returns
    pointers into.
  - `cache` (`linked_list_t` of `c_block_t`): raw, as-read-from-disc
    sectors, each with a per-16-bit-word `flags` byte (`FLAGS_EDGE`,
    `FLAGS_VERIFIED`, ...) recording read-boundary and verification state.
  - `fragments` (`linked_list_t` of `v_fragment_t`): runs of samples that
    stage 1 has cross-verified between two reads but that haven't yet been
    merged into `root`.
  - `sortcache`: the `isort` index over cached data used to speed up
    finding overlap candidates.
  - `cdcache_begin/end/size`, `jitter`: this session's model of what the
    *drive's own* readahead/backseek cache currently contains, used to
    decide when a read is likely to hit stale cached data instead of a
    fresh read (see `cdrom_cache_handler()`/`cdrom_cache_update()`, and
    `paranoia_cachemodel_size()` for how it's seeded from the drive's
    negotiated `nsectors`).
  - `enable`: the current `paranoia_mode_t` bitmask (`paranoia_modeset()`).
  - `cursor`: the next sector number that `paranoia_read_limited()` will
    return.
  - `dynoverlap`/`dyndrift`, `stage1`/`stage2` (`offsets`): running
    statistics used to dynamically grow the search overlap and estimate
    drift when reads keep failing to line up.

## 3. The read loop: `cdio_paranoia_read_limited()`

Call sequence for a single call (`lib/paranoia/paranoia.c`):

1. Compute `[beginword, endword)` for `p->cursor` (one CD sector's worth
   of 16-bit words).
2. **Fast path:** if `root` already verified-and-covers
   `[beginword, endword)` (plus a safety margin of
   `MAX_SECTOR_OVERLAP` sectors, when verify/overlap mode is on), skip
   straight to step 6.
3. Otherwise, try to extend `root` *without* touching the drive first:
   - `i_paranoia_trim()` frees fragments/cache no longer needed, bounding
     memory use.
   - `i_stage2()` merges any already-verified `v_fragment_t`s that abut
     `root` into it (cross-correlating fragment vs. root via
     `overlap.c`, or falling back to silence-alignment via `gap.c` when
     the audio is silent). `i_end_case()` instead pads with zero-silence
     once the last sector of the session/track has actually been seen.
4. If that still isn't enough, read more raw data from disc:
   `i_read_c_block()` issues one or more `cdda_read_timed()` calls
   (via `lib/cdda_interface`) totalling `p->d->nsectors`-sized chunks,
   "jiggling" the exact sectors requested call-to-call so a
   consistently-misread sector isn't mistaken for ground truth, and
   flags each word `FLAGS_EDGE` near a low-level read boundary (where
   drive readahead/caching most often introduces errors).
   - With `PARANOIA_MODE_VERIFY`: `i_stage1()` cross-correlates the new
     block against previous cache blocks to build/extend verified
     `v_fragment_t`s (`PARANOIA_CB_VERIFY`, and on success
     `PARANOIA_CB_FIXUP_EDGE`/`PARANOIA_CB_FIXUP_ATOM` for jitter fixed at
     a read boundary vs. mid-block).
   - With `PARANOIA_MODE_OVERLAP` only (no full verify): each low-level
     read (minus its `FLAGS_EDGE` margins) is trusted as-is and turned
     directly into a `v_fragment_t`.
   - With neither mode (`PARANOIA_MODE_DISABLE`/paranoia off): the raw
     block is promoted straight into `root`, unverified.
5. **Retry/skip accounting:** if `root` hasn't grown by at least half a
   sector, bump `retry_count`; every 5th stalled retry either grows
   `dynoverlap` (`PARANOIA_CB_OVERLAP`, "look harder") or — once
   `dynoverlap` maxes out or `max_retries` is hit — calls
   `verify_skip_case()` **unless** `PARANOIA_MODE_NEVERSKIP` is set, in
   which case it just keeps retrying forever. `verify_skip_case()` grafts
   in whatever partially-matching data it can find (preferring already
   verified bytes) or, failing that, writes silence, and reports
   `PARANOIA_CB_SKIP` (this is the "V" you see in cdparanoia's progress
   display).
6. Loop back to step 2 until `root` covers the requested range, then
   `p->cursor++` and return a pointer *into* `root` (the caller must not
   free it — it's only valid until the next `_read_limited()` call).

## 4. `paranoia_mode_t` (what you enable)

| Flag | Effect |
|---|---|
| `PARANOIA_MODE_DISABLE` (0x00) | No verification/fixups; raw reads pass straight through. |
| `PARANOIA_MODE_VERIFY` (0x01) | Cross-verify reads against each other (stage 1) before trusting them. |
| `PARANOIA_MODE_FRAGMENT` (0x02) | Unsupported. |
| `PARANOIA_MODE_OVERLAP` (0x04) | Perform overlapped reads (cdda2wav-style); without `VERIFY` this trusts each read's non-edge bytes directly. |
| `PARANOIA_MODE_SCRATCH` (0x08) | Unsupported. |
| `PARANOIA_MODE_REPAIR` (0x10) | Unsupported (repair is always attempted when verify data allows it). |
| `PARANOIA_MODE_NEVERSKIP` (0x20) | Never give up and skip/silence a stuck read — retry indefinitely instead. |
| `PARANOIA_MODE_FULL` (0xff) | All of the above except `DISABLE`. `cd-paranoia`'s default is `PARANOIA_MODE_FULL ^ PARANOIA_MODE_NEVERSKIP` (full paranoia, but allowed to skip after `max_retries`, `-z`). |

## 5. `paranoia_cb_mode_t` (what the callback reports)

`callback(long inpos, paranoia_cb_mode_t function)` is invoked throughout
the loop above so the caller (typically a progress display) can show what
kind of work is happening:

| Value | Meaning |
|---|---|
| `PARANOIA_CB_READ` | About to issue/have issued a low-level read. |
| `PARANOIA_CB_VERIFY` | Cross-verifying a candidate against previous data (stage 1). |
| `PARANOIA_CB_FIXUP_EDGE` | Jitter fixed at a low-level read boundary. |
| `PARANOIA_CB_FIXUP_ATOM` | Jitter fixed mid-block ("atomic" region, not at an edge). |
| `PARANOIA_CB_SCRATCH` | Unsupported (scratch detection). |
| `PARANOIA_CB_REPAIR` | Unsupported. |
| `PARANOIA_CB_SKIP` | Retries exhausted for this position; `verify_skip_case()` grafted in best-effort/silent data ("V" in the progress display). |
| `PARANOIA_CB_DRIFT` | Drift beyond normal jitter detected between reads. |
| `PARANOIA_CB_BACKOFF` | Unsupported. |
| `PARANOIA_CB_OVERLAP` | Dynamic search overlap (`dynoverlap`) was grown because reads keep failing to line up. |
| `PARANOIA_CB_FIXUP_DROPPED` | Dropped sample(s) detected and compensated. |
| `PARANOIA_CB_FIXUP_DUPED` | Duplicated sample(s) detected and compensated. |
| `PARANOIA_CB_READERR` | Hard read error from the drive. |
| `PARANOIA_CB_CACHEERR` | The drive's read timing implies it served a cache hit paranoia's cache model didn't expect — a sign paranoia may be mismodelling this drive's cache (`-A` exists to diagnose this; see section 6). |
| `PARANOIA_CB_WROTE` | A sector has been written to the output file (`cd-paranoia.c`-side bookkeeping, not emitted by the library itself for real writes but used by `cd-paranoia.c` to mark progress). |
| `PARANOIA_CB_FINISHED` | The current span/track is done. |

`src/cd-paranoia.c`'s `callback()` maps each of these to a `slevel` (0-8)
which in turn selects one of the "smilies" (`:-)`, `:-|`, `;-(`, ...) shown
in the progress bar, and to characters written into the scrolling
`dispcache` bar (`-`, `+`, `e`, `C`, `V`, `!`) so a user can see roughly
*where* in the current span problems occurred.

## 6. End-to-end flow of `cd-paranoia.c`'s `main()`

1. Parse options with `getopt_long()` into local flags (`output_type`,
   `paranoia_mode`, `force_cdrom_*`, `-A`/`run_cache_test`, ...).
2. Open log/report files if `-l`/`-L`/`-A` requested them.
3. Resolve/open the drive: `cdda_identify()` (either the explicit
   `-d`/`-k`/`-g` device or auto-detected via
   `cdio_get_devices_with_cap_ret()`), then `cdda_open()`. Apply
   `-c`/`-C` (endianness), `-n` (sectors/read), `-o` (search overlap),
   `-S` (speed).
4. **If `-A`:** call `analyze_cache()` (`src/cachetest.c`) instead of
   reading any audio — see below — and exit with its warning code.
5. Parse the `<span>` argument (`parse_offset()`) into first/last LSN,
   validate it only covers audio tracks, and apply `-t`/`-O`
   (toc/sample offset) and leadin/leadout padding.
6. `paranoia_init()` + `paranoia_modeset(paranoia_mode)` +
   `paranoia_overlapset()` if `-o` was given, then `paranoia_seek()` to
   the first sector.
7. Main read loop: for each output file (one per track in `-B` batch
   mode, or one file for the whole span), write the appropriate
   WAV/AIFF/AIFC header (`src/header.c`), then repeatedly call
   `paranoia_read_limited(p, callback, max_retries)` and
   `buffering_write()` the result until `batch_last` is reached,
   applying the `-O` sample-offset shift and endian byte-swapping as
   needed. A `NULL` return with `errno` of `EBADF`/`ENOMEDIUM` aborts;
   any other skip is reported and (with `-X`) can abort the current file.
8. `paranoia_free()`, `cdda_close()` (via the `atexit(cleanup)` handler).

## 7. `cachetest.c`'s `analyze_cache()`: bypassing paranoia entirely

`-A`/`--analyze-drive` does **not** exercise `lib/paranoia` at all. It
calls `cdio_cddap_read_timed()`/`cdda_read_timed()` directly
(`lib/cdda_interface`) and measures how long individual reads take in
order to empirically determine:

- the drive's non-linear-access (readahead) cache size,
- whether that cache is contiguous,
- how far the drive reads ahead of the requested sector
  ("rollahead"/"readahead") and how far behind it keeps data
  ("rollbehind"/cache tail cursor and its granularity),
- whether a backward seek actually flushes the drive's cache (some
  drives don't, which is what `PARANOIA_CB_CACHEERR` above is warning
  about at runtime).

It does this purely by timing seeks (`MIN_SEEK_MS` = 6ms is used as the
"this was almost certainly a cache hit, not a real seek" threshold) — see
`time_drive()`/`retime_drive()` — and reports a warning level (0 = OK, 1 =
paranoia's cache model may not match this drive) that `main()` prints to
the user, independent of and prior to any actual ripping.
