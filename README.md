# XGallery

**English** | [Українська](README.uk.md) | [Français](README.fr.md)

A Windows program for organizing a photo and video archive. It remembers the files in a chosen
folder, turns folder names into tags, lets you add your own tags, groups files with the same tags
into rated collections, searches by tags, shows photos, finds duplicates and keeps your folders tidy.

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

The **"Tag groups"** list in the same window holds your own groups of tags, for example "People" and
"Places". "Add…", "Rename…" (or a double click) and "Delete" change the list; "Up" and "Down" move the
selected group. The changes are written with "Save". Deleting a group doesn't delete its tags: they
just stay without a group.

## Tags

- **Automatic.** Every folder between the root folder and the file becomes a tag. The file
  `Vacation/2024/sea.jpg` gets the tags "Vacation" and "2024". A comma in a folder name is
  replaced with a space.
- **Your own.** Inside a collection (see below), right-clicking a file's thumbnail opens the
  metadata window. There you can remove a tag from the file or add new ones. Changes are written
  with the "Save" button. A tag that is no longer used anywhere is deleted automatically. Double-clicking a
  tag in this window adds it to the "Contains tags" field of the main window. The search itself
  only runs when you click "Search".
- **Tag groups.** Each tag belongs to one group of the gallery or to none. The tag list of the
  main window shows the tags without a group first, then the groups in the order set in the gallery's
  edit window; inside a group the tags go alphabetically. Each group starts with its name in grey
  italics between dashes (— People —): it is not a tag and can't be selected. To move a tag,
  right-click it in the list and choose **"Group"** → "No group" or the group you need; the current
  one is ticked. A tag that is no longer used anywhere is deleted together with its group membership.
- **Adding tags.** The "Add tags" button in the metadata window opens a window with a "New tags:"
  field (several tags separated by commas) and a table of all the gallery's tags, grouped as in the
  main window's list: each group starts a new row with its name (in bold) before its tags. Long names wrap onto more lines; the wider the window, the more columns. The number in brackets after a tag is how many collections have it (collections, not files). The file's tags
  are ticked: tick the ones to add, clear the ones to remove and click "Save". In bulk editing, tags
  that only some of the selected files have are greyed out and can't be changed; typing such a tag
  into "New tags:" gives it to all the files. While it is open you can still open the photo from
  the metadata window; closing the metadata window closes it too. Right-clicking a tag in the table opens a
  menu: **"Delete…"**, after a confirmation, deletes it from **all files of the gallery**; **"Group"** → "No
  group" or another group moves it there (it is shown in that group right away, the current one is ticked).
  Both change the whole gallery, so they are written as soon as you click "Save" in this window ("Cancel"
  here keeps the tag as it was; "Cancel" in the metadata window no longer undoes them).
- **Copying and pasting tags.** In the tag list of this window (also in bulk editing) select tags
  with the mouse or keyboard and press Ctrl+C, or use "Copy tags" in the right-click menu. Ctrl+V or
  "Paste tags" adds a list from the clipboard: tags are separated by commas or line breaks, the
  characters `<` `>` `/` `\` are replaced with spaces, the spelling is the usual one, and tags the
  file already has are skipped. Handy for giving the same set of tags to different files.
- **Bulk editing.** Inside a collection, tick the checkboxes in the corner of the thumbnails. The
  selection is kept when you move between pages, collections and even different searches, so you
  can collect as many files as you like. The number of selected files is shown under the grid
  ("Selected: N"). Right-clicking any file's thumbnail opens a window with the tags common to all selected files. Added
  and removed tags apply to all selected files at once; a file that already has an added tag gets
  it only once. **"Check all"** ticks every file of the open collection (all its pages), **"Check page"** only the files of the current page, **"Uncheck all"** clears the
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
  tags at all, or excludes them from the search. In the list and in the search fields it is shown
  in bold on a tinted background, so it is not confused with an ordinary tag named "No tags".

## Collections

Files with exactly the same set of tags form a **collection**. Its tags in alphabetical order,
joined with "/", digits after letters, make the collection's path, for example "Kate/Sea/2024". So
photos with the same set of tags stay together, no matter which folder they are in.

- The main window shows the **collections** of the found files: a cover and a name each. They come
  in the order of their first file in the search results (see "Sorting"), as many per page as fit in the window.
- **Name** — a new collection is named "<word> <number>". The number is the smallest (by value) of
  its tags made of digits only, written as the tag is ("005"), or "001" if there is none. The word is
  the alphabetically first of its other tags in the **last** tag group (by the groups' order) that has
  any of them; if none, the group before it, and so on; tags without a group come last; "Collection"
  if there are no such tags. So with groups "People", "Places": tags Mary, Forest (Places), 005, 2024 →
  "Forest 005"; files without tags → "Collection 001". Put the new tags into groups first (the
  "Add tags" window, right click → "Group") so a new collection gets the name you want. Existing
  collections keep their names; "Default" in the collection properties fills in this name. You can
  change it; names may repeat. A collection whose name was cleared is shown by its tags, or `<no tags>`.
- **Rating** — 1–100, 50 for a new collection. The rating belongs to the collection, not to single
  files. On the update from a version with file ratings, each collection got the average rating of
  its files, rounded up.
- **Cover** — by default the first photo of the collection by file name. To choose another, open
  the photo's metadata window inside the collection and click **"Set as cover"** (it takes effect
  on "Save"; the button is grey if the photo already is the cover, and absent in bulk editing). If
  the cover leaves the collection, the first photo becomes the cover again. A collection of videos
  or other files only shows its first file.
- **Right click** on a collection opens the "Collection properties" window: name (the "Default"
  button brings back the default name) and rating. **"Delete collection…"** moves all its files to
  the Recycle Bin and removes them from the gallery, after one confirmation; the progress window has
  a **"Stop"** button (files already moved stay deleted, the rest stay in the collection). A file
  that can't go to the Recycle Bin (too big for it, or a drive without one) is never deleted silently:
  the program asks whether to delete it permanently (this file, or all such files) or to stop. If a
  file can't be moved for another reason, you choose between continuing (it stays) and stopping.
- **Left click** opens the collection: the grid shows its files, page by page. Search, sorting and
  the tag list are locked meanwhile; **"◀ Close collection"** (or Esc) goes back to the list of
  collections, to the page you came from. Inside a collection a click on a file opens it, a right
  click opens its metadata window, and bulk editing works as usual.
- **When tags change.** If a collection loses all its files (you changed the tags of all of them,
  renamed or deleted a tag, moved its folder and ran a rescan), its name, rating and cover go to
  the new collection the files went to. If at least one file stays, the new collection gets default
  properties. If the files join a collection that already exists, it keeps its own properties. An
  open collection follows its files the same way; if it disappears, the program goes back to the
  list of collections.

## Search and sorting

- **"Contains tags"** — a file must have all the listed tags (comma-separated).
- **"Does not contain"** — a file must have none of these tags.
- **"Exact"** (the checkbox next to "Contains tags") — finds only files that have exactly these
  tags and no others. The "Does not contain" field is disabled in this mode. If "Contains tags"
  is empty, files without tags are found.
- Double-clicking a tag in the list on the left adds it to "Contains tags". Right-clicking lets
  you choose where to add the tag, and also **rename** it. If a tag with the new name already
  exists, nothing is renamed.
- **Sorting** puts the found files in order; each collection stands where its first file is.
  Inside an open collection (and in the viewer) files always go by file name, whatever the sorting:
  - "Classic" — by the collection's **rating** (higher first), then by the collection's **name**
    as you see it (alphabetically, case doesn't matter), then by its path (see above). For example, when searching for "Kate", "Kate/Village/2024"
    with rating 90 comes before "Kate/Sea/2024" with rating 10;
  - "Newest first" — the files added to the gallery most recently come first (handy after
    adding a new batch of photos), then as in Classic;
  - "Largest first" — the biggest files first (handy for freeing up space), then as in Classic.
- **"Hide other tags"** (the checkbox above the tag list) — the list keeps only the tags of the
  files found by the last search. `<no tags>` is always shown.
- **"Tag filter…"** (the field under that checkbox) — as you type, the tag list keeps only the
  entries containing the text, case-insensitively (`<no tags>` always stays). It always applies, also
  together with "Hide other tags", and the text stays until you clear it yourself (the cross in
  the field or the keyboard) or close the program.

Results are shown in pages; use the buttons under the grid to page through. A page holds as many
thumbnails as fit in the window: making the window bigger adds columns and rows, making it smaller
removes them. The thumbnail at the top left stays where it is, so the page number and the number of
pages may change (the first page may then be shorter than the others). In a window narrower than 900 pixels
the line under the grid shows only "Page N of M"; the numbers of collections and files are in its tooltip. The tooltip of a collection shows its name, tags, rating and number
of files; the tooltip of a file shows its path, size and tags.

## Interface language

The program speaks Ukrainian, English and French. On the first start a window with three flag
buttons appears; if you close it, the language is Ukrainian and the question is asked again next
time. You can change the language at any time in the **Language** menu — the window is
translated immediately, without a restart. The choice is stored in the same database as the
galleries.

## Viewer

Clicking a photo thumbnail inside a collection opens it in the viewer.

| Action | What it does |
|---|---|
| ← → (↑ ↓) | previous / next photo of the open collection, in a loop |
| Double click | full screen / back to the previous size |
| Right click | rotate 90° counter-clockwise |
| Delete | move the photo to the Recycle Bin and remove it from the gallery (after a confirmation); the next photo is shown |
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
  The program's own folder (exe, libraries, database) and the `thumbs` folder next to the database
  are skipped too if they lie inside a gallery.
- Folder links (NTFS junctions and symbolic links to folders) are skipped by default, so files they
  lead to don't get into the gallery. With **"Enter junction folders"** ticked in the **Settings**
  menu, rescan and "Add folder" follow them too. Switching the option either way first shows a window
  describing what will happen. Either way every folder is walked only once, so a
  link pointing back up can't make the program loop, and a folder reachable both directly and
  through a link is recorded under its own path. Unticking the option makes the next rescan remove
  the files found through links from the database, together with their tags. "Delete junk" never
  enters such folders, and a folder link can't be chosen as the gallery folder to add files to.

## The "File" menu

- **Rescan (F5)** — synchronizes the database with the disk: adds new files, removes missing
  ones, updates changed ones. If you moved or renamed a file, the program recognizes it by its
  content, so your own tags are kept.
- **Add files to gallery…** — choose files (several at once if you like), then the first-level
  gallery folder to copy them into. You can type a new name and the folder is created. The
  folder name becomes a tag. The folder chosen last (for "Add files" and "Add folder" alike) is
  remembered for each gallery and offered first, while it exists.
- **Add folder to gallery…**
  - if the chosen folder contains files, the program asks which gallery folder (tag) to copy all
    of its content into, subfolders included;
  - if it contains only subfolders, each of them becomes a gallery folder (tag) of its own.

  In both cases the files are **copied**, the originals stay where they are. Duplicates are
  skipped. If a file with the same name already exists but its content differs, the new copy gets
  the suffix `(1)`, `(2)`… At the end you see how many files were copied and how many duplicates
  were skipped. **"Stop"** in the progress window ends the copying after the current file; the files
  copied so far stay in the gallery. Names that Windows reserves for devices (`CON`, `NUL`, `COM1`…) get a `_` in the
  folder name (`CON_`), while the tag stays without it ("Con").
- **Download from the Internet…** — the download manager, see
  [Downloading from the Internet](#downloading-from-the-internet).
- **Find duplicates…** — groups of files with identical content. After each file's path its
  collection is shown in brackets; its tooltip shows the whole virtual path (its tags). Tick the extra ones and click
  "Delete": the files go to the **Recycle Bin**. If you tick all the files of a group, the first
  one is kept.
- **Delete junk…** — deletes `Thumbs.db`, `desktop.ini` and empty folders in the gallery.
  The deletion is **permanent, without the Recycle Bin**, so a list and a confirmation come
  first. The program's own folder is left alone.

A single file can be deleted in the metadata window (right-click a file's thumbnail): the
**"Delete file…"** button moves it to the Recycle Bin. In the viewer the Delete key does the same (not in a viewer opened from the metadata window: there it only explains why). Here, in "Find duplicates…" and when deleting a collection, a file that
can't go to the Recycle Bin (too big for it, or a drive without one) is never deleted silently: the
program asks first whether to delete it permanently.

The **"Export…"** button in the same window copies the file out of the gallery to a place you
choose. If that place is inside the current gallery, the program first asks whether you really
want a duplicate; the copy then joins the gallery under the usual import rules: tags from its
folders and, if the name is taken, `photo (1).jpg`. Manually added tags are not carried over to
the copy.

The **"File name:"** field in the same window renames the file on disk when you press "Save"; its
tags stay. If the rename fails (the name is taken or has forbidden characters), the
file keeps its old name and the program shows an error.

While **"Keep the extension when renaming"** is on in the **Settings** menu (the default), the
field shows only the name without its extension, and the extension can't be changed. Turn it off
and the field shows the full name, so the extension can be changed too.

## Downloading from the Internet

**File → Download from the Internet…** opens the download manager. It downloads a web page, or
the files the page links to, into the current gallery. It sends only GET requests, one at a
time. The window has two tabs: **"Profile"** (the profile list and the profile's settings, shown
first) and **"Download"** (the page address and access, the tags, the root folder and the start
button).

**Profiles** describe how to find the files on a page. They work like saved sessions in PuTTY:
type a name and click **"Save"** (a profile with that name is replaced after a question); select
a profile in the list and click **"Load"** (or double-click it) to fill in the fields; **"Delete"**
removes the selected profile. Profiles belong to the program, not to a gallery.
**"Export…"** saves all profiles to a JSON file, **"Import…"** adds the profiles of such a file
(profiles with the same names are replaced).

A profile holds:

- **User-Agent** — sent as the `User-Agent` header; empty means Qt's default one.
- **Mode**:
  - **Download the page** — the page itself is saved as `page_1.html` (or `page_1.bin` if the
    server says it isn't `text/html`). The other fields of the profile are not used and are locked.
  - **Download files from the page** — the program looks for links on the page with these fields:
    - **Tag** — any, `img` or `a`.
    - **Link source** — the name of an attribute (`src`, `href`, `data-original`…) the link is
      taken from. Empty: the text inside the tag is taken (inner tags removed).
    - **Class** — classes separated by spaces: the tag must have all of them, in any order. Case
      matters, as in HTML. Empty: any tag.
    - **Attributes** — "attribute = value" pairs the tag must also have; the value must be exactly
      this, but case doesn't matter. Spaces around names and values are removed on saving.

Links from an attribute may be relative (`/img/1.jpg`, `../a.jpg`): they are completed with the
page's address. Only links starting with `http://` or `https://` are downloaded; the same link is
downloaded once.

Below the profile:

- **Page address** — the page to download.
- **User name**, **Password**, **Bearer token** — for sites that need a login. They are
  **never saved** and are cleared when the window closes. Basic authentication is sent if the user
  name or the password is filled in, the `Bearer` header if the token is. If both are filled in, the
  token goes with every request and the user name and password are given when the server asks for
  them. They are sent **only to the site of the page address**: if the server redirects to another
  site (or from `https` to plain `http`), they are not sent there.
- **Tags of the downloaded files** — these tags (and only these) are given to every downloaded
  file. "Add tags" works as in the metadata window. A new tag the gallery doesn't have yet gets the
  group chosen for it when a download adds it. The list is cleared when the window closes.
- **Root folder** — a first-level folder of the gallery (or a new name). The folder chosen last is
  remembered for each gallery and offered next time (the first one if it no longer exists). Each download goes into a
  new numbered folder inside it: the program finds the folders whose names start with a number
  (`1-sea`, `004 - village - 2`, `05 - summer`), takes the largest number, adds 1 and appends
  `-collection`: here `006-collection`. Below 1000 the number has leading zeros.

**"Start download"** starts the work. Before each file there is a random pause of 0.1–3 seconds,
so the site is not overloaded and its bot protection is not triggered. Meanwhile the window shows
a progress bar and everything except **"Stop download"** is locked. What has been downloaded before
a stop is still added to the gallery. A request with no data for 60 seconds fails; error responses
(404 and the like) are not saved. In the end you see how many links were found, downloaded and
failed, and which ones failed.

A file is named after the last part of its link without the query: from
`https://site.com/1280/7/279/82840051/82840051_018_7358.jpg?ver=1` comes `82840051_018_7358.jpg`.
A link without an extension (`https://site.com/1280/7/279/82840051`) gives `file_007.jpg`, where
007 is the link's number on the page and the extension follows the server's `Content-Type`
(`.bin` if it says nothing known). A name that is already taken gets `(1)`, `(2)`… Duplicates are
**not** skipped here: better a duplicate than a missing file; "Find duplicates…" removes them.

The folders (the root folder and `006-collection`) give the downloaded files no tags. A rescan
doesn't add them either, since the files are already known; they become tags only if the gallery
is created anew from its folder (and, as usual, a moved file gets the tags of its new folders).

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

All tags, collections and galleries are stored in the single file `xgallery.sqlite`. To make a
backup, copy it while the program is closed. A new version of the program may update the
database on its first start, and an older version doesn't know about those changes and will not
work with such a database correctly. So make a copy before updating the program. From this version
on, the program refuses to open a database updated by a newer version and says so. Files that a
version older than this one added or deleted are put in order (collections) on the next start.

Next to the database lies the **`thumbs`** folder: ready-made thumbnails, so pages with big photos
open quickly. Each thumbnail is named after the file's content (and lies in one of 256 subfolders, so no folder
gets too big), so duplicates share one, and it is
deleted together with the last file that uses it. The folder needs no backup and can be deleted at
any time: the program simply makes the thumbnails again. If the folder can't be created or written
to (for example, on a write-protected USB stick), the program works without it, only slower.

## License

This project is licensed under the [MIT License](LICENSE).

## Third-party notices and distribution

- [Qt](https://www.qt.io/) — application framework ([LGPL v3](licenses/lgpl-3.0.txt))
- [xxHash](https://github.com/Cyan4973/xxHash) 0.8.3 — hash of a collection's tags
  ([BSD 2-Clause](third_party/xxhash/LICENSE))
