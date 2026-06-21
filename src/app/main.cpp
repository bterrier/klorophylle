// SPDX-License-Identifier: GPL-3.0-or-later
#include <QtCore/QDir>
#include <QtCore/QLibraryInfo>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimer>
#include <QtDBus/QDBusConnection>
#include <QtGui/QGuiApplication>
#include <QtGui/QIcon>
#include <QtQml/QQmlApplicationEngine>
#include <QtQuickControls2/QQuickStyle>

#include "appcontext.h"
#include "agentviewmodel.h"
#include "alertcontroller.h"
#include "freedesktopnotificationsink.h"
#include "freedesktopsecretstore.h"
#include "isecretstore.h"
#include "iagentrepository.h"
#include "inmemoryagentrepository.h"
#include "inmemorysecretstore.h"
#include "networkwebfetcher.h"
#include "navigationcontroller.h"
#include "blescanner.h"
#include "carethresholdsmodel.h"
#include "catalogsearchmodel.h"
#include "clock.h"
#include "csvcatalogrepository.h"
#include "database.h"
#include "deviceregistry.h"
#include "devicesortfiltermodel.h"
#include "discovereddevicesmodel.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "ijournalrepository.h"
#include "iattachmentrepository.h"
#include "iattachmentfilestore.h"
#include "attachment.h"
#include "diskattachmentfilestore.h"
#include "inmemoryattachmentrepository.h"
#include "inmemoryattachmentfilestore.h"
#include "sqliteattachmentrepository.h"
#include "inmemorybindingrepository.h"
#include "inmemorycarethresholdrepository.h"
#include "inmemoryjournalrepository.h"
#include "inmemoryplantrepository.h"
#include "inmemoryreadingrepository.h"
#include "inmemorysensorrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "backupimporter.h"
#include "backupserializer.h"
#include "legacyimporter.h"
#include "plantduplicator.h"
#include "readingscsvexporter.h"
#include "registeredsensorsmodel.h"
#include "sensordeleter.h"
#include "livereadingsmodel.h"
#include "log.h"
#include "migrationrunner.h"
#include "plantcaremodel.h"
#include "historysynccontroller.h"
#include "inmemorysyncstaterepository.h"
#include "isyncstaterepository.h"
#include "plantjournalmodel.h"
#include "plantlistmodel.h"
#include "sensorstatusmodel.h"
#include "qsettingskeyvaluestore.h"
#include "readingingester.h"
#include "sqlitesyncstaterepository.h"
#include "schema.h"
#include "settingsstore.h"
#include "sqlitebindingrepository.h"
#include "sqlitecarethresholdrepository.h"
#include "sqliteagentrepository.h"
#include "sqlitejournalrepository.h"
#include "sqliteplantrepository.h"
#include "sqlitereadingrepository.h"
#include "sqlitesensorrepository.h"
#include "storageerror.h"

#include <memory>
#include <optional>

using namespace klr;

namespace {

// The writable application data directory (holds the DB and the attachments/ tree), created if
// needed; empty on failure. Resolved once at the composition root and shared by the DB + file store.
QString appDataDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        qCWarning(lcApp) << "could not resolve/create the app data directory";
        return {};
    }
    return dir;
}

// Opens (creating if needed) the application database and brings it to the current
// schema baseline. Returns nullopt on failure, so the app can fall back to
// in-memory (ephemeral) repositories and still run.
std::optional<Database> openAppDatabase(const Clock &clock, const QString &dir)
{
    if (dir.isEmpty())
        return std::nullopt;
    const QString path = QDir(dir).filePath(QStringLiteral("klorophylle.db"));
    try {
        Database db = Database::openFile(path, clock);
        MigrationRunner(db.handle(), baselineMigrations()).migrateTo(kSchemaVersion);
        qCInfo(lcApp) << "database ready at" << path << "(schema" << kSchemaVersion << ")";
        return db;
    } catch (const StorageError &e) {
        qCWarning(lcApp) << "database open/migrate failed:" << e.what();
        return std::nullopt;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    // Prefer the OS-native (portal) file dialog over QtQuick.Dialogs' Material-themed QML
    // fallback. Qt only loads a platform theme that provides native dialogs when one is
    // selected; with no Plasma plugin in some Qt builds, KDE/GNOME desktops otherwise get
    // no theme and fall back to QML. xdg-desktop-portal is the desktop-agnostic backend.
    // Set before QGuiApplication (the theme is created during its construction); never
    // override a user's explicit choice. Must precede the QGuiApplication below.
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME"))
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
#endif

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("Klorophylle"));
    QGuiApplication::setOrganizationName(QStringLiteral("Klorophylle"));
    // Brand window/taskbar icon (the SVG is bundled by klr_style at :/klr/branding/).
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/klr/branding/icon.svg")));
    // Style: RUNTIME selection of our own Klorophylle.Style, Basic as the neutral fallback
    // for the few controls we don't implement (docs/adr/0018). We deliberately use runtime
    // selection (NOT compile-time) so QQuickStyle::setStyle governs implicit/attached
    // controls too — notably the attached `ToolTip` — routing them through our style.
    // Screens still `import Klorophylle.Style` directly, so OUR controls are resolved at
    // compile time regardless; only the fallback/attached chain is runtime-resolved. Must
    // be set before any QML that imports Qt Quick Controls is loaded.
    QQuickStyle::setStyle(QStringLiteral("Klorophylle.Style"));
    QQuickStyle::setFallbackStyle(QStringLiteral("Basic"));

    // ---- Composition root: construct services, inject them. No getInstance(),
    // no setContextProperty.
    SystemClock clock;

    // Device-local UI preferences (units, colour scheme) — QSettings-backed, injected.
    // Not domain data, so deliberately outside the app database (docs/adr/0008).
    QSettingsKeyValueStore preferences;
    SettingsStore settings(&preferences);
    SettingsStore::s_instance = &settings; // consumed by SettingsStore::create()

    // First persistence: open + migrate the app database, then construct the
    // repositories over it. If the DB is unavailable, fall back to in-memory
    // (ephemeral) repositories so the app still runs.
    const QString dataDir = appDataDir();
    std::optional<Database> database = openAppDatabase(clock, dataDir);

    std::unique_ptr<IPlantRepository> plantRepo;
    std::unique_ptr<IJournalRepository> journalRepo;
    std::unique_ptr<IAttachmentRepository> attachmentRepo;
    std::unique_ptr<ISensorRepository> sensorRepo;
    std::unique_ptr<IBindingRepository> bindingRepo;
    std::unique_ptr<IReadingRepository> readingRepo;
    std::unique_ptr<ICareThresholdRepository> thresholdRepo;
    std::unique_ptr<ISyncStateRepository> syncStateRepo;
    std::unique_ptr<IAgentRepository> agentRepo;
    // The attachment FILE store is independent of the DB (files on disk, only metadata in SQL —
    // ADR 0024). When the DB is available we get a disk-backed store rooted at the data dir; in the
    // ephemeral fallback (or when the data dir couldn't be resolved) photos live in RAM for the session.
    std::unique_ptr<IAttachmentFileStore> fileStore;
    if (database) {
        plantRepo = std::make_unique<SqlitePlantRepository>(*database);
        journalRepo = std::make_unique<SqliteJournalRepository>(*database);
        attachmentRepo = std::make_unique<SqliteAttachmentRepository>(*database);
        sensorRepo = std::make_unique<SqliteSensorRepository>(*database);
        bindingRepo = std::make_unique<SqliteBindingRepository>(*database);
        readingRepo = std::make_unique<SqliteReadingRepository>(*database);
        thresholdRepo = std::make_unique<SqliteCareThresholdRepository>(*database);
        syncStateRepo = std::make_unique<SqliteSyncStateRepository>(*database);
        agentRepo = std::make_unique<SqliteAgentRepository>(*database);
        fileStore = std::make_unique<DiskAttachmentFileStore>(dataDir);
    } else {
        plantRepo = std::make_unique<InMemoryPlantRepository>();
        journalRepo = std::make_unique<InMemoryJournalRepository>();
        attachmentRepo = std::make_unique<InMemoryAttachmentRepository>();
        sensorRepo = std::make_unique<InMemorySensorRepository>(clock);
        bindingRepo = std::make_unique<InMemoryBindingRepository>();
        readingRepo = std::make_unique<InMemoryReadingRepository>();
        thresholdRepo = std::make_unique<InMemoryCareThresholdRepository>();
        syncStateRepo = std::make_unique<InMemorySyncStateRepository>();
        agentRepo = std::make_unique<InMemoryAgentRepository>();
        fileStore = std::make_unique<InMemoryAttachmentFileStore>();
    }

    // Orphan-file sweep (ADR 0024 decision 5): delete attachment files with no surviving row (entries
    // cascade-deleted while closed, raw deletes, restores). Runs once now, after migrations; it never
    // deletes a row for a missing file, so a restored-without-files backup keeps its rows.
    {
        QSet<QString> liveRefs;
        for (const Attachment &a : attachmentRepo->all())
            liveRefs.insert(a.fileRef);
        const int swept = fileStore->sweepOrphans(liveRefs);
        if (swept > 0)
            qCInfo(lcApp) << "swept" << swept << "orphan attachment file(s)";
    }

    // The plant catalog is shipped read-only reference data, independent of the app
    // DB — it is available even when the DB fell back to in-memory above. Built before the
    // care models because they derive each plant's ideal ranges from it (data-driven; no
    // seeding — the override table holds only user edits).
    CsvCatalogRepository catalogRepo = loadBundledCatalog();
    qCInfo(lcApp) << "plant catalog loaded:" << catalogRepo.count() << "species";
    CatalogSearchModel catalogModel(catalogRepo);

    // The passive scanner is built first so the plant list can read its per-handle broadcast
    // freshness for the connectivity dot (it never opens a connection — advertisement-first).
    const DeviceRegistry registry = makeBuiltinRegistry();
    BleScanner scanner(registry);

    PlantListModel plantsModel(*plantRepo, sensorRepo.get(), bindingRepo.get(), readingRepo.get(),
                               thresholdRepo.get(), &catalogRepo, &clock, &settings, &scanner);
    PlantJournalModel journalModel(*journalRepo, attachmentRepo.get(), fileStore.get());
    PlantCareModel careModel(*sensorRepo, *bindingRepo, *readingRepo, *thresholdRepo, clock,
                             settings, plantRepo.get(), &catalogRepo);

    // The editable per-plant care thresholds: catalog ideals shown as defaults, with
    // per-plant overrides layered on top.
    CareThresholdsModel thresholdsModel(*thresholdRepo, catalogRepo, *plantRepo, settings);

    DiscoveredDevicesModel devicesModel(scanner, clock);
    // Surface supported sensors above BLE noise: the browse list is supported-first
    // (then strongest signal); the pairing picker shows supported only.
    // The browse list (Sensors screen) also drops already-registered devices — they appear
    // in the "Registered sensors" section instead (no double-listing). The pairing
    // picker keeps them (you may re-attach a known-but-unbound sensor to a plant).
    DeviceSortFilterModel sortedDevices(/*onlySupported*/ false, /*excludeRegistered*/ true,
                                        sensorRepo.get());
    sortedDevices.setSourceModel(&devicesModel);
    DeviceSortFilterModel supportedDevices(/*onlySupported*/ true);
    supportedDevices.setSourceModel(&devicesModel);
    LiveReadingsModel liveModel(scanner, settings);

    // Brings an existing WatchFlower data.db forward (Settings -> Import). Holds refs to
    // the repos + clock, all of which outlive `context`, so it is safe to inject by ptr.
    LegacyImporter legacyImporter(*plantRepo, *sensorRepo, *bindingRepo, *readingRepo,
                                  *journalRepo, clock);

    // Clone a plant (and its shared binding history) — Plant settings -> Duplicate. Holds
    // refs to repos that outlive `context`, so it is safe to inject by ptr.
    PlantDuplicator plantDuplicator(*plantRepo, *journalRepo, *thresholdRepo, *bindingRepo);

    // Data export & backup (Export screen, ADR 0010). Pure helpers over the repos +
    // clock; file IO is at the AppContext edge. All refs outlive `context`.
    ReadingsCsvExporter csvExporter(*plantRepo, *bindingRepo, *readingRepo, *sensorRepo);
    BackupSerializer backupSerializer(*plantRepo, *sensorRepo, *bindingRepo, *readingRepo,
                                      *journalRepo, *thresholdRepo, clock, attachmentRepo.get());
    BackupImporter backupImporter(*plantRepo, *sensorRepo, *bindingRepo, *readingRepo,
                                  *journalRepo, *thresholdRepo, clock, attachmentRepo.get());

    // Always-on, sensor-level ingestion (ADR 0011): persist any REGISTERED sensor's broadcast
    // the whole time the app runs — not just while a plant screen is open. Sensor-keyed; the
    // reading repo fans each sample out to every bound plant. Refs outlive `context`.
    ReadingIngester ingester(*sensorRepo, *readingRepo, clock);

    // Always-on history backfill (ADR 0014): connects on a cadence to download the hours the
    // app was closed + the battery, persisting through the repos. Refs outlive `context`.
    HistorySyncController historySync(scanner, *sensorRepo, *readingRepo, *syncStateRepo, clock,
                                      settings);
    // Now that history sync exists, let the Sensors list show "connected" while a download runs.
    devicesModel.setHistorySync(&historySync);

    // The selected plant's bound-sensor status (the plant-detail Sensors tab): per-sensor
    // liveness, battery (from the reading store), last-seen and GATT-open.
    SensorStatusModel sensorStatusModel(*sensorRepo, *bindingRepo, *readingRepo, clock, &scanner,
                                        &historySync);

    // Every registered sensor (bound + unbound) for the Sensors screen's "Registered sensors"
    // section, and the guarded deleter behind its per-sensor "delete data" action.
    RegisteredSensorsModel registeredSensorsModel(*sensorRepo, *bindingRepo, *readingRepo, clock,
                                                  &scanner, &historySync);
    SensorDeleter sensorDeleter(*sensorRepo, *bindingRepo, *readingRepo);
    // When the registered set changes (pair / import / restore / delete refreshes the model),
    // re-run the browse-list filter so a freshly-registered sensor drops out of the live scan.
    QObject::connect(&registeredSensorsModel, &QAbstractItemModel::modelReset, &sortedDevices,
                     &DeviceSortFilterModel::refilter);

    // Care notifications (ADR 0016): the desktop delivery backend (freedesktop D-Bus —
    // DBus is confined to this executable) + the evaluator AppContext re-runs after each
    // ingest. It notifies once on a care-status transition (soil-moisture-TooLow = "time to
    // water"). Refs outlive `context`.
    FreedesktopNotificationSink notificationSink;
    AlertController alertController(*plantRepo, *bindingRepo, *readingRepo, *thresholdRepo,
                                    &catalogRepo, clock, settings, notificationSink);

    // AI assistant (ADR 0019): the chat view-model drives a karness AgentSession over the
    // domain tools. The provider is built lazily from SettingsStore (Ollama-first; no key needed).
    // API keys live in the freedesktop Secret Service (decision 9); without a session bus / keyring
    // we fall back to in-memory storage (a local endpoint needs no key, so the agent still works).
    std::unique_ptr<ISecretStore> secrets;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    if (QDBusConnection::sessionBus().isConnected())
        secrets = std::make_unique<FreedesktopSecretStore>();
    else
#endif
        secrets = std::make_unique<InMemorySecretStore>();
    // Web lookups (ADR 0023) go through a real QtNetwork fetcher, host-restricted to the
    // allowlist; the agent only uses it when the opt-in setting is on.
    NetworkWebFetcher webFetcher;
    AgentViewModel agentViewModel(*plantRepo, *journalRepo, *bindingRepo, *readingRepo,
                                  *thresholdRepo, clock, settings, *secrets, *agentRepo, webFetcher,
                                  *attachmentRepo, *fileStore);

    AppContext context(&scanner, &sortedDevices, &liveModel, plantRepo.get(),
                       journalRepo.get(), &plantsModel, &journalModel, &careModel,
                       &sensorStatusModel,
                       &catalogRepo, &catalogModel, &supportedDevices, thresholdRepo.get(),
                       &thresholdsModel, &legacyImporter, &plantDuplicator, &csvExporter,
                       &backupSerializer, &backupImporter, &settings, &clock, &ingester,
                       &historySync, sensorRepo.get(), readingRepo.get(), syncStateRepo.get(),
                       &alertController, &registeredSensorsModel, &sensorDeleter, bindingRepo.get(),
                       &agentViewModel, attachmentRepo.get(), fileStore.get());
    AppContext::s_instance = &context; // consumed by AppContext::create()

    // Shell navigation: the typed route stack QML mirrors onto its StackView.
    NavigationController navigation;
    NavigationController::s_instance = &navigation; // consumed by NavigationController::create()

    QQmlApplicationEngine engine;
    // Prepend Qt's own QML import dir so QtQuick.Controls + the Basic fallback resolve to
    // the real (installed) plugin. Qt's QuickControls2 lib embeds a `:/qt-project.org/
    // imports/QtQuick/Controls` qmldir naming a dynamic plugin; from our qrc-resident QML
    // that stub is consulted first and aborts runtime resolution ("plugin
    // qtquickcontrols2plugin not found"). Giving the install path priority fixes it; the
    // path is correct in dev and deployment (QLibraryInfo). See docs/adr/0018.
    engine.addImportPath(QLibraryInfo::path(QLibraryInfo::QmlImportsPath));
    engine.loadFromModule("Klorophylle", "Main");
    if (engine.rootObjects().isEmpty())
        return -1;

    // Start the passive advertisement scan at the composition root, decoupled from any screen
    // (ADR 0011): the ingester persists every registered sensor's broadcast for the app's
    // whole lifetime, so a plant never goes stale while the user is on another screen. The agent
    // runs until process teardown (BleScanner setLowEnergyDiscoveryTimeout(0)).
    scanner.start();
    historySync.start(); // startup-grace + periodic cadence-gated history backfill

    // One app-wide heartbeat re-judges sensor connectivity once a second. Liveness is
    // time-relative (a sensor goes "offline" 60s after its last broadcast with no new
    // event to trigger a refresh), so the models can't rely on data-driven updates alone.
    // Keeping the timer at the composition root (not inside the models) leaves the clock
    // injected: each refreshConnectivity() reads the injected Clock and stays deterministic
    // under test. The recompute is cheap and emits dataChanged only for rows that changed.
    QTimer connectivityTimer;
    connectivityTimer.setInterval(1000);
    QObject::connect(&connectivityTimer, &QTimer::timeout, &plantsModel,
                     &PlantListModel::refreshConnectivity);
    QObject::connect(&connectivityTimer, &QTimer::timeout, &devicesModel,
                     &DiscoveredDevicesModel::refreshConnectivity);
    QObject::connect(&connectivityTimer, &QTimer::timeout, &context,
                     &AppContext::refreshSelectedStatus);
    QObject::connect(&connectivityTimer, &QTimer::timeout, &sensorStatusModel,
                     &SensorStatusModel::refreshConnectivity);
    QObject::connect(&connectivityTimer, &QTimer::timeout, &registeredSensorsModel,
                     &RegisteredSensorsModel::refreshConnectivity);
    connectivityTimer.start();

    return app.exec();
}
