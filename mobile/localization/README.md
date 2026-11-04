# Shared mobile translations

The native clients share 729 English source keys across 12 languages. English
source strings are stable lookup keys, not protocol identifiers. The generated
Android resource names use a deterministic hash of each key; Swift uses the
English key in `Localizable.strings`.

Edit the source catalogs, then run `python mobile/tools/generate_localizations.py`:

- `desktop.json`: the common Qt `localizedText` translations, captured from
  `src/client/main_window.cpp`. Review this snapshot when desktop wording changes.
- `*.tsv`: UTF-8 rows with 12 **pipe-separated** columns, ordered
  `en|es|fr|de|pt|it|ru|zh|ja|ko|ar|hi`. `\n` represents an intentional newline.
- `aliases.json`: alternate English source keys with equivalent translated text.
- `compositions.json`: label/value rows composed from `[[translated labels]]`.
- `required.json`: the required mobile source keys; missing translations fail
  generation. New catalog rows automatically become native resources.

Generated files: `catalog.json`, Android `values*/strings.xml` and
`LocalizationKeys.kt`, iOS `*.lproj/Localizable.strings`, `InfoPlist.strings`, and
`Core/LocalizationKeys.swift`. Run `generate_xcode_project.py` when adding source
files or a localization directory, and run `check_projects.py` before committing.

Use `L10n.format("... %1 ...", ...)` for dynamic values. Both native formatters
keep values unchanged and isolate `@username`, `#rank` and other interpolated
values in Arabic. Complete sentence templates may reorder placeholders. For
legacy status strings, `L10n.text` / `L` recognizes a known English template at
the presentation boundary. Keep protocol messages and selection identifiers in
English. Never send translated enum labels to the server.

User messages, player names, room/layer names, moderation reasons and drawing
words must bypass translation. Android label/card helpers have explicit raw-text
options; SwiftUI should receive those strings directly. The drawing surface is
explicitly LTR, independent of mirrored UI layout. Do not reverse layer ordering
or canvas coordinates for Arabic.

System resource prompts follow OS/app language settings. The custom language
picker applies immediately to app-owned UI and saves the choice. Translations
of platform names and emulator-control labels intentionally preserve the names
users need to recognize. Translation quality can be refined in these source
files without changing either native implementation.
