# XGallery

**English** | [Українська](README.uk.md) | [Français](README.fr.md)

A Windows program for organizing a photo and video archive. It remembers the files in a chosen
folder, turns folder names into tags, lets you add your own tags and ratings, search by tags,
view photos, find duplicates and keep your folders tidy.

All information is kept in a database. The program changes the files themselves only when you
ask it to: rotating, deleting, adding, cleaning up junk.

## First start

1. **File → New gallery…** (or the "New" button). Enter a name and the root folder that holds
   your photos.
2. Agree to the rescan. The program walks through all subfolders and records the files in the
   database. A checksum (sha256) is computed for every file, so the first rescan of a large
   archive can take a while. Later rescans are fast because they only process changed files.

During long operations a progress window appears and the interface is locked until the
operation finishes.

## Galleries

The program can keep several independent galleries. The gallery selector and the "New" and
"Edit" buttons are in the top row of the main window.

In the edit window you can change the name, the root folder and the **"Allow enlarging images"**
option: with it, small photos are stretched to fill the viewer window. The same window has a
**"Delete gallery…"** button. It removes everything about the gallery from the database but
leaves the files on disk untouched.

## Tags

- **Automatic.** Every folder between the root folder and the file becomes a tag. The file
  `Vacation/2024/sea.jpg` gets the tags "Vacation" and "2024". A comma in a folder name is
  replaced with a space.
- **Your own.** Right-clicking a thumbnail opens the metadata window. There you can add an
  existing tag, create a new one, remove a tag and change the **rating** (0–100, 50 by default).
  Changes are written with the "Save" button. A tag that is no longer used anywhere is deleted
  automatically. Double-clicking a tag in this window adds it to the "Contains tags" field of the
  main window. The search itself only runs when you click "Search".
- **Bulk editing.** Tick the checkboxes in the corner of the thumbnails. The selection is kept
  when you move between pages and even between different searches, so you can collect as many
  files as you like. The number of selected files is shown under the grid ("Selected: N").
  Right-clicking any thumbnail opens a window with the tags common to all selected files. Added
  and removed tags apply to all selected files at once; a file that already has an added tag gets
  it only once. **"Check all"** ticks every file on the current page, **"Uncheck all"** clears the
  ticks on all pages. Switching the gallery clears the selection.
- **Viewing from the edit window.** Clicking the thumbnail in the metadata window opens the photo
  in the viewer (without browsing) or the video in its program. In bulk editing, clicking a row of
  the file list works the same way, and the arrows in the viewer browse only the selected photos.
- **Spelling.** All tags are stored in one form: first letter upper case, the rest lower case
  ("cats", "CATS" → "Cats"). An underscore `_` reads as a space, so the folders `_april` and
  `april` give one tag "April", and `new_year` becomes "New year". Tags typed into the search
  fields are brought to the same form. The characters `,` `<` `>` `/` and `\` are not allowed in
  tag names.
- **`<no tags>`** is a service entry, always first in the list. It finds the files that have no
  tags at all, or excludes them from the search.

## Search and sorting

- **"Contains tags"** — a file must have all the listed tags (comma-separated).
- **"Does not contain"** — a file must have none of these tags.
- **"Exact"** (the checkbox next to "Contains tags") — finds only files that have exactly these
  tags and no others. The "Does not contain" field is disabled in this mode. If "Contains tags"
  is empty, files without tags are found.
- Double-clicking a tag in the list on the left adds it to "Contains tags". Right-clicking lets
  you choose where to add the tag, and also **rename** it. If a tag with the new name already
  exists, nothing is renamed.
- **Sorting:**
  - "Classic" — first by **virtual path**, then higher ratings, then by file name. The virtual
    path is all the file's tags in alphabetical order joined with "/", digits after letters, for
    example "Kate/Sea/2024". So photos with the same set of tags stay together, no matter which
    folder they are in;
  - "Newest first" — the files added to the gallery most recently come first (handy after
    adding a new batch of photos), then as in Classic;
  - "Largest first" — the biggest files first (handy for freeing up space), then as in Classic.
- **"Hide other tags"** (the checkbox above the tag list) — the list keeps only the tags of the
  files found by the last search. `<no tags>` is always shown.

Results are shown in pages of 16 thumbnails; use the buttons under the grid to page through.
The tooltip of a thumbnail shows the path, rating, size and tags.

## Interface language

The program speaks Ukrainian, English and French. On the first start a window with three flag
buttons appears; if you close it, the language is Ukrainian and the question is asked again next
time. You can change the language at any time in the **Language** menu — the window is
translated immediately, without a restart. The choice is stored in the same database as the
galleries.

## Viewer

Clicking a photo thumbnail opens it in the viewer.

| Action | What it does |
|---|---|
| ← → (↑ ↓) | previous / next photo of the search results, in a loop |
| Double click | full screen / back to the previous size |
| Right click | rotate 90° counter-clockwise |
| Esc | leave full screen; the next Esc closes the window |

After a rotation a **"Save to file"** bar appears. A JPEG is not re-encoded: only the
orientation mark in its metadata changes, so neither quality nor shooting data is lost, however
many times you rotate the photo. PNG, BMP, TIFF and WebP are rewritten without quality loss.
GIF, animated WebP and multi-page TIFF can't be saved: only their first frame would remain. If
you close the window or the program with an unsaved rotation, the program asks what to do
with it.

## Videos and other files

- **Videos** (mp4, mov, avi, mpg, mkv, wmv and others) are shown as a frame, like in Windows
  Explorer. A click opens the video in the program Windows assigns to it.
- **Other files** are shown as a card with their extension and don't open anything.
- `*.db` and `*.ini` files, including `Thumbs.db` and `desktop.ini`, never get into a gallery.
  The program's own folder (exe, libraries, database) is skipped too if it lies inside a gallery.

## The "File" menu

- **Rescan (F5)** — synchronizes the database with the disk: adds new files, removes missing
  ones, updates changed ones. If you moved or renamed a file, the program recognizes it by its
  content, so its rating and your own tags are kept.
- **Add files to gallery…** — choose files (several at once if you like), then the first-level
  gallery folder to copy them into. You can type a new name and the folder is created. The
  folder name becomes a tag.
- **Add folder to gallery…**
  - if the chosen folder contains files, the program asks which gallery folder (tag) to copy all
    of its content into, subfolders included;
  - if it contains only subfolders, each of them becomes a gallery folder (tag) of its own.

  In both cases the files are **copied**, the originals stay where they are. Duplicates are
  skipped. If a file with the same name already exists but its content differs, the new copy gets
  the suffix `(1)`, `(2)`… At the end you see how many files were copied and how many duplicates
  were skipped. Names that Windows reserves for devices (`CON`, `NUL`, `COM1`…) get a `_` in the
  folder name (`CON_`), while the tag stays without it ("Con").
- **Find duplicates…** — groups of files with identical content. Tick the extra ones and click
  "Delete": the files go to the **Recycle Bin**. If you tick all the files of a group, the first
  one is kept.
- **Delete junk…** — deletes `Thumbs.db`, `desktop.ini` and empty folders in the gallery.
  The deletion is **permanent, without the Recycle Bin**, so a list and a confirmation come
  first. The program's own folder is left alone.

A single file can be deleted in the metadata window (right-click a thumbnail): the
**"Delete file…"** button moves it to the Recycle Bin.

## Portable mode (for a USB stick)

The program looks for the database `xgallery.sqlite` next to its exe first, and if it isn't
there, in `%APPDATA%\Nova30\XGallery\`.

To make a portable version, put your database from AppData, or simply an empty file
`xgallery.sqlite`, next to the exe. In portable mode:

- the window title says **[Portable]**; in normal mode it says **[Full]**;
- the root folder of a new gallery must be on the same drive as the program. Its path is stored
  relative to the exe, so the gallery keeps working even if the USB stick gets a different drive
  letter. A gallery on another drive that came with a database from AppData can still be edited.

The path of the database in use is shown in the status bar for a few seconds after the start.

## Backup

All tags, ratings and galleries are stored in the single file `xgallery.sqlite`. To make a
backup, copy it while the program is closed. A new version of the program may update the
database on its first start, and an older version doesn't know about those changes and will not
work with such a database correctly. So make a copy before updating the program.

## License

This project is licensed under the [MIT License](LICENSE).

## Third-party notices and distribution

- [Qt](https://www.qt.io/) — application framework ([LGPL v3](licenses/lgpl-3.0.txt))
