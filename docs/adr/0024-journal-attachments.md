# ADR 0024 — Journal photo attachments (file-backed, entry-keyed)

**Status:** accepted · **Builds on:** ADR 0004
(persistence baseline / `journal_entries`, the change_log), ADR 0010 (backup/restore), ADR 0002
(QML-singleton injection).

A journal entry today carries only text (`note`). The product needs **photos** — a leaf close-up on
an `Observation`, before/after shots on a `Repotting` — and the vision tool needs
something to read. Photo storage was deferred from an earlier phase (the deferred-attachment note in
`ijournalrepository.h`); this ADR lands it. It is a **general persistence improvement, AI-agnostic** —
the vision plumbing + `read_plant_photo` tool that consume it are ADR 0025.

## Decisions

1. **A photo is an `Attachment` keyed to a journal entry, not a `JournalEntryKind`.** The kind enum
   stays purely semantic (`Note`…`Observation`, plus `Memory`); a bare photo is a genuine
   `Observation` *with an attachment*, and one entry owns **zero-or-many** attachments (before/after
   on one `Repotting`). The value type:
   ```cpp
   struct Attachment {
       AttachmentId id;          // app-minted UUIDv7, like every syncable entity
       JournalEntryId entry;     // FK — the entry is the stable anchor
       QString fileRef;          // app-data-relative path: "attachments/<uuid>.<ext>"
       QString caption;          // free text — "Before" / "After"; disambiguates multiple shots
       QDateTime addedAt;        // UTC; stamped from the injected Clock at the business edge
   };
   ```
   A new `AttachmentId` joins the identity vocabulary (`core/ids.h`, UUIDv7 like `JournalEntryId`).

2. **Files on disk, only metadata in the DB.** `fileRef` is an **app-data-relative path to a file**
   (`<AppData>/attachments/<uuid>.<ext>`), never a BLOB and never absolute — so the DB stays small and
   portable (a copied/restored DB on another machine still resolves relative paths). This is the
   persistence design's "leaning files" decision; the cost (a separate file-sync channel for sync) is
   accepted and deferred.

3. **Schema v8: an `attachments` table; cascade-deletes with its entry.**
   ```sql
   CREATE TABLE attachments (
     id        TEXT PRIMARY KEY,                                                -- UUIDv7
     entry_id  TEXT NOT NULL REFERENCES journal_entries(id) ON DELETE CASCADE,
     file_ref  TEXT NOT NULL,
     caption   TEXT NOT NULL DEFAULT '',
     added_at  TEXT NOT NULL)                                                   -- UTC ISO-8601
   CREATE INDEX idx_attachment_entry ON attachments(entry_id, added_at);
   ```
   Bump `kSchemaVersion` 7 → 8, one migration step `"journal-attachments"` following the
   `MigrationRunner` pattern (transactional, fail-loud, version bumped only on commit). The FK
   cascade means deleting an entry (or, transitively, a plant) drops its attachment **rows**.

4. **Two seams, kept apart: `IAttachmentRepository` (metadata) + `IAttachmentFileStore` (bytes).** A
   filesystem copy cannot join a DB transaction, and mixing them would make the SQLite repository's
   parity suite touch the disk. So:
   - `IAttachmentRepository` — `add` / `updateCaption` / `remove` / `forEntry` / `all`, the metadata
     rows only. The triple: interface + `SqliteAttachmentRepository` (writes a `change_log` row per
     mutation, in the same transaction, via `detail::appendChangeLog`) + `InMemoryAttachmentRepository`,
     both run through the **same** behavioural suite (`test_attachmentrepository`). The in-memory fake
     never touches the disk.
   - `IAttachmentFileStore` — `store(sourcePath, id) → fileRef` / `read(fileRef) → optional<QByteArray>`
     / `remove(fileRef)` / `absolutePath(fileRef)`. `DiskAttachmentFileStore` (rooted at
     `<AppData>/attachments`) + `TempAttachmentFileStore` (a `QTemporaryDir`-rooted fake) so file tests
     stay hermetic.

5. **Orphan-file cleanup is mark-and-sweep at startup, plus an immediate delete in the edge.** SQLite
   cascade drops attachment *rows* but cannot delete *files*. So:
   - A single-photo delete goes through the business edge and removes the **row first, then the file**
     — a crash mid-way leaves a harmless orphan file, never a row pointing at a missing file (the safe
     failure direction).
   - For cascade deletes (entry/plant), the rows vanish without the edge seeing them. The invariant we
     guarantee is "no row without a file"; the reverse (a file without a row) is just wasted disk. So
     `sweepOrphans()` — list every file under `<AppData>/attachments`, delete any whose `fileRef` is in
     no `repo.all()` row — runs **once in the composition root after migrations**. It is robust to any
     deletion path (cascade, raw SQL, restore) and **never deletes a row for a missing file**, so a
     restored backup whose files are absent keeps its rows and renders an "unavailable" placeholder.

6. **The business edge is an `AttachmentController` (klr_gui).** A `QObject` injected with
   `IAttachmentRepository&` + `IAttachmentFileStore&` + `Clock&`, exposing `Q_INVOKABLE addPhoto(entry,
   sourceUrl, caption)` / `removePhoto(id)` / `setCaption(id, text)`. It mints the `AttachmentId`, asks
   the file store to copy the file in, stamps `addedAt` from the **injected Clock** (never
   `currentDateTime()`), then writes the row; on remove it does row-then-file. Exposed to QML through
   `AppContext` (no `setContextProperty`, ADR 0002). This keeps file I/O out of the repository and QML
   thin (the minimize-QML rule).

7. **Backup carries attachment *metadata* now; file bytes are deferred.** `BackupSerializer` /
   `BackupImporter` gain an `attachments` array (`{id, entry, fileRef, caption, addedAt}`, id-preserving
   import); bump the backup format version. The **files themselves are not in the JSON** — this matches
   the persistence design's open question ("how the sync payload ships the file bytes"). A restore on a
   fresh machine therefore has rows whose files are absent; decision 5's sweep must not delete those
   rows, and the UI shows "image unavailable". Full file-bytes-in-backup (or a content-addressed side
   channel) is an explicit follow-up.

## Implementation order (one commit each, ctest green throughout)

1. This ADR.
2. `AttachmentId` in `core/ids.h` + the `Attachment` value type (`persistence/attachment.h`).
3. **Schema v8**: append the `"journal-attachments"` migration to `schema.cpp`, bump `kSchemaVersion`
   + the `schema.h` doc comment; extend `test_migration` (table exists at vN; v7→v8 non-destructive;
   FK cascade declared).
4. `IAttachmentRepository` + `InMemoryAttachmentRepository`; start `test_attachmentrepository` against
   the in-memory impl.
5. `SqliteAttachmentRepository` (+ change_log per mutation); run the same parity suite against it +
   an SQLite-only cascade-on-entry-delete test. Add both to the persistence CMake list.
6. `IAttachmentFileStore` + `DiskAttachmentFileStore` + `TempAttachmentFileStore` incl. `sweepOrphans`;
   `test_filestore` (store/read/remove idempotent; sweep deletes file-without-row, keeps file-with-row).
7. Composition root (`app/main.cpp`): resolve `<AppData>/attachments`, construct the file store +
   attachment repo, run `sweepOrphans()` after migrations (incl. the in-memory fallback branch).
8. `AttachmentController` (klr_gui) + expose via `AppContext`; a small controller test.
9. Journal UI: `PlantJournalModel` attachments/thumbnails role + the journal delegate's add/view/
   caption affordances (qmllint/cachegen).
10. Backup: `attachments` array in `BackupSerializer`/`BackupImporter`, bump the format version,
    tolerate a missing file on restore; extend `test_backupserialize` / `test_backupimport`.

## Tests

`test_migration` (v8 table + v7→v8 non-destructive + cascade), `test_attachmentrepository` (parity:
add/forEntry/updateCaption/remove/all, change_log per mutation, SQLite cascade-on-entry-delete),
`test_filestore` (store/read/remove/sweep over a `QTemporaryDir`), `test_appcontext` (the
`AttachmentController` add/remove/caption edge stamps the injected clock and coordinates repo+file
store), `test_backupserialize` + `test_backupimport` (attachment round-trip; a missing-file `fileRef`
restores without crashing). The journal QML delegate stays qmllint/cachegen-only by design.

## Out of scope / follow-ups

- **Vision plumbing + the `read_plant_photo` tool** — ADR 0025 (consumes this layer).
- **File bytes in backup / sync** — deferred (decision 7); metadata-only for now.
- **Non-photo media** (video, audio) — the `Attachment` type is media-general, but only images are
  captured/rendered now.
- **Thumbnail generation/caching** — the UI scales the source image for now; a generated-thumbnail
  cache is a later optimisation if large libraries make it necessary.

## Addendum — photo management moved to the add/edit dialog

The first implementation (item 9) put *Add photo* and tap-to-delete inside the **read-only entry-view
dialog**, with the add/edit dialog photo-free. That was backwards: you had to create an entry, reopen
it, and find the affordance two taps deep in a viewer — and you could never attach a photo *while*
writing the entry. Photo management now lives in the **add/edit dialog**, and the view dialog only
*displays* a read-only thumbnail strip.

- All photo changes are **staged and committed on Save** (so Cancel discards them):
  `stagedAdds` (picked file URLs, shown as thumbnails before any row exists), `pendingRemoves`
  (existing attachmentIds tapped for deletion). In add mode there is no entry id until Save.
- A new `AppContext::saveJournalEntry(entryId, kind, note, addPhotoUrls, removeAttachmentIds)` does
  the whole commit in one step — **create-or-update first** (so a brand-new entry exists before any
  photo binds to it), then remove the marked attachments, then attach the staged URLs. `addJournalEntry`
  now **returns the new entry id** (was `void`) to make that ordering possible. The logic stays in
  tested C++; QML only collects the staged URLs/ids. Covered by
  `test_appcontext::saveJournalEntryStagesAndReconcilesPhotos` (add-with-photos + edit add/remove in
  one Save).
