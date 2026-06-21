// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "seriesmodel.h" // owned by value: the per-sensor history chart

#include <QtCore/QAbstractItemModel>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtQml/qqmlregistration.h>

QT_BEGIN_NAMESPACE
class QQmlEngine;
class QJSEngine;
QT_END_NAMESPACE

namespace klr {

class BleScanner;
class DiscoveredDevicesModel;
class LiveReadingsModel;
class ISensorRepository;
class IReadingRepository;
class ISyncStateRepository;
class IBindingRepository;
class RegisteredSensorsModel;
class SensorDeleter;
class IPlantRepository;
class IJournalRepository;
class IAttachmentRepository;
class IAttachmentFileStore;
class ICatalogRepository;
class ICareThresholdRepository;
class PlantListModel;
class PlantJournalModel;
class PlantCareModel;
class SensorStatusModel;
class ReadingIngester;
class HistorySyncController;
class CareThresholdsModel;
class CatalogSearchModel;
class LegacyImporter;
class PlantDuplicator;
class ReadingsCsvExporter;
class BackupSerializer;
class BackupImporter;
class SettingsStore;
class Clock;
class AlertController;
class AgentViewModel;

// The single façade QML reaches — a QML singleton injected from the composition
// root (no setContextProperty; see docs/adr/0002). It uses CONSTRUCTOR INJECTION:
// the only constructor requires the services, so the class is NOT
// default-constructible and QML is forced to obtain the composition-root instance
// through create(). (A default-constructible QML_SINGLETON would be silently
// default-constructed by the engine, yielding an un-wired instance.)
class AppContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QAbstractItemModel *devices READ devices CONSTANT)
    // Discovered devices filtered to supported sensors only (the pairing picker).
    Q_PROPERTY(QAbstractItemModel *supportedDevices READ supportedDevices CONSTANT)
    Q_PROPERTY(QAbstractItemModel *liveReadings READ liveReadings CONSTANT)
    // Plant-first surface: the plant list, the selected plant's journal, and the
    // journal-entry kind labels (so the QML carries no presentation logic).
    Q_PROPERTY(QAbstractItemModel *plants READ plants CONSTANT)
    Q_PROPERTY(QAbstractItemModel *journal READ journal CONSTANT)
    // The global (plant-less) journal — user-wide agent memory + global notes (ADR 0022).
    Q_PROPERTY(QAbstractItemModel *globalJournal READ globalJournal CONSTANT)
    // The selected plant's current readings (one per quantity, aggregated across its
    // bound sensors) + its currently-bound sensors, for the care / multi-sensor UI.
    Q_PROPERTY(QAbstractItemModel *careReadings READ careReadings CONSTANT)
    // Plant catalog: the species-search results the picker binds to.
    Q_PROPERTY(QAbstractItemModel *catalogResults READ catalogResults CONSTANT)
    // The selected plant's editable care thresholds: one row per carable quantity.
    Q_PROPERTY(QAbstractItemModel *careThresholds READ careThresholds CONSTANT)
    Q_PROPERTY(QVariantList boundSensors READ boundSensors NOTIFY careChanged)
    // The selected plant's bound sensors with live status, for the plant-detail Sensors tab.
    Q_PROPERTY(QAbstractItemModel *sensorStatus READ sensorStatus CONSTANT)
    // Every registered sensor (the `sensors` table, bound + unbound) for the Sensors screen's
    // "Registered sensors" section — distinct from the live BLE-scan `devices` list.
    Q_PROPERTY(QAbstractItemModel *registeredSensors READ registeredSensors CONSTANT)
    // The selected plant's history chart for one quantity (filled by loadHistory).
    Q_PROPERTY(QObject *history READ history CONSTANT)
    // The selected registered sensor's history chart for one quantity (filled by
    // loadSensorHistory) — plant-agnostic, no care band. A separate model from `history`.
    Q_PROPERTY(QObject *sensorHistory READ sensorHistory CONSTANT)
    // The selected registered sensor's app id (set by selectRegisteredSensor) — the
    // sensor-detail screen passes it back to loadSensorHistory / removeRegisteredSensor.
    Q_PROPERTY(QString selectedSensorId READ selectedSensorId NOTIFY selectedChanged)
    // Whether the selected registered sensor is currently bound to a plant (an open
    // binding) — the sensor-detail screen disables "delete" while true.
    Q_PROPERTY(bool selectedSensorBound READ selectedSensorBound NOTIFY selectedChanged)
    // All kind labels, in enum order — for RENDERING any entry (includes the agent-authored Memory).
    Q_PROPERTY(QStringList journalKinds READ journalKinds CONSTANT)
    // The kinds a USER may create (Note..Observation) — Memory is excluded (agent-authored).
    Q_PROPERTY(QStringList creatableJournalKinds READ creatableJournalKinds CONSTANT)
    // The kinds a user may create in the GLOBAL journal — only Note (care kinds + Observation are
    // plant-scoped; Memory is agent-authored). ADR 0022.
    Q_PROPERTY(QStringList globalCreatableJournalKinds READ globalCreatableJournalKinds CONSTANT)
    Q_PROPERTY(QString selectedPlantName READ selectedPlantName NOTIFY selectedPlantChanged)
    Q_PROPERTY(QString selectedPlantSpecies READ selectedPlantSpecies NOTIFY selectedPlantChanged)
    // The selected plant's species for display: "Botanical · Common" when the catalog
    // has a common name, else just the botanical key. Empty when no species is set.
    Q_PROPERTY(QString selectedPlantSpeciesDisplay READ selectedPlantSpeciesDisplay NOTIFY selectedPlantChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY selectedChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectedChanged)
    Q_PROPERTY(int selectedRssi READ selectedRssi NOTIFY selectedChanged)
    // Whether the selected device offers a one-shot GATT read (non-broadcast).
    Q_PROPERTY(bool selectedCanRead READ selectedCanRead NOTIFY selectedChanged)
    // A GATT read is in progress.
    Q_PROPERTY(bool reading READ reading NOTIFY readingChanged)
    // A GATT history backfill is in progress.
    Q_PROPERTY(bool historySyncing READ historySyncing NOTIFY historySyncingChanged)
    // The selected sensor's live status, for the sensor-detail page (recomputed on a timer):
    // connectivity (Liveness int), authoritative battery + last-seen text, and whether a GATT
    // connection is open to it right now.
    Q_PROPERTY(int selectedLiveness READ selectedLiveness NOTIFY selectedStatusChanged)
    Q_PROPERTY(QString selectedBatteryText READ selectedBatteryText NOTIFY selectedStatusChanged)
    Q_PROPERTY(QString selectedLastSeenText READ selectedLastSeenText NOTIFY selectedStatusChanged)
    Q_PROPERTY(bool selectedGattOpen READ selectedGattOpen NOTIFY selectedStatusChanged)
    // When this install last completed a GATT history download for the selected sensor (relative
    // text), or empty for a sensor that has never been history-synced.
    Q_PROPERTY(QString selectedLastSyncText READ selectedLastSyncText NOTIFY selectedStatusChanged)
    // Care notifications: a human line for the settings pane — empty unless currently
    // snoozed, then "snoozed until <time>". The master enable binds directly to Settings.
    Q_PROPERTY(QString notificationsSnoozedText READ notificationsSnoozedText NOTIFY notificationsChanged)
    // The AI assistant chat view-model — its own QAbstractListModel + invokables drive
    // the AIInsights screen. Typed (uncreatable QML element) so QML resolves its members.
    Q_PROPERTY(klr::AgentViewModel *agent READ agent CONSTANT)

public:
    AppContext(BleScanner *scanner, QAbstractItemModel *devices,
               LiveReadingsModel *live, IPlantRepository *plantRepo,
               IJournalRepository *journalRepo, PlantListModel *plants,
               PlantJournalModel *journal, PlantCareModel *care = nullptr,
               SensorStatusModel *sensorStatus = nullptr,
               ICatalogRepository *catalogRepo = nullptr,
               CatalogSearchModel *catalog = nullptr,
               QAbstractItemModel *supportedDevices = nullptr,
               ICareThresholdRepository *thresholdRepo = nullptr,
               CareThresholdsModel *thresholdsModel = nullptr,
               LegacyImporter *importer = nullptr,
               PlantDuplicator *duplicator = nullptr,
               ReadingsCsvExporter *csvExporter = nullptr,
               BackupSerializer *backupSerializer = nullptr,
               BackupImporter *backupImporter = nullptr,
               SettingsStore *settings = nullptr, const Clock *clock = nullptr,
               ReadingIngester *ingester = nullptr, HistorySyncController *historySync = nullptr,
               ISensorRepository *sensorRepo = nullptr, IReadingRepository *readingRepo = nullptr,
               ISyncStateRepository *syncStateRepo = nullptr, AlertController *alerts = nullptr,
               RegisteredSensorsModel *registeredSensors = nullptr,
               SensorDeleter *sensorDeleter = nullptr, IBindingRepository *bindingRepo = nullptr,
               AgentViewModel *agent = nullptr,
               IAttachmentRepository *attachmentRepo = nullptr,
               IAttachmentFileStore *fileStore = nullptr, QObject *parent = nullptr);

    QAbstractItemModel *devices() const;
    QAbstractItemModel *supportedDevices() const;
    QAbstractItemModel *liveReadings() const;
    QAbstractItemModel *plants() const;
    QAbstractItemModel *journal() const;
    QAbstractItemModel *globalJournal() const;
    QAbstractItemModel *careReadings() const;
    QAbstractItemModel *sensorStatus() const;
    QAbstractItemModel *registeredSensors() const;
    QAbstractItemModel *catalogResults() const;
    QAbstractItemModel *careThresholds() const;
    QVariantList boundSensors() const;
    QObject *history() const;
    QObject *sensorHistory() { return &m_sensorHistory; }
    AgentViewModel *agent() const;
    QString selectedSensorId() const { return m_selectedSensorId; }
    bool selectedSensorBound() const { return m_selectedSensorBound; }
    QStringList journalKinds() const;
    QStringList creatableJournalKinds() const;
    QStringList globalCreatableJournalKinds() const;
    QString selectedPlantName() const { return m_selectedPlantName; }
    QString selectedPlantSpecies() const { return m_selectedPlantSpecies; }
    QString selectedPlantSpeciesDisplay() const;
    bool scanning() const;
    QString status() const { return m_status; }
    QString selectedName() const { return m_selectedName; }
    QString selectedId() const { return m_selectedId; }
    int selectedRssi() const { return m_selectedRssi; }
    bool selectedCanRead() const { return m_selectedCanRead; }
    bool reading() const;
    bool historySyncing() const;
    int selectedLiveness() const { return m_selectedLiveness; }
    QString selectedBatteryText() const { return m_selectedBatteryText; }
    QString selectedLastSeenText() const { return m_selectedLastSeenText; }
    bool selectedGattOpen() const { return m_selectedGattOpen; }
    QString selectedLastSyncText() const { return m_selectedLastSyncText; }
    QString notificationsSnoozedText() const;
    // Recompute the selected sensor's live status (driven on a timer from the composition
    // root) and emit selectedStatusChanged if anything changed. No-op without a selection.
    Q_INVOKABLE void refreshSelectedStatus();

    Q_INVOKABLE void startScan();
    Q_INVOKABLE void stopScan();
    // Manually download stored history now from every reachable paired sensor, ignoring the
    // cadence. The auto path runs on its own at startup + on a timer.
    Q_INVOKABLE void syncHistoryNow();
    Q_INVOKABLE void selectDevice(const QString &id);
    // Connect to the selected device, read its current values once, disconnect.
    Q_INVOKABLE void readValue();

    // Plant-first actions (plants exist with no sensor; goal #1). `species` is a
    // catalog key (botanical name) chosen from the picker, or empty for none.
    Q_INVOKABLE void addPlant(const QString &name, const QString &species);
    Q_INVOKABLE void removePlant(const QString &plantId);
    // Delete the currently-selected plant (the destructive action on its settings
    // subscreen); no-op if nothing is selected.
    Q_INVOKABLE void removeSelectedPlant();
    // Clone the currently-selected plant (its species, care thresholds, journal and full
    // sensor-binding history, so the duplicate keeps the shared past readings) into a new
    // plant named `newName`, then SELECT the duplicate (so a follow-up sensor reassignment
    // targets it). Returns the new plant's id, or "" if nothing is selected / on failure.
    Q_INVOKABLE QString duplicateSelectedPlant(const QString &newName);
    Q_INVOKABLE void selectPlant(const QString &plantId);
    // Catalog: run a species search (results land in catalogResults), and set
    // the selected plant's species to a catalog key (empty clears it).
    Q_INVOKABLE void searchCatalog(const QString &query);
    Q_INVOKABLE void setSelectedPlantSpecies(const QString &speciesKey);
    // Returns the new entry's id (empty on failure) so the caller can attach staged photos to it.
    Q_INVOKABLE QString addJournalEntry(int kind, const QString &note);
    // Edit an existing entry of the selected plant: keeps its entry-date timestamp, sets editedAt
    // to "now", applies the new kind/note. A user edit moves only the edit date, never the timeline
    // position (ADR 0020).
    Q_INVOKABLE void editJournalEntry(const QString &entryId, int kind, const QString &note);
    // Add/edit an entry and reconcile its photos in one step (the add/edit dialog's Save). entryId
    // empty = create; otherwise edit. Removes each id in removeAttachmentIds, then attaches each
    // file:// URL in addPhotoUrls — so a brand-new entry's photos, staged in the dialog before the
    // entry existed, are attached once it does. Returns the entry id (empty on a failed create).
    Q_INVOKABLE QString saveJournalEntry(const QString &entryId, int kind, const QString &note,
                                         const QStringList &addPhotoUrls,
                                         const QStringList &removeAttachmentIds);
    Q_INVOKABLE void removeJournalEntry(const QString &entryId);
    // Journal photos (ADR 0024). Attach a photo (a file:// URL from the QML picker) to an existing
    // entry of the selected plant, edit a photo's caption, or remove a photo. addPhoto stamps addedAt
    // from the injected clock, copies the file into the store, then writes the row; removePhoto deletes
    // the row then the file. All refresh the journal model so the new thumbnail shows.
    Q_INVOKABLE void addPhotoToEntry(const QString &entryId, const QString &fileUrl,
                                     const QString &caption);
    Q_INVOKABLE void setPhotoCaption(const QString &attachmentId, const QString &caption);
    Q_INVOKABLE void removePhoto(const QString &attachmentId);
    // The photos on a journal entry, as {attachmentId, url, caption} maps (a local file:// url). Lets
    // the entry-view dialog refresh its thumbnails after an add/remove without re-reading the model.
    Q_INVOKABLE QVariantList photosForEntry(const QString &entryId) const;
    // Global-journal writes (ADR 0022): plant-less entries. add creates a Note (the only
    // user-creatable global kind); edit/remove curate any global entry (incl. the agent's Memory),
    // a user edit moving only editedAt (ADR 0020). They refresh the globalJournal model.
    Q_INVOKABLE void addGlobalJournalEntry(int kind, const QString &note);
    Q_INVOKABLE void editGlobalJournalEntry(const QString &entryId, int kind, const QString &note);
    Q_INVOKABLE void removeGlobalJournalEntry(const QString &entryId);
    // Fill `history` with the selected plant's series for `quantity` (a Quantity int).
    Q_INVOKABLE void loadHistory(int quantity);

    // Registered-sensor management. selectRegisteredSensor points the sensor-detail
    // status fields (selectedLiveness/Battery/LastSeen/…) + the delete guard at a known
    // sensor by its app id (it may be offline / not in the live scan). loadSensorHistory
    // fills `sensorHistory` with that sensor's series for `quantity` (plant-agnostic, no
    // band). removeRegisteredSensor deletes the sensor + its data via the SensorDeleter,
    // refusing while it is still bound; reports the outcome via sensorRemoved().
    Q_INVOKABLE void selectRegisteredSensor(const QString &sensorId);
    Q_INVOKABLE void loadSensorHistory(const QString &sensorId, int quantity);
    Q_INVOKABLE void removeRegisteredSensor(const QString &sensorId);
    // The quantities this sensor has stored readings for, as {value:int, label:string}
    // maps in enum order — the sensor-detail history selector's options (labels from C++).
    Q_INVOKABLE QVariantList sensorHistoryQuantities(const QString &sensorId) const;

    // Bring an existing WatchFlower `data.db` forward into this app (the file is
    // read read-only; see LegacyImporter). `fileUrl` is a file:// URL from the QML file
    // picker. Adds plants/sensors/readings (re-running duplicates) and reports the
    // outcome through importFinished(). No-op (failed) without an injected importer.
    Q_INVOKABLE void importLegacyDatabase(const QString &fileUrl);
    // A sensible starting folder (file:// URL) for the import file picker: the legacy
    // WatchFlower data directory if it exists, else the generic data location. Derived
    // from GenericDataLocation, NOT this app's AppDataLocation (different org name).
    Q_INVOKABLE QString legacyImportFolder() const;
    // The standard legacy data.db (file:// URL) if it exists, else empty. Lets the UI offer
    // a one-click import of the detected database — a native file dialog can't be forced to
    // open at a given folder (the portal backend remembers its own last-used location).
    Q_INVOKABLE QString detectedLegacyDatabase() const;

    // Data export & backup (ADR 0010). Each writes a Clock-stamped file under the
    // export folder and returns its path ("" on failure). The CSV is a lossy spreadsheet
    // dump in display units; the backup is a lossless id-preserving JSON snapshot.
    // The CSV window follows the persisted period choice (exportPeriodLabels()).
    Q_INVOKABLE QString exportReadingsCsv();
    Q_INVOKABLE QString exportBackup();
    // CSV export period labels, in order, for the export-screen dropdown. The chosen
    // index persists in SettingsStore::exportPeriodIndex; index 0 is "All data".
    Q_INVOKABLE QStringList exportPeriodLabels() const;
    // Restore a JSON backup. `fileUrl` is a file:// URL from the QML file picker. Upserts
    // by UUID (idempotent) and refreshes the plant list + selected-plant care.
    Q_INVOKABLE void restoreBackup(const QString &fileUrl);
    // Open the export folder in the platform file manager (creating it if needed).
    Q_INVOKABLE void revealExportFolder();
    // The export folder as a local path (DocumentsLocation/Klorophylle).
    Q_INVOKABLE QString exportFolder() const;
    // A starting folder (file:// URL) for the restore file picker: the export folder if it
    // exists, else the Documents location.
    Q_INVOKABLE QString backupImportFolder() const;

    // Care thresholds: override one quantity's ideal range (min/max as display-unit
    // text, empty clears a bound) or reset the whole set to the catalog species' ideals.
    // Both refresh the care status.
    Q_INVOKABLE void editCareThreshold(int quantity, const QString &minText, const QString &maxText);
    Q_INVOKABLE void resetCareThresholds();

    // Bind/unbind a sensor to the selected plant. `deviceId` is a discovered device's
    // platform handle; the sensor is minted/deduped from it. unbind keys on the
    // app-minted sensorId (as shown by boundSensors).
    Q_INVOKABLE void attachSensor(const QString &deviceId);
    Q_INVOKABLE void detachSensor(const QString &sensorId);

    // Care notifications: mute all care notifications for `hours` from now. The deadline
    // is computed from the injected clock and persisted on SettingsStore (survives restart).
    Q_INVOKABLE void snoozeNotifications(int hours);

    // QML obtains the composition-root instance through this factory (set-once
    // global). With no default constructor, QML cannot make its own.
    static AppContext *s_instance;
    static AppContext *create(QQmlEngine *, QJSEngine *);

signals:
    void scanningChanged();
    void statusChanged();
    void selectedChanged();
    void selectedPlantChanged();
    void readingChanged();
    void historySyncingChanged();
    void selectedStatusChanged();
    void careChanged();
    void notificationsChanged();
    // A legacy-import attempt finished: `summary` is a ready-to-show message and `ok`
    // is false when the file could not be imported (the message then carries the error).
    void importFinished(const QString &summary, bool ok);
    // An export finished: `summary` is a ready-to-show message, `ok` is the outcome, and
    // `folderUrl` (file:// URL) is the export folder so the UI can offer "Show in folder".
    void exportFinished(const QString &summary, bool ok, const QString &folderUrl);
    // A restore attempt finished (same convention as importFinished).
    void restoreFinished(const QString &summary, bool ok);
    // A registered-sensor delete finished: `ok` is false when refused (still bound) or on
    // error, with `message` ready to show; the sensor-detail screen pops on success.
    void sensorRemoved(bool ok, const QString &message);

private:
    void setStatus(const QString &s);
    // Write `bytes` to a Clock-stamped <export folder>/klorophylle_<ts>.<ext> file,
    // emitting exportFinished. `what` names the artifact for the message. Returns the
    // path, or "" on failure.
    QString writeExport(const QByteArray &bytes, const QString &ext, const QString &what);

    BleScanner *m_scanner = nullptr;
    QAbstractItemModel *m_devices = nullptr;
    QAbstractItemModel *m_supportedDevices = nullptr;
    LiveReadingsModel *m_live = nullptr;
    IPlantRepository *m_plantRepo = nullptr;
    IJournalRepository *m_journalRepo = nullptr;
    // Journal photo storage (ADR 0024): metadata repo + the file bytes seam, kept apart.
    IAttachmentRepository *m_attachmentRepo = nullptr;
    IAttachmentFileStore *m_fileStore = nullptr;
    ICatalogRepository *m_catalogRepo = nullptr;
    ICareThresholdRepository *m_thresholdRepo = nullptr;
    PlantListModel *m_plants = nullptr;
    PlantJournalModel *m_journal = nullptr;
    PlantJournalModel *m_globalJournal = nullptr; // owned; global-scoped (ADR 0022), created in ctor
    PlantCareModel *m_care = nullptr;
    SensorStatusModel *m_sensorStatus = nullptr;
    RegisteredSensorsModel *m_registeredSensors = nullptr;
    CareThresholdsModel *m_thresholdsModel = nullptr;
    CatalogSearchModel *m_catalog = nullptr;
    LegacyImporter *m_importer = nullptr;
    PlantDuplicator *m_duplicator = nullptr;
    ReadingsCsvExporter *m_csvExporter = nullptr;
    BackupSerializer *m_backupSerializer = nullptr;
    BackupImporter *m_backupImporter = nullptr;
    SettingsStore *m_settings = nullptr;
    const Clock *m_clock = nullptr;
    // The always-on, sensor-level ingestion seam (ADR 0011): persists ANY registered sensor's
    // broadcasts the whole time the app runs, independent of which plant/screen is open.
    ReadingIngester *m_ingester = nullptr;
    // GATT history backfill orchestrator (ADR 0014): owns the cadence + persistence; AppContext
    // drives it ("sync now") and reflects its busy state + refreshes on a completed sync.
    HistorySyncController *m_historySync = nullptr;
    // For the selected sensor's authoritative status: the sensor (by handle) and its latest
    // battery reading live behind the repository boundary, not in the broadcast snapshot.
    ISensorRepository *m_sensorRepo = nullptr;
    IReadingRepository *m_readingRepo = nullptr;
    // Per-sensor history-sync bookkeeping, for the detail page's "last synced" line.
    ISyncStateRepository *m_syncStateRepo = nullptr;
    // Care-notification evaluator (ADR 0016): re-judged after each ingest, notifies on a
    // care-status transition. Owns its delivery sink; AppContext only triggers + snoozes it.
    AlertController *m_alerts = nullptr;
    // Registered-sensor management: the guarded deleter + the binding repo (for the
    // selected sensor's bound flag). The per-sensor history chart is owned by value.
    SensorDeleter *m_sensorDeleter = nullptr;
    IBindingRepository *m_bindingRepo = nullptr;
    AgentViewModel *m_agent = nullptr; // the AI chat view-model
    SeriesModel m_sensorHistory;
    QString m_selectedSensorId;        // the selected registered sensor's app id
    bool m_selectedSensorBound = false; // is it currently bound to a plant?
    QString m_status;
    QString m_selectedName;
    QString m_selectedId;
    int m_selectedRssi = 0;
    bool m_selectedCanRead = false;
    int m_selectedLiveness = -1;
    QString m_selectedBatteryText;
    QString m_selectedLastSeenText;
    bool m_selectedGattOpen = false;
    QString m_selectedLastSyncText;
    QString m_selectedPlantId;
    QString m_selectedPlantName;
    QString m_selectedPlantSpecies;
};

} // namespace klr
