# Translations

PeekWarden uses Qt Linguist translation files.

To add a language:

1. Add a new `PeekWarden_<locale>.ts` file, for example `PeekWarden_ja.ts`.
2. Add that file to `PEEKWARDEN_TRANSLATIONS` in `CMakeLists.txt`.
3. Translate the source strings in Qt Linguist.
4. Build normally. CMake runs `lrelease` and copies the generated `.qm` file beside the executable under `translations/`.

`src/TranslationCatalog.cpp` keeps current user-visible strings discoverable for Qt translation tooling.
