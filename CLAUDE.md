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
- New source files (headers too, also third-party ones) go into `PROJECT_SOURCES` in `CMakeLists.txt`;
  other repo files (docs, licenses, SVG sources) into the non-building `XGalleryOtherFiles` target, so Qt Creator shows them. Windows-only libs
  (`ole32 shell32 uuid`, for Explorer thumbnails) are linked under `if(WIN32)`.
- There are no unit tests in the repo. Verify logic with a throwaway console program in the
  scratchpad that compiles the needed `.cpp` files (`database`, `migrations`, `scanner`,
  `mediafiles`, `imagerotation`, `virtualpath`, `collections`, `thumbcache`; for folder sync also `foldersync`
  (stub `Scanner::recycleFile` by compiling `scanner.cpp` with `recycleFile=realRecycleFile`, so nothing reaches the Recycle Bin); for downloads also `downloadprofile`,
  `htmllinks`, `downloadnames`, `httpfetcher` + Qt Network; HTTP is tested against a local Python `http.server`) and sets `QCoreApplication::setApplicationName("XGalleryScanTest")`
  so it never touches the real DB. Delete `%APPDATA%\XGalleryScanTest` afterwards.
- Do not put test files into the user's Recycle Bin, and do not read the user's personal files.
- The shell Claude runs in sees a virtualized `%APPDATA%`, not the user's real DB.

## Architecture

| File | Role |
|---|---|
| `main.cpp` | App/organization name (`Nova30`/`XGallery`) → AppData location |
| `mainwindow.*` | Everything in the main window: gallery combo, search, sort, tag list, grid (collections, or the files of the open collection), File menu actions, busy handling |
| `database.*` | `Db::` — connection handling, settings, galleries, search (files and collections), metadata, duplicates. GUI thread uses `Db::MainConnection` |
| `migrations.*` | Schema versions (`PRAGMA user_version`), one step per version |
| `scanner.*` | Worker-thread jobs: rescan, import, junk search; sha256; folder → tag rules |
| `mediafiles.*` | File kind by extension (image / video / other) and thumbnails (videos via `IShellItemImageFactory`, others as an extension card) |
| `imageviewer.*` | Viewer window: fit/upscale, arrows (circular), double-click full screen, right-click rotation, Delete key (only with a `DeleteHandler`, else the refusal warning) |
| `imagerotation.*` | Saving rotations to disk (lossless for JPEG via EXIF orientation) |
| `gallerydialog.*`, `imagemetadialog.*`, `tagpickerdialog.*`, `duplicatesdialog.*`, `collectiondialog.*`, `busydialog.*` | Dialogs. `DuplicatesDialog`: path + " (collection)" per file, tooltip = virtual path (`Db::DuplicateFile`); right click on a group → "Ігнорувати" / "Не ігнорувати" (see Duplicates rule). `ImageMetaDialog` has a bulk mode (several paths → common tags only, no name/delete/export). `TagPickerDialog` is its "Додати теги" window. `CollectionDialog` = collection name + rating. `GalleryDialog` also edits the tag groups |
| `virtualpath.*` | Virtual path (sorted tags joined by `/`) and its sort key |
| `collections.*` | Collections: xxh128 of the virtual path (`hashOf`), default name, `retag` (vpath/vpath_key/vpath_hash of files) and `sync` (collection rows: transfer, create, delete, covers) |
| `downloaddialog.*` | "Завантажити з інтернету" window, two tabs ("Профіль" first: profiles + profile fields; "Завантаження": the rest + start): profiles (PuTTY-like), never-saved login/password/token, tags, root folder, the run (page → links → files one by one → `Scanner::importFiles`) |
| `downloadprofile.*` | `DownloadProfile` (mode, tag, source attribute, classes, attributes, attempts) + JSON of one profile and of the export file |
| `htmllinks.*` | Regex tag tokenizer (no WebKit): profile → strings → links; entities; page charset (Windows code pages when Qt has no ICU) |
| `downloadnames.*` | Next `NNN-collection` folder, file name from a link, `file_NNN.ext`, `page_N.html/bin`, extension from Content-Type |
| `httpfetcher.*` | One GET at a time over `QNetworkAccessManager`: manual redirects, credentials only for the page's host, 60 s transfer timeout; `retryAfterMs` (Retry-After: seconds or HTTP date vs. the answer's Date) |
| `foldersync.*` | `FolderSync::` — "Синхронізувати з папкою": `compare` (worker) and `applyToFolder` / `applyToGallery` / `merge` (worker; the gallery side writes records in one transaction) |
| `busyspinner.*` | `BusySpinner`: a circle of dots running round, hidden while stopped (download manager's pauses) |
| `wrappedtext.*` | `WrappedText::layout`: text wrapped at word boundaries, else anywhere (main tag list, tag picker) |
| `trashprompt.*` | `TrashPrompt::ask`: the question when a file can't go to the Recycle Bin (delete permanently / all such / continue / stop) |
| `freespace.*` | `FreeSpace::` — room on drives (`available`, `driveOf`, `sameDrive`; `isOutOfSpace` after a failed write = the drive has less than the bytes + 1 MiB), `LowSpaceBytes` (100 MB, download start warning), the DB reserve (see its rule). No GUI (texts via `QCoreApplication::translate`) |
| `thumbcache.*` | Grid thumbnails cached as `thumbs/<first 2 hex>/<sha256>.png` next to the DB (256 subfolders: FAT32 per-folder limit) (`ThumbCache::thumbnail` = load or make+store; stores only real previews of images and videos, never extension cards — a video Windows can't preview is tried again; "other" files skip the cache). All errors ignored |
| `third_party/xxhash/` | Vendored xxHash 0.8.3 single header (BSD-2, used header-only via `XXH_INLINE_ALL` in `collections.cpp`) |
| `i18n.*`, `languagedialog.*`, `translations/*.ts`, `flags/` | UI language (uk source, en/fr translations), first-start flag dialog, flag SVGs + rendered PNGs (48x32 and @2x) |
| `xgallery.ico`, `xgallery.rc`, `resources.qrc` | Program icon, rendered from `logo.svg` (each size 16–256 px rendered directly, centred on a transparent square, PNG entries). Rebuild the .ico when the SVG changes |

## Rules that must keep holding

- **Schema changes only through `migrations.cpp`**: append a new step function to the list,
  never edit existing steps. A DB with a newer `user_version` than `latestVersion()` is refused (not opened). At start
  `Db::repairCollections` fixes what an older version (which ignores newer schemas) may have written: files without
  vpath_hash are retagged, collections left without files are synced away, and updated_at below added_at (0) is set to added_at. Current version: 17 (galleries; rating+name; allow_upscale; no commas in tags;
  normalized tag spelling; virtual path; virtual path with digits after letters; '_' in tags is a space;
  collections — images.rating dropped; collections.name_key; tag groups; download profiles; gallery settings;
  collections.name_manual; ignored duplicates; download_profiles.attempts; images.updated_at).
- **DB reserve** (owner's rule): the last `FreeSpace::DbReserveBytes` (30 MB) of the DB's drive stay for SQLite (its
  rollback journal; no journal_mode is set). `dbReserveReached()` (free ≤ reserve) refuses at the start, with
  `reserveRefusal()`: add files/folder, `runImport` (after collecting), export into the gallery, `DownloadDialog::start`,
  folder sync whose direction copies into the gallery (only "Папку — як галерею" is left). Per file, before copying,
  `keepsDbReserve(target, bytes, addsToDb)` (a target on the DB's drive must leave more than the reserve after `bytes`;
  `addsToDb` = records are written, so the DB's drive must not be at the reserve whatever the target's drive):
  `Scanner::importFiles` (`ImportStats::reserveReached`, break, the rest is committed), folder sync copies
  (`ApplyResult::reserveReached`; `addsToDb` only for the gallery side), downloads before each file (with the bytes
  downloaded so far, `m_reserveReached` → import what's there). Reports use `reserveStop()`. `ThumbCache::store` stores
  nothing at the reserve (checked at most every 2 s). `confirmRoom` counts the DB drive's room minus the reserve.
  Image rotation is exempt (owner's decision).
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
- **Rescan**: all files incl. hidden/system, except `*.db`/`*.ini`, the program's own DB, the `thumbs` cache next to it and anything
  inside the exe folder (import and junk deletion skip that folder too). Unchanged = same size+mtime
  (no re-hash). Folder tags are added only to new files, so manual tag edits survive.
  Folder walks go through `Scanner::forEachFile`, never `QDirIterator::Subdirectories` (it enters junctions, knows no loops).
  Links to folders (junctions, symlinks) are followed only with the program-wide setting `follow_folder_links` ("1"; menu
  "Налаштування" → "Заходити в junction-папки", off by default; switching it either way asks first, describing
  the consequences — `MainWindow::setFollowFolderLinks`) by rescan and "add folder" (read on the GUI thread, passed in),
  and only after all folders reachable without them. Loops are always prevented: each folder is walked once by its
  identity (volume serial + file index on Windows, st_dev + st_ino elsewhere — also catches Linux bind mounts).
  Never followed, whatever the setting: junk search (deletes permanently; walked-twice mounts count as content) and the
  gallery folder offered as an import target (`chooseRootTag`; import refuses targets outside the gallery).
  Moves/renames: a missing record + one new file with the same sha256+size (1:1 only) keep
  id and manual tags; old-folder tags are swapped for new ones.
  All files of one scan/import get the same `added_at` (for "newest first").
- **Import** (add files/folder): copy, never move; "Перервати" in the busy dialog (`BusyDialog::setStopHandler`, an
  `std::atomic_bool` passed to `Scanner::importFiles` — not `QFuture::cancel`, which would drop the stats) stops after the
  current file and commits what was copied; room: `runImport` sums the sources' sizes in the collector job and
  `confirmRoom` (mainwindow.cpp, also used by folder sync) asks if a drive has less; a failed copy removes its target and,
  when `FreeSpace::isOutOfSpace`, stops the import (`ImportStats::outOfSpace`, the rest is committed); `failed` entries are
  "source — reason"; skip duplicates (DB, same batch, or same
  name+content already in the target); name clash → `name (1).ext`; records are written
  immediately (no rescan needed). The target folder is checked to stay inside the gallery; a
  Windows device name (CON, NUL, COM1…) gets `_` (`CON_`, `con_.old`), its tag stays `Con`.
- **Export** (metadata dialog, single file): a save dialog, then `MainWindow::exportFile`. Outside the gallery (or a place
  rescan ignores) = plain copy, replacing only after the copy succeeded. Inside the gallery = a question, then import with
  `ImportItem::keepDuplicate` (never overwrites: name clash → `name (1).ext`; folder tags only). The dialog can't close meanwhile.
- **Rename** (metadata dialog, single file, on Save after the meta): `MainWindow::renameImageFile`; the record keeps its id, tags and rating.
  Any failure leaves the old name and shows why; names rescan would ignore (`.db`, `.ini`) are refused.
  Menu "Налаштування" → keep extension (setting `keep_extension_on_rename`, program-wide, on unless "0"): the field then holds the name without the extension.
- **Deleting user files**: always ask first. Single files and duplicates go to the Recycle Bin
  (`QFile::moveToTrash`); if that fails, nothing is deleted. A single file (metadata dialog's button, Delete key in the
  main window's viewer; the metadata dialog's viewer only warns why not, `ImageViewer::setDeleteRefusal`) goes through `MainWindow::trashImageFile` (refused while busy). "Видалити сміття" deletes
  `Thumbs.db`/`desktop.ini` and empty folders permanently (as the owner asked), still behind a
  confirmation. "Видалити колекцію…" (`CollectionDialog::DeleteRequested`, one confirmation) trashes all the collection's
  files in a worker (`Scanner::moveFilesToTrash`, COM initialized there, progress + "Перервати"), then `Db::deleteImages` for
  the ones that went. It recycles via `Scanner::recycleFile` (IFileOperation + a sink that aborts when `PreDeleteItem` lacks
  `TSF_DELETE_RECYCLE_IF_POSSIBLE`, i.e. the shell would destroy the file): such a file, or any failure, is asked about on the GUI
  thread (`MainWindow::askAboutTrashProblem`, blocking queued call): delete permanently / all such / continue / stop.
  Single-file deletion (`MainWindow::trashImageFile`) and duplicates use `recycleFile` too, on the GUI thread; all three ask
  through `TrashPrompt::ask` (several = batch). `QFile::moveToTrash` is only the non-Windows fallback.
- **Rotation**: JPEG only edits/adds the EXIF orientation tag (never re-encodes unless the
  file is malformed; then quality 100 with the EXIF block copied). Other formats are
  re-encoded losslessly. Pending turns are counted mod 4; 0 = nothing to save.
- **Virtual path** (`virtualpath.*`): images.vpath = the file's tags in alphabetical order joined by `/`,
  images.vpath_key = its alphabet-aware sort key (Latin, Cyrillic, then digits: "Маша/Море/001";
  changing that order needs a migration step that recomputes all paths). `VirtualPath::update` writes only
  vpath/vpath_key (old migration steps use it); program code calls `Collections::retag`/`update` instead.
- **Collections** (`collections.*`, UI term "Колекція" for the virtual path): files with the same vpath. images.vpath_hash =
  xxh128 of vpath (16-byte BLOB); table `collections` (gallery_id, vpath_hash UNIQUE per gallery, name — duplicates allowed,
  may be empty → shown as the tags or `<без тегів>`; rating 1…100, new = 50; cover_image_id — first image by file name,
  NULL if the collection has no image; name_key = `Collections::nameKey` — sortKey of the shown name, i.e. of vpath
  when the name is empty; set on insert, rename and transfer). The rating lives only on collections. Whenever a file's tag set changes
  (meta save, bulk edit, tag rename/delete, rescan new/moved files, import) call `Collections::retag` then `sync` in the
  same transaction; after deleting records `sync` alone. `sync`: a collection left without files moves (same row id, new
  hash) to the row-less collection most of its files went to; otherwise it's deleted; row-less hashes get default rows;
  default name (`Collections::defaultName(vpath, groupRanks)`, owner's rule): "<word> <number>" — number = the smallest by value
  of the digits-only tags as written, else "001"; word = alphabetically (sortKey) first other tag of the last group (by
  position) that has one, ungrouped tags last, else "Collection" (not translated: stored data). Existing names are never
  recomputed automatically; `Db::defaultCollectionName` feeds CollectionDialog's "За замовчуванням".
  collections.name_manual (`CollectionRecord::nameManual`): 1 = the user gave the name. CollectionDialog: `textEdited` → manual,
  "За замовчуванням" → automatic, shown under the name; saved by `Db::updateCollection`. New rows (sync) are 0; a transfer keeps it.
  Migration v14 guessed it: a name equal to the old (last two tags) or the v14 rule (frozen copy `v14GroupedName`) = automatic.
  File menu "Перейменувати всі колекції…" (`MainWindow::renameAllCollections`, a question: only automatic / all / cancel) →
  `Db::renameCollections(galleryId, onlyAutomatic)`: default names with the current groups, all marked automatic, one transaction. Groups chosen for not-yet-stored tags
  must be in the DB before the save/import that names the collection: `Db::prepareTagGroups` creates those tags with their
  group first (metadata Save, bulk edit, download import; an unused one goes with the next `deleteUnusedTags`).
  invalid covers are re-chosen (a cover is valid while its image stays in the collection).
  Freshness (owner's rule): images.updated_at = added_at on insert; `Collections::touch` sets it to now for all files of a
  collection — `sync` does it for both sides of every Move (`from` if it still has files, `to`): tag edits, new/moved files,
  and deleted records (each deletion site calls `Collections::removing` before the DELETE: a Move from its collection to
  none — `Db::deleteImages`, rescan, folder sync); `Db::updateCollection` when the rating changes. The name never touches
  (owner's decision: "Перейменувати всі колекції…" would give every collection one date; the READMEs say so). CollectionDialog shows
  "Створена" = MIN(added_at) and "Оновлена" = MAX(updated_at) of its files (`CollectionRecord::createdAt/updatedAt`, only
  filled by `Db::collection`). Renaming a file doesn't change
  the cover. "Встановити обкладинкою" in the single-image metadata dialog (disabled if already the cover, absent in bulk
  mode) is applied after Save via `Db::setCollectionCover` (the collection the file is in after the tag save).
- **Thumbnail cache** (`thumbcache.*`): created at start (`ThumbCache::init`), keyed by sha256 so duplicates share one
  file; a failure to create/read/write just means no cache. When records disappear or change content (`Db::deleteImages`,
  `Db::deleteGallery`, `Db::updateImageFile` after rotation, rescan removed/updated files) collect the old hashes and call
  `ThumbCache::removeUnused` after the commit: it deletes only thumbnails no record of any gallery uses any more.
- Main window: the grid shows collections of the search result (`Db::searchCollections`: ordered by the position of
  their first file in the file order, paged by collection). Left click opens the collection (`m_openCollectionId`,
  row id — it survives transfers): grid = all its files (`TagQuery::collectionId`, ignores the search), search/sort/tag
  list locked, "◀ Закрити колекцію" (Esc) returns to the saved position. Right click on a collection → `CollectionDialog`.
  If the open collection's row is gone, `loadPage` falls back to the collection list.
- Grid size: as many cells (min. `ThumbnailSize`+16 × +32) as fit the grid area (`fitGridToSize` on its resize, buttons are
  made on demand and never deleted, extras hidden; reload 200 ms after the last change). Paging is by offset (`m_offset` = the
  top-left item, `m_collectionsOffset` to return to), not by page number: a resize keeps the top-left item in place, prev/next
  move by `pageSize()` (prev stops at 0, so the first page may be shorter); the label counts pages both ways from the offset.
  Window narrower than `CompactWidth` (900): the label is only "Сторінка N з M", the counts in its tooltip (`updatePageLabel`).
- Sorting (files joined to their collection row as `ic`): Classic = ic.rating DESC, ic.name_key, vpath_key,
  file name NOCASE, id (file name and id always last); NewestFirst = added_at DESC then classic;
  LargestFirst = size DESC then classic; UpdatedFirst ("Спочатку оновлені", combo between Newest and Largest, enum value
  last = saved setting) = updated_at DESC then classic. These order the collections; the files of an open collection
  (grid and viewer, images only) always use Classic, i.e. file name then id (owner's decision).
- Bulk tag edit (inside a collection only): checkboxes on file thumbnails (hidden for "other" files and for collections). The selection is a set of image ids
  (`m_selectedIds`) kept across pages/searches, cleared by "Зняти всі" or a gallery switch; "Обрати всі"
  ticks every file of the open collection (`m_selectableIds`, filled by `loadPage`), "Обрати сторінку" the current page only;
  each is enabled only while it would tick something. Right click on a file with 2+ selected → `Db::imagesByIds` + `Db::applyTagChanges`.
  Double click on a tag in the metadata dialog adds it to "Містить" without searching. Double click on a tag in the main
  tag list replaces "Містить" with it and searches (owner's rule); its right-click menu still adds.
- **Tag groups**: table `tag_groups` (gallery_id, name, position = user order; names unique per gallery only in the dialog);
  `tags.group_id` (NULL = no group, ON DELETE SET NULL; `Db::saveTagGroups` also nulls it explicitly). Edited in `GalleryDialog`
  (saved on its Save via `Db::saveTagGroups`), a tag is moved via the tag list's right-click menu "Група" (`Db::setTagGroup`).
  `Db::tagSections` splits a tag list: no group first, then groups by position, order inside kept. A deleted unused tag loses
  its group. Main tag list: a group header item before each group (`GroupHeaderRole`, `Qt::NoItemFlags`, "— name —"), hidden by the
  filter when no tag of its group is shown; tag items carry `TagGroupRole`. Changing a tag's group keeps the list's scroll position.
  The tag list has no horizontal scroll: `WrappingItemDelegate` (mainwindow.cpp) wraps long names (`WrappedText::layout`: word boundary, else anywhere),
  entries are as wide as the viewport, `ResizeMode::Adjust` re-lays them out on resize.
- Tag picker (`TagPickerDialog`, from the metadata dialog): new tags (comma list) + equal columns (as many as fit, min. 150 px each; start size 800×600) of checkboxes for all gallery tags
  (long names and group names wrap like the main tag list: `WrappingCheckBox`/`WrappingLabel`, height for width;
  each tag shown as "tag (N)", N = collections having it, `Db::tagCollectionCounts`, read when the dialog opening it opens, 0 for new tags)
  (each group starts a new row, its name a bold label taking a cell before its tags; the file's tags ticked, unticking removes them; bulk mode: tags
  only some files have (`ImageMetaDialog::setPartialTags`) are locked, partially checked; typing one in "Нові теги" unlocks it).
  It returns added/removed relative to the state it showed; boxes the user didn't touch follow `setAssignedTags`.
  Not modal (`show()`, a child of the metadata dialog, closes with it), so the preview and viewer stay usable. Writes nothing itself. Right click = a menu:
  "Видалити…" + confirmation marks a tag for deletion from the whole gallery (typing/pasting it again in the open picker takes that
  back); "Група" (only after `setTagGroups`) moves the tag to another group in the table at once (`groupChanges()` = tag → group vs.
  the sections given). Gallery-wide edits are written on the picker's own "Зберегти" (owner's decision: the metadata dialog's
  Cancel doesn't undo them): both the metadata and the download window emit `galleryTagsEdited(groupChanges, deleted)`, handled by
  `MainWindow::applyGalleryTagEdits` (`setTagGroups` → `Db::setTagGroup`, `deleteTagsFromGallery` → `Db::deleteTags`, reload list and
  page); the metadata dialog then gets fresh `setGalleryTags`/counts and drops deleted tags from its initial/partial lists. Groups of
  tags not stored yet stay pending (passed back to the picker as `pending`): `ImageMetaDialog::groupChanges()` is applied after its
  Save, the download window's `m_pendingGroups` after the run's import.
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
- **Download manager** (`DownloadDialog`, File menu "Завантажити з інтернету…", needs a gallery): GET only, one request at a
  time, random 0.1–3 s pause before every file, 60 s transfer timeout, error responses not saved. Profiles: table
  `download_profiles` (program-wide, name UNIQUE), saved like PuTTY sessions; JSON export/import replaces by name.
  Page mode: `page_N.html` (Content-Type text/html) or `.bin`, other profile fields locked. Files mode: tag any/img/a, source
  attribute (empty = body text without inner tags), classes (all present, case-sensitive), attributes (name case-insensitive,
  exact value case-insensitive); attribute links are resolved against the page (`<base href>`), a body's text only if absolute;
  only http(s), fragment dropped, each link once. Login/password/token and the tag list live only in the window; Basic if
  user or password is set, Bearer if the token is (both: Bearer sent, Basic answers a 401); only to the page's host, never
  after an https→http redirect; Qt's auth cache is cleared per run. Target: `<root folder>/<NNN>-collection` (NNN = max leading
  number of the root folder's subfolders + 1, 3 digits below 1000). File name = last link segment (decoded, made valid), no
  extension → `file_NNN.<ext from Content-Type or bin>`. Files are imported with `ImportItem::keepDuplicate`, `folderTags = false`
  and `tags` = the window's tags (owner's decision: no folder tags, the user picks the tags in the window). While it runs only "Зупинити завантаження" works (both tabs' fields disabled) and the window can't close; stopping
  still imports what was downloaded.
  Attempts (profile, files mode, 1…99, default 3, total incl. the first): any failure of a file (also 404, local errors) puts it
  in `m_failed` with a pause (`Result::retryAfterMs`, else 10 s); after the list, the longest pause is waited out (over 60 s only after
  a yes; no = finish as now, `m_retriesDeclined`), then the failed files (`m_queue` of indexes into `m_links`, `m_retrying`) are tried
  again into the same `m_targetDir` (one import at the end). `BusySpinner` + countdown during every pause; Stop works there too.
  The page itself is not retried.
  Room: `start` warns (yes/no) when the gallery's drive or the temp folder's has less than `FreeSpace::LowSpaceBytes`.
  A local write failure (`HttpFetcher::Result::writeFailed`, a failed close/flush, temp folder or file not created) is
  never retried: `localWriteFailed` → the run stops (`m_writeError` in the summary) and what was downloaded is imported.
- Per-gallery settings: table `gallery_settings` (gallery_id, key, value; cascade with the gallery), `Db::gallerySetting`/`setGallerySetting`.
  The root folder last chosen is remembered per gallery: downloads (`download_root_folder`, saved on a list pick or when the typed
  name is left) and imports (`import_root_folder`, shared by add files/folder, saved when `chooseRootTag` is accepted; it wins
  over the add-folder suggestion, which stays in the list). A remembered folder that no longer exists is ignored (first one as before).
- **Ignored duplicates**: table `ignored_duplicates` (gallery_id, path relative to the root, sha256; cascade with the gallery).
  "Ігнорувати" / "Не ігнорувати" (DuplicatesDialog's group menu, `Db::setDuplicatesIgnored`) store / drop every file of the
  group (each enabled only while it would change something). `Db::findDuplicates`
  marks such files `ignored` (path and sha256 both match); shown with "[пропущено]" before the path, still checkable for deletion.
  A group of ignored files only is shown only with "Показати пропущені дублікати" (not saved, off at start). Moved/changed files
  no longer match; entries no record matches are dropped when the dialog opens (`Db::pruneIgnoredDuplicates`).
- **Folder sync** (`foldersync.*`, File menu "Синхронізувати з папкою…", `MainWindow::syncWithFolder`): the chosen folder is the root of a
  "second gallery" (same relative paths, case ignored everywhere: `pathKey` = case fold); refused if it is, is inside or contains the
  gallery root. Flow: mode question (Quick / Reliable) → rescan (`runRescan`) → `compare` → result box ("Нічого не робити" /
  "Папку — як галерею" / "Галерею — як папку") → a second yes/no if anything is deleted or replaced → apply. Owner's rule: Quick = same
  path + size counts as synced unread, Reliable = sha256 always; mtime is never compared (other machine's clock). Both read only folder
  files whose size some unmatched gallery file has; gallery sha256 comes from the DB. Matching: path (+content) → same content elsewhere
  (`moved`, same file name preferred) → same path left on both sides (`changed`) → only in one. Folder walk never follows links and skips
  `Scanner::isIgnoredFile` and its own `.xgsync-part`/`.xgsync-tmp`; an unreadable folder file and the gallery file at its path are left alone.
  Apply order: deletes → moves (swaps/cycles: a blocker steps aside under `.xgsync-tmp`, names restored on stop) → copies (chunked into
  `.xgsync-part`, source mtime kept, never over an existing file; a replaced old version is deleted only once the new one is complete) →
  emptied folders `rmdir`ed, errors ignored. Room: before applying, `confirmRoom` with `FolderSync::bytesToCopy` per side
  (nothing freed is counted: deletes go to the Recycle Bin); a failed copy when `FreeSpace::isOutOfSpace` ends that plan's
  copies (`ApplyResult::outOfSpace`; a merge still tries the other side). Deletes go through `Scanner::recycleFile` + `TrashPrompt::ask` (several). Target folders are
  resolved to their on-disk spelling (`Folders`). Gallery side: moved records keep id/manual tags (`Scanner::moveRecord`; paths set to
  placeholders first, real ones at the end, since moves may swap paths), new files `Scanner::addRecord` (folder tags), changed keep the
  record; retag + deleteUnusedTags + sync, committed also when stopped. Progress in bytes; `BusyDialog::setSpinning` + "Перервати" via
  `setStopConfirmation` (the job pauses on `Control::pause` while asked).
  "Об'єднати" (`FolderSync::merge`, `Direction::Merge`, owner's rule): only copies, first gallery → folder, then folder → gallery
  (recorded with folder tags), each at the source's relative path; onlyInGallery / onlyInFolder plus the other side's version of each
  `changed` file; a taken name → `name (1).ext` (`numberedName`, like import); nothing deleted or moved, no second question.
- "Сховати чужі теги": the tag list shows `Db::tagsOfSearch` (tags of the last search's results).
- Tag filter field (`m_tagFilterEdit`, under that checkbox): always hides list entries not containing its text
  (case-insensitive); the `<без тегів>` service entry always stays. Only the user changes the text; it is not saved between runs.