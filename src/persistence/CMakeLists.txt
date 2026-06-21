# SPDX-License-Identifier: GPL-3.0-or-later
# klr_persistence — the repository boundary. The ONLY layer that includes <QtSql>;
# SQL never leaks above it.
qt_add_library(klr_persistence STATIC
    plant.h
    journalentry.h
    attachment.h
    agentconversation.h
    # repository interfaces
    ireadingrepository.h
    iplantrepository.h
    ijournalrepository.h
    iattachmentrepository.h
    isensorrepository.h
    ibindingrepository.h
    isyncstaterepository.h
    iagentrepository.h
    # plant-facing reading attribution (shared by both reading-repo impls)
    readingattribution.h
    readingattribution.cpp
    # in-memory fakes (tests + future in-app cache)
    inmemoryreadingrepository.h
    inmemoryreadingrepository.cpp
    inmemoryplantrepository.h
    inmemoryplantrepository.cpp
    inmemoryjournalrepository.h
    inmemoryjournalrepository.cpp
    inmemoryattachmentrepository.h
    inmemoryattachmentrepository.cpp
    # attachment file bytes (kept separate from the metadata repo — ADR 0024)
    iattachmentfilestore.h
    diskattachmentfilestore.h
    diskattachmentfilestore.cpp
    inmemoryattachmentfilestore.h
    inmemoryattachmentfilestore.cpp
    inmemorysensorrepository.h
    inmemorysensorrepository.cpp
    inmemorybindingrepository.h
    inmemorybindingrepository.cpp
    inmemorycarethresholdrepository.h
    inmemorycarethresholdrepository.cpp
    inmemorysyncstaterepository.h
    inmemorysyncstaterepository.cpp
    inmemoryagentrepository.h
    inmemoryagentrepository.cpp
    # SQLite storage substrate
    storageerror.h
    database.h
    database.cpp
    migrationrunner.h
    migrationrunner.cpp
    schema.h
    schema.cpp
    sqlsupport.h
    sqlsupport.cpp
    sqliteplantrepository.h
    sqliteplantrepository.cpp
    sqlitejournalrepository.h
    sqlitejournalrepository.cpp
    sqliteattachmentrepository.h
    sqliteattachmentrepository.cpp
    sqlitesensorrepository.h
    sqlitesensorrepository.cpp
    sqlitebindingrepository.h
    sqlitebindingrepository.cpp
    sqlitereadingrepository.h
    sqlitereadingrepository.cpp
    icarethresholdrepository.h
    sqlitecarethresholdrepository.h
    sqlitecarethresholdrepository.cpp
    sqlitesyncstaterepository.h
    sqlitesyncstaterepository.cpp
    sqliteagentrepository.h
    sqliteagentrepository.cpp
    # WatchFlower data.db importer
    legacyimporter.h
    legacyimporter.cpp
    # backup/export (ADR 0010): stable enum<->token table for the JSON wire format
    backuptokens.h
    backuptokens.cpp
    # readings CSV export (display units, tidy/long, plant-facing)
    readingscsvexporter.h
    readingscsvexporter.cpp
    # lossless id-preserving JSON backup (canonical units) + restore
    backupserializer.h
    backupserializer.cpp
    backupimporter.h
    backupimporter.cpp
    # clone an existing plant (keeps the shared history; ADR 0005/0006)
    plantduplicator.h
    plantduplicator.cpp
    # delete a registered sensor + its data, guarded on no open binding
    sensordeleter.h
    sensordeleter.cpp
    # plant catalog — read-only bundled reference data (CSV, no SQL). A pure parser
    # feeds the CSV-backed repository behind the same boundary as the SQL repos.
    catalogentry.h
    catalogcsv.h
    catalogcsv.cpp
    catalogthresholds.h
    catalogthresholds.cpp
    icatalogrepository.h
    csvcatalogrepository.h
    csvcatalogrepository.cpp
    # device-local UI preferences — QSettings-backed IKeyValueStore (not SQL,
    # not synced; the interface + in-memory fake live in klr_core).
    qsettingskeyvaluestore.h
    qsettingskeyvaluestore.cpp
)
target_link_libraries(klr_persistence PUBLIC Qt6::Core Qt6::Sql klr_core)
target_include_directories(klr_persistence PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

# The bundled ~3400-plant catalog (legacy CSV). Shipped read-only under :/klr/catalog/
# and parsed at startup — same resource mechanism as the brand fonts in klr_style.
qt_add_resources(klr_persistence "klr_catalog"
    PREFIX "/klr"
    FILES
        catalog/plantdb.csv)
