# XGallery — notes for Claude

Desktop photo-gallery manager. Qt 6 Widgets, C++17, SQLite, Windows (MinGW).
The owner (Ivan) talks in Ukrainian; all UI strings are Ukrainian. Code, comments and
commit messages are English. Features are added incrementally on request.
User docs: `README.md` (English, main), `README.uk.md`, `README.fr.md` with a language switcher
on top. When a feature changes, update all three; example file/folder names are in each file's
language, UI labels exactly as in the translations.

## Build

```powershell
# Exact versions are not fixed: use the newest installed Qt 6 and MinGW.
$qt = Get-ChildItem C:\Qt -Directory -Filter "6.*" | Sort-Object { [version]$_.Name } | Select-Object -Last 1
$mingw = Get-ChildItem C:\Qt\Tools -Directory -Filter "mingw*_64" | Sort-Object Name | Select-Object -Last 1
$env:PATH = "$($mingw.FullName)\bin;C:\Qt\Tools\Ninja;$($qt.FullName)\mingw_64\bin;" + $env:PATH
# Qt Creator's Debug build dir for that Qt
cd (Get-ChildItem build -Directory -Filter "Desktop_Qt_$($qt.Name -replace '\.','_')_MinGW_64_bit*Debug").FullName
C:\Qt\Tools\CMake_64\bin\cmake.exe .               # only when files were added to CMakeLists.txt
C:\Qt\Tools\CMake_64\bin\cmake.exe --build .
```

- Keep the build free of warnings.
- New source files go into `PROJECT_SOURCES` in `CMakeLists.txt`. Windows-only libs
  (`ole32 shell32 uuid`, for Explorer thumbnails) are linked under `if(WIN32)`.
- There are no unit tests in the repo. Verify logic with a throwaway console program in the
  scratchpad that compiles the needed `.cpp` files (`database`, `migrations`, `scanner`,
  `mediafiles`, `imagerotation`) and sets `QCoreApplication::setApplicationName("XGalleryScanTest")`
  so it never touches the real DB. Delete `%APPDATA%\XGalleryScanTest` afterwards.
- Do not put test files into the user's Recycle Bin, and do not read the user's personal files.
- The shell Claude runs in sees a virtualized `%APPDATA%`, not the user's real DB.

## Architecture

| File | Role |
|---|---|
| `main.cpp` | App/organization name (`Nova30`/`XGallery`) → AppData location |
| `mainwindow.*` | Everything in the main window: gallery combo, search, sort, tag list, 4×4 grid, File menu actions, busy handling |
| `database.*` | `Db::` — connection handling, settings, galleries, search, metadata, duplicates. GUI thread uses `Db::MainConnection` |
| `migrations.*` | Schema versions (`PRAGMA user_version`), one step per version |
| `scanner.*` | Worker-thread jobs: rescan, import, junk search; sha256; folder → tag rules |
| `mediafiles.*` | File kind by extension (image / video / other) and thumbnails (videos via `IShellItemImageFactory`, others as an extension card) |
| `imageviewer.*` | Viewer window: fit/upscale, arrows (circular), double-click full screen, right-click rotation |
| `imagerotation.*` | Saving rotations to disk (lossless for JPEG via EXIF orientation) |
| `gallerydialog.*`, `imagemetadialog.*`, `duplicatesdialog.*`, `busydialog.*` | Dialogs. `ImageMetaDialog` has a bulk mode (several paths → common tags only, no rating/delete) |
| `virtualpath.*` | Virtual path (sorted tags joined by `/`) and its sort key |
| `i18n.*`, `languagedialog.*`, `translations/*.ts`, `flags/` | UI language (uk source, en/fr translations), first-start flag dialog, flag SVGs + rendered PNGs (48x32 and @2x) |
| `xgallery.ico`, `xgallery.rc`, `resources.qrc` | Program icon, rendered from `logo.svg` (each size 16–256 px rendered directly, centred on a transparent square, PNG entries). Rebuild the .ico when the SVG changes |

## Rules that must keep holding

- **Schema changes only through `migrations.cpp`**: append a new step function to the list,
  never edit existing steps. Current version: 8 (galleries; rating+name; allow_upscale; no commas in tags;
  normalized tag spelling; virtual path; virtual path with digits after letters; '_' in tags is a space).
- **Heavy work off the UI thread**: `QtConcurrent::run`/`mapped` + `MainWindow::runBusy(watcher, text)`.
  `runBusy` must be called before connecting the caller's `finished` handler (so the UI is
  unlocked first and the handler may start the next job). Worker code opens its own DB
  connection (`Db::open(uniqueName)` … `Db::close`).
- **DB paths are relative to the gallery root**, `/`-separated. In portable mode (DB next to the
  exe) gallery roots are stored relative to the exe dir and must be on the exe's drive;
  `Gallery::rootFolder` in memory is always absolute.
- **Tags**: per gallery, always in `Db::normalizeTag` spelling (`_` → space, whitespace collapsed, first letter upper,
  rest lower) — when stored, and for search input. Folder names between root and file become tags;
  commas → space (`tagsFromRelativePath`). Manual tags must not contain `,` `<` `>` `/` `\`
  (`Db::isValidTagName`). Unused tags are deleted after every change (`Db::deleteUnusedTags`).
- **Search**: `Db::TagQuery` (include/exclude tags, onlyUntagged/skipUntagged, exact). `<без тегів>` is
  not a tag: it exists only in `MainWindow` as text in the search fields and the tag list's service
  entry (`parseTags` turns it into the untagged flags, `tagsText` back). `Db::` knows no service tags.
  Shown bold on a tinted background in the list and the search fields (`highlightNoTags`), so it differs from a real tag "Без тегів".
- **Rescan**: all files incl. hidden/system, except `*.db`/`*.ini`, the program's own DB and anything
  inside the exe folder (import and junk deletion skip that folder too). Unchanged = same size+mtime
  (no re-hash). Folder tags are added only to new files, so manual tag edits survive.
  Moves/renames: a missing record + one new file with the same sha256+size (1:1 only) keep
  id, rating and manual tags; old-folder tags are swapped for new ones.
  All files of one scan/import get the same `added_at` (for "newest first").
- **Import** (add files/folder): copy, never move; skip duplicates (DB, same batch, or same
  name+content already in the target); name clash → `name (1).ext`; records are written
  immediately (no rescan needed). The target folder is checked to stay inside the gallery; a
  Windows device name (CON, NUL, COM1…) gets `_` (`CON_`, `con_.old`), its tag stays `Con`.
- **Deleting user files**: always ask first. Single files and duplicates go to the Recycle Bin
  (`QFile::moveToTrash`); if that fails, nothing is deleted. "Видалити сміття" deletes
  `Thumbs.db`/`desktop.ini` and empty folders permanently (as the owner asked), still behind a
  confirmation.
- **Rotation**: JPEG only edits/adds the EXIF orientation tag (never re-encodes unless the
  file is malformed; then quality 100 with the EXIF block copied). Other formats are
  re-encoded losslessly. Pending turns are counted mod 4; 0 = nothing to save.
- **Virtual path** (`virtualpath.*`): images.vpath = the file's tags in alphabetical order joined by `/`,
  images.vpath_key = its alphabet-aware sort key (Latin, Cyrillic, then digits: "Маша/Море/001";
  changing that order needs a migration step that recomputes all paths). Recompute with `VirtualPath::update` whenever a file's tag
  set changes (meta save, bulk edit, tag rename, rescan new/moved files, import).
- Sorting: Classic = vpath_key, rating DESC, name NOCASE; NewestFirst = added_at DESC then classic;
  LargestFirst = size DESC then classic. The viewer walks the full search result (images only)
  in the same order.
- Bulk tag edit: checkboxes on thumbnails (hidden for "other" files). The selection is a set of image ids
  (`m_selectedIds`) kept across pages/searches, cleared by "Зняти всі" or a gallery switch; "Обрати всі"
  ticks the current page only. Right click with 2+ selected → `Db::imagesByIds` + `Db::applyTagChanges`.
  Double click on a tag in the metadata dialog adds it to "Містить" without searching.
- Tag clipboard (metadata dialog, single and bulk): Ctrl+C / context menu copies the selected tags as `А, Б`;
  Ctrl+V adds a list via `Db::tagsFromText` (commas and line breaks separate; other forbidden characters → space; normalized).
- **i18n**: source strings are Ukrainian in `tr()` / `QCoreApplication::translate`. Language code
  (uk/en/fr) is the `language` setting in the DB; not set → `LanguageDialog` at start (closing it = uk,
  not saved). Switching (menu "Language", always English; items in their own language) calls
  `I18n::apply` → `QEvent::LanguageChange`. `MainWindow` sets every fixed text in `retranslateUi()`
  and refreshes data texts in `changeEvent`; dialogs pick the language up when opened.
  After adding/changing UI strings: run `lupdate -no-obsolete -locations none -source-language uk
  -target-language en|fr *.cpp *.h -ts translations/xgallery_en|fr.ts` and fill the new
  translations (the build embeds the .qm files via `qt_add_translations`). Keep 0 unfinished.
  `<без тегів>` is shown and parsed in the UI language (`noTagsDisplayName`).
- "Сховати чужі теги": the tag list shows `Db::tagsOfSearch` (tags of the last search's results).
- Tag filter field (`m_tagFilterEdit`, under that checkbox): always hides list entries not containing its text
  (case-insensitive); the `<без тегів>` service entry always stays. Only the user changes the text; it is not saved between runs.