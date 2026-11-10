# Skrumi 11.0.0 — game development, Unicode and project tooling

- Add lazy, right-associative conditional expressions, postfix field/index updates,
  int32 bitwise operators and compound assignments; support hex/binary literals,
  digit separators, exponents, non-nested block comments and UTF-8 BOM sources.
- Breaking: relational comparisons now bind tighter than equality, following C.
  Int32 arithmetic, nullable semantics, struct references and switch fallthrough stay.
- Improve parser recovery and suppress semantic cascades; limit check/LSP diagnostics.
  Warn at declarations that shadow builtin names (SKR2001), add --deny-warnings and
  a scope-aware LSP rename quick fix. Lexer/parser check diagnostics have codes.
- Add Unicode 16 grapheme APIs using pinned utf8proc and grapheme-aware UI editing;
  keep byte/code-point APIs compatible. Add explicit byte_size and char_length names.
- Add conservative font coverage reporting, TTF fallback chains and load_font_file;
  extend the bitmap alphabet. Complex-script shaping and color fonts remain unsupported.
- Add alpha circles/lines/rings/bitmap/TTF text, native FPS limiting and Windows/X11
  cursor visibility/capture/relative motion. Add polygons, triangles, ellipses,
  gradients, rotation/flips, icon atlases, 9-slice, RGB canvases and PNG screenshots.
- Add miniaudio mixing with channels, per-voice/master/channel volume, pause, pan,
  looping, streaming and fades; Windows legacy sounds now overlap. Document backend
  limits, including static-musl device access and the unavailable Web mixer.
- Add deterministic per-interpreter random_seed/shuffle, array helpers, assert_close,
  long-running clock_ms64, user_data_dir and math helpers. Add std/game with geometry,
  camera, timers, tweens, particle pools and atomic JSON saves.
- Add skrumi init, TOML project manifests, project run/build/check, atomic build --force
  and explicit script tests. Protect source files from forced output replacement.
- Expand executable documentation, platform/Unicode/audio guides and English overview.
  Pin third-party dependencies with hashes and retain their license notices.
- Add official Unicode conformance, PCM mixer, raster/PNG, window/UI, CLI and LSP
  regression coverage. See docs/DEVELOPMENT_STATUS.md for deliberately unimplemented
  larger proposals; this release does not claim the entire roadmap is complete.

# Skrumi 10.0.1 — optional editor integration in GUI setup

- Detect native VS Code and Kate installations on Windows/Linux and offer
  independent opt-in choices, rescan and restoration of previous selections.
- Install VS Code extension files offline in the user extension directory, with
  a private runtime/LSP path hint for custom installation directories.
- Install Kate syntax/external tools and merge the Skrumi LSP/debug profiles into
  existing JSON without replacing other profiles. Plugins remain user-controlled.
- Leave Zed installation manual; its dev-extension sources are not bundled in setup.
- Extend transaction journals to v4 for editor snapshots: install, update,
  deselection, cancellation, process interruption, recovery and uninstall.
  Preserve edited/unmanaged files and unrelated Kate configuration changes;
  pre-existing manual integrations are skipped with a warning, without blocking
  runtime installation or claiming ownership of those files;
  continue reading older journals/manifests. No installer visual redesign.
- Add standalone regression coverage for integration ownership and recovery,
  and update component versions consistently. No language or VM changes.

# Skrumi 10.0.0 — explicit narrowing and deterministic calls

- Breaking: implicit float-to-int narrowing is rejected for declarations,
  assignments, parameters, results, fields and typed-container writes. Use
  to_int() explicitly, including for integral-valued floats. Builtin int
  parameters and array indices also require int. Int-to-float widening remains.
- CLI check and LSP report known narrowing in literals, numeric expressions,
  declared variables, calls, fields and nested container literals. Unknown
  dynamic types remain runtime-checked; this is not full static type inference.
- Breaking: remove the sized array declaration syntax. Use an ordinary empty
  declaration followed by resize(name, length). Remove its AST size member,
  compiler branch, bytecode instruction and VM handler.
- Breaking: a declared Skrumi function shadows a builtin by name, regardless of
  argument types/count. Direct calls and function values agree; invalid arguments
  do not select another implementation. Module aliases explicitly select exports.
- Correct the reference's array equality description and complete type_of
  results in the reference and builtin/LSP help. Execute marked documentation
  examples with assertions instead of only passing them through check.
- Add transactional Fish PATH integration in its native conf.d fragment, honoring
  XDG_CONFIG_HOME. Preserve user content, avoid duplicate PATH entries, safely
  quote literal paths and restore snapshots on rollback. csh/tcsh remain unsupported.
- Fix permission snapshots for a newly created shell configuration: use the same
  symbolic permission format as fs_permissions so a valid rollback is accepted.
- Migrate standard tests and installer arithmetic to explicit conversions; keep
  installer layout, GUI workflow, VM hot-path optimization and release assets.

# Skrumi 9.0.1 — recover installation after manual directory deletion

- Windows setup automatically recovers registration ownership when the main
  installation directory was deleted but the external maintenance executable
  and its ownership record remain intact.
- Recovery verifies the maintenance executable checksum and all seven expected
  registry values, including version, installation path and uninstall commands.
  Missing or modified registration values and unverified files remain protected.
- Recovery uses the existing transaction journal: cancellation or interruption
  restores the previous registry snapshots; successful uninstall removes the
  recovered registration instead of restoring a stale entry.
- Added regression coverage for manual deletion, older-version registration,
  rollback, repeated installation, uninstall and conflicting ownership evidence.
- Installer layout and language syntax are unchanged.

# Skrumi 9.0.0 — modules, distribution and editor integration

- Standardize source loading on `.skm`, canonical module identities and explicit
  errors for unresolved imports in runtime, check and LSP.
- Distribute Windows/Linux GUI installers, Android SDK and demo applications,
  editor integrations, benchmark results and checksums.
- Preserve transactional installation, recovery, ownership verification and
  protection of user files during updates and removal.

Earlier changes remain available in the repository's Git history and release tags.
