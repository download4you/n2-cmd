# Native dependencies

The upstream licenses remain alongside the vendored source and in `licenses/`.
Lua 5.4.6 and libvterm 0.3.3 compile as C and do not require an external runtime.

## libvterm local changes

The pinned upstream parser, terminal modes, keyboard encoding, alternate-screen
buffer and ordinary screen operations are retained. Local changes are limited
to primary-screen reflow and its cursor/scrollback metadata:

- `VTermStateFields.at_phantom` carries the pending-wrap insertion position
  through a resize. Resize cursor notifications preserve that state. Without
  this change, shrinking `abcdef` from 8 to 3 columns and appending `X`
  overwrites `f`.
- Primary-screen reflow packs complete wide glyphs, omits cleared padding left
  when a wide glyph wraps, preserves hard line boundaries, and exports only
  the new rows preceding the viewport. The upstream routine could abort or
  drop the suffix of a wrapped paragraph taller than the new viewport.
- `VTermScreenCallbacks.sb_pushline_reflow` supplies continuation metadata for
  those newly packed history rows. Existing callbacks remain available.
- Alternate-screen resizing retains the upstream fixed-grid behavior rather
  than reflowing full-screen application content.

`tests/terminal_tests.cpp` exercises exact-margin append, explicit newlines,
wide glyphs, Persian/mixed text, narrow/wide resize cycles, retained history,
alternate screens, and cursor/selection/rendering consistency.
