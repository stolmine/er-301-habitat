# Raw changelog: v2.5.0 -> v2.5.1

Commits between `v2.5.0` and `v2.5.1`, in reverse chronological order.

## Release commit

- `ff4479e` v2.5.1 release: scope 1.2.1 + spreadsheet 2.7.1

## scope feature work

- `f91feaa` scope 1.2.0.7 (dev): drop box around voltmeter readout
- `33b74e7` scope 1.2.0.6 (dev): gate solid box on encoder grab, not just hover
- `2604819` scope 1.2.0.4 (dev): dotted/solid control boxes, drop column headers
- `f22afc2` scope 1.2.0.3 (dev): voltmeter readout + auto-expand control boxes
- `1849d20` scope 1.2.0.2 (dev): clear probe on decimation change
- `c60ab94` scope 1.2.0.1 (dev): fix stuck warmup counter in ScopeGraphic::draw
- `f61497b` scope 1.1.0.1: user-controllable timebase + Y-axis gain (dev)

## scope hot fixes (dev iteration)

- `2efdcc1` scope 1.2.0.5 (dev): restore col3 local

## spreadsheet feature work

- `0b96912` spreadsheet 2.7.0.1: Larets true stereo (dev)

## spreadsheet version management

- `183d68d` spreadsheet 2.7.1.1 (dev): bump above stale SD ceiling

## biome (CloudSeed port relegated)

- CloudSeed port attempt (commits `2190131` Phase A vendor, `6ed6308`
  Phase B wind-down) preserved on branch `cloudseed-archive`. Scrubbed
  from main pre-release after the first-frame DSP hang on Cortex-A8
  was confirmed unfixable without root-causing the upstream issue.
  Biome reverts to its v2.5.0 state (PKGVERSION 2.2.0).

## post-v2.5.0 editorial / tracking (carried into v2.5.1 history)

- `7eaba30` todo + release-tracking updates post-v2.5.0
- `a8cf609` v2.5.0 release notes: editorial pass

## Stats

- Total commits in v2.5.1: 15 (excluding `v2.5.0` itself)
- Packages with code changes: scope, spreadsheet, biome (biome work uncovered, not user-facing in v2.5.1)
- New custom Graphic subclasses: 3 (`ScopeGraphic`, `ScopeControlBox`, `ScopeVoltsReadout`), all header-only per `feedback_no_out_of_line_virtuals`
- Memory updates: new `feedback_stereo_pattern_selection`; `feedback_package_version_bump` extended with `install-packages.sh` interaction note
- Plans: `planning/scope-timebase-gain.md`, `planning/larets-stereo.md`
