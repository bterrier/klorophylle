// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcontext.h"

#include "agentviewmodel.h"
#include "alertcontroller.h"
#include "backupimporter.h"
#include "backupserializer.h"
#include "blescanner.h"
#include "carestatus.h"
#include "carethresholdsmodel.h"
#include "clock.h"
#include "catalogentry.h"
#include "catalogsearchmodel.h"
#include "catalogthresholds.h"
#include "discovereddevice.h"
#include "discovereddevicesmodel.h"
#include "format.h"
#include "attachment.h"
#include "historysynccontroller.h"
#include "iattachmentfilestore.h"
#include "iattachmentrepository.h"
#include "ibindingrepository.h"
#include "icarethresholdrepository.h"
#include "icatalogrepository.h"
#include "ijournalrepository.h"
#include "iplantrepository.h"
#include "ireadingrepository.h"
#include "isensorrepository.h"
#include "isyncstaterepository.h"
#include "liveness.h"
#include "sensor.h"
#include "journalentry.h"
#include "journalformat.h"
#include "legacyimporter.h"
#include "livereadingsmodel.h"
#include "log.h"
#include "plant.h"
#include "plantcaremodel.h"
#include "plantduplicator.h"
#include "plantjournalmodel.h"
#include "plantlistmodel.h"
#include "readingingester.h"
#include "readingscsvexporter.h"
#include "registeredsensorsmodel.h"
#include "sensordeleter.h"
#include "sensorstatusmodel.h"
#include "settingsstore.h"
#include "storageerror.h"
#include "units.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QLocale>
#include <QtCore/QStandardPaths>
#include <QtCore/QTimeZone>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtGui/QDesktopServices>
#include <QtQml/QQmlEngine>

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>

namespace klr {

namespace {

// Clock-stamped export file name: klorophylle_<yyyyMMdd-HHmmss>.<ext> (UTC, from the
// injected Clock — no wall-clock reads, so it is deterministic under test).
QString stampedName(qint64 nowMs, const QString &ext)
{
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(nowMs, QTimeZone::UTC);
    return QStringLiteral("klorophylle_%1.%2")
        .arg(dt.toString(QStringLiteral("yyyyMMdd-HHmmss")), ext);
}

// The CSV export window options, in dropdown order. `days <= 0` means "all data".
// Single source of truth: SettingsStore persists only the index into this table, and
// SettingsStore::kExportPeriodCount must match its size.
struct ExportPeriod { const char *label; int days; };
constexpr ExportPeriod kExportPeriods[] = {
    { QT_TR_NOOP("All data"),      0 },
    { QT_TR_NOOP("Last 24 hours"), 1 },
    { QT_TR_NOOP("Last 7 days"),   7 },
    { QT_TR_NOOP("Last 30 days"),  30 },
    { QT_TR_NOOP("Last 90 days"),  90 },
};

} // namespace

AppContext *AppContext::s_instance = nullptr;

AppContext::AppContext(BleScanner *scanner, QAbstractItemModel *devices,
                       LiveReadingsModel *live, IPlantRepository *plantRepo,
                       IJournalRepository *journalRepo, PlantListModel *plants,
                       PlantJournalModel *journal, PlantCareModel *care,
                       SensorStatusModel *sensorStatus,
                       ICatalogRepository *catalogRepo, CatalogSearchModel *catalog,
                       QAbstractItemModel *supportedDevices,
                       ICareThresholdRepository *thresholdRepo,
                       CareThresholdsModel *thresholdsModel, LegacyImporter *importer,
                       PlantDuplicator *duplicator, ReadingsCsvExporter *csvExporter,
                       BackupSerializer *backupSerializer, BackupImporter *backupImporter,
                       SettingsStore *settings, const Clock *clock, ReadingIngester *ingester,
                       HistorySyncController *historySync, ISensorRepository *sensorRepo,
                       IReadingRepository *readingRepo, ISyncStateRepository *syncStateRepo,
                       AlertController *alerts, RegisteredSensorsModel *registeredSensors,
                       SensorDeleter *sensorDeleter, IBindingRepository *bindingRepo,
                       AgentViewModel *agent, IAttachmentRepository *attachmentRepo,
                       IAttachmentFileStore *fileStore, QObject *parent)
    : QObject(parent)
    , m_scanner(scanner)
    , m_devices(devices)
    , m_supportedDevices(supportedDevices)
    , m_live(live)
    , m_plantRepo(plantRepo)
    , m_journalRepo(journalRepo)
    , m_attachmentRepo(attachmentRepo)
    , m_fileStore(fileStore)
    , m_catalogRepo(catalogRepo)
    , m_thresholdRepo(thresholdRepo)
    , m_plants(plants)
    , m_journal(journal)
    , m_care(care)
    , m_sensorStatus(sensorStatus)
    , m_registeredSensors(registeredSensors)
    , m_thresholdsModel(thresholdsModel)
    , m_catalog(catalog)
    , m_importer(importer)
    , m_duplicator(duplicator)
    , m_csvExporter(csvExporter)
    , m_backupSerializer(backupSerializer)
    , m_backupImporter(backupImporter)
    , m_settings(settings)
    , m_clock(clock)
    , m_ingester(ingester)
    , m_historySync(historySync)
    , m_sensorRepo(sensorRepo)
    , m_readingRepo(readingRepo)
    , m_syncStateRepo(syncStateRepo)
    , m_alerts(alerts)
    , m_sensorDeleter(sensorDeleter)
    , m_bindingRepo(bindingRepo)
    , m_agent(agent)
{
    // The global journal (ADR 0022) is derived from the injected repository — a second journal
    // model scoped to plant-less entries, owned here (no extra composition-root wiring needed).
    if (m_journalRepo) {
        // The global journal carries no photos (memory + notes), so no attachment wiring.
        m_globalJournal = new PlantJournalModel(*m_journalRepo, nullptr, nullptr, this);
        m_globalJournal->setGlobal();
    }

    // The snoozed-text line follows the persisted notification prefs (enable/snooze).
    if (m_settings)
        connect(m_settings, &SettingsStore::notificationsChanged, this,
                &AppContext::notificationsChanged);
    // History backfill: reflect its busy state and refresh the live UI after a completed sync.
    if (m_historySync) {
        connect(m_historySync, &HistorySyncController::busyChanged, this,
                &AppContext::historySyncingChanged);
        connect(m_historySync, &HistorySyncController::changed, this, [this] {
            if (m_plants)
                m_plants->refresh();
            if (m_care && m_care->hasPlant())
                m_care->refresh();
            if (m_sensorStatus)
                m_sensorStatus->refresh(); // a sync may have refreshed a sensor's battery
            if (m_registeredSensors)
                m_registeredSensors->refresh(); // ditto for the registered-sensors list
            Q_EMIT careChanged();
        });
    }
    qCDebug(lcApp) << "AppContext ctor scanner=" << static_cast<void *>(m_scanner);

    // A threshold edit refreshes the care aggregate so the per-reading status updates.
    if (m_thresholdsModel) {
        connect(m_thresholdsModel, &CareThresholdsModel::changed, this, [this] {
            if (m_care)
                m_care->refresh();
            if (m_plants)
                m_plants->refresh(); // the at-a-glance health pill reflects the new ranges
            Q_EMIT careChanged();
        });
    }

    if (!m_scanner)
        return; // no BLE wired (e.g. a logic test exercising only the plant flow)

    connect(m_scanner, &BleScanner::scanningChanged, this, &AppContext::scanningChanged);
    connect(m_scanner, &BleScanner::errorOccurred, this,
            [this](const QString &msg) { setStatus(msg); });
    connect(m_scanner, &BleScanner::deviceChanged, this, [this](const QString &id) {
        const DiscoveredDevice *d = m_scanner->device(id);
        // Always-on, sensor-level ingestion (ADR 0011): persist any REGISTERED sensor's
        // broadcast regardless of which plant/screen is open — the readings table is
        // sensor-keyed and fans out to every bound plant. The desktop handle is a MAC
        // (matching how pairing mints the sensor). On a real write, refresh the live UI:
        // the plant-list health badges and, if its plant is open, the care aggregate.
        if (d && m_ingester) {
            const std::optional<SensorId> wrote = m_ingester->ingest(HandleKind::Mac, id, d->latest);
            if (wrote) {
                if (m_plants)
                    m_plants->refresh();
                if (m_care && m_care->hasPlant())
                    m_care->refresh();
                if (m_sensorStatus)
                    m_sensorStatus->refresh(); // a broadcast may have updated battery
                if (m_registeredSensors)
                    m_registeredSensors->refresh(); // a first-seen sensor may be newly registered
                // Fresh data can cross a care threshold — notify on the transition. The
                // evaluator debounces, so a plant that stays out of range is announced once.
                if (m_alerts)
                    m_alerts->evaluate();
                Q_EMIT careChanged();
            }
        }
        if (id != m_selectedId)
            return;
        if (d) {
            m_selectedRssi = d->rssi;
            if (!d->name.isEmpty())
                m_selectedName = d->name;
            Q_EMIT selectedChanged();
        }
    });
    connect(m_scanner, &BleScanner::readingChanged, this, &AppContext::readingChanged);
    connect(m_scanner, &BleScanner::readingChanged, this, [this](bool reading) {
        if (reading)
            setStatus(tr("Reading…"));
        else if (status() == tr("Reading…"))
            setStatus({});
    });
    connect(m_scanner, &BleScanner::readFailed, this,
            [this](const QString &, const QString &msg) { setStatus(msg); });
}

AppContext *AppContext::create(QQmlEngine *, QJSEngine *)
{
    qCDebug(lcApp) << "AppContext::create s_instance=" << static_cast<void *>(s_instance);
    Q_ASSERT_X(s_instance, "AppContext::create",
               "composition root must set AppContext::s_instance before loading QML");
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

QAbstractItemModel *AppContext::devices() const { return m_devices; }
QAbstractItemModel *AppContext::supportedDevices() const { return m_supportedDevices; }
QAbstractItemModel *AppContext::liveReadings() const { return m_live; }
QAbstractItemModel *AppContext::plants() const { return m_plants; }
QAbstractItemModel *AppContext::journal() const { return m_journal; }
QAbstractItemModel *AppContext::globalJournal() const { return m_globalJournal; }
QAbstractItemModel *AppContext::careReadings() const { return m_care; }
QAbstractItemModel *AppContext::sensorStatus() const { return m_sensorStatus; }
QAbstractItemModel *AppContext::registeredSensors() const { return m_registeredSensors; }
QAbstractItemModel *AppContext::catalogResults() const { return m_catalog; }
QAbstractItemModel *AppContext::careThresholds() const { return m_thresholdsModel; }
QVariantList AppContext::boundSensors() const { return m_care ? m_care->boundSensors() : QVariantList{}; }
QObject *AppContext::history() const { return m_care ? m_care->history() : nullptr; }
AgentViewModel *AppContext::agent() const { return m_agent; }
QStringList AppContext::journalKinds() const { return journalKindLabels(); }

QStringList AppContext::creatableJournalKinds() const
{
    // The user-creatable kinds are the contiguous [Note..Observation] prefix; Memory is
    // agent-authored only, so it is omitted here (the add-entry picker feeds on this list). Index
    // into this list maps 1:1 onto the JournalEntryKind value, which the dialog passes as `kind`.
    QStringList kinds;
    for (int k = int(JournalEntryKind::Note); k <= int(JournalEntryKind::Observation); ++k)
        kinds.append(journalKindLabel(static_cast<JournalEntryKind>(k)));
    return kinds;
}

QStringList AppContext::globalCreatableJournalKinds() const
{
    // The global journal is plant-less, so the care kinds (Watering..Pruning) and Observation —
    // all plant-scoped — don't apply, and Memory is agent-authored: a user-created global entry is
    // a Note. Index 0 maps to JournalEntryKind::Note, which the dialog passes as `kind`.
    return { journalKindLabel(JournalEntryKind::Note) };
}

void AppContext::loadHistory(int quantity)
{
    if (m_care)
        m_care->loadHistory(static_cast<Quantity>(quantity));
}

void AppContext::selectRegisteredSensor(const QString &sensorId)
{
    m_selectedSensorId = sensorId;
    const SensorId sid{ QUuid::fromString(sensorId) };
    const std::optional<Sensor> s = m_sensorRepo ? m_sensorRepo->get(sid) : std::nullopt;

    // Drive the shared selected* status fields off the sensor's hardware handle, so the
    // existing refreshSelectedStatus() machinery (liveness/battery/last-seen/last-sync)
    // works for a registered sensor that may be offline / not in the live scan.
    m_selectedId = s ? s->handleValue : QString();
    m_selectedName = s ? (s->model.isEmpty() ? s->handleValue : s->model) : QString();
    const DiscoveredDevice *d =
        (m_scanner && !m_selectedId.isEmpty()) ? m_scanner->device(m_selectedId) : nullptr;
    m_selectedRssi = d ? d->rssi : 0;
    m_selectedCanRead = d && d->canRead;

    // Referenced by a plant? ANY binding (open or closed) ties this sensor's data to a
    // plant's history, so the detail screen disables delete while true (see SensorDeleter).
    m_selectedSensorBound = m_bindingRepo && !m_bindingRepo->bindingsForSensor(sid).isEmpty();
    Q_EMIT selectedChanged();
    refreshSelectedStatus(); // populate the detail page's status immediately
}

void AppContext::loadSensorHistory(const QString &sensorId, int quantity)
{
    const Quantity q = static_cast<Quantity>(quantity);
    if (!m_readingRepo) {
        m_sensorHistory.clear();
        return;
    }
    const SensorId sid{ QUuid::fromString(sensorId) };
    const QDateTime to = m_clock
        ? QDateTime::fromMSecsSinceEpoch(m_clock->nowMs(), QTimeZone::UTC)
        : QDateTime::currentDateTimeUtc();
    const QDateTime from = to.addDays(-3650); // effectively all of this sensor's history

    // Sensor-keyed series (plant-agnostic): no care band — thresholds live on a plant, and a
    // sensor may belong to several (or none). Convert canonical samples to the display unit
    // so the QtGraphs axis renders in it (storage stays canonical).
    QList<Reading> series = m_readingRepo->history(sid, q, from, to);
    const Unit disp = displayUnit(q, m_settings ? m_settings->displayUnits() : DisplayUnits{});
    for (Reading &r : series) {
        if (r.value) {
            r.value = convert(*r.value, r.unit, disp);
            r.unit = disp;
        }
    }
    m_sensorHistory.setReadings(series, std::nullopt);
}

QVariantList AppContext::sensorHistoryQuantities(const QString &sensorId) const
{
    QVariantList out;
    if (!m_readingRepo)
        return out;
    const SensorId sid{ QUuid::fromString(sensorId) };
    // Dli is derived (never stored), so it is the exclusive upper bound — every measured
    // quantity sits below it. Offer only the ones this sensor has actually recorded.
    for (int i = 0; i < static_cast<int>(Quantity::Dli); ++i) {
        const Quantity q = static_cast<Quantity>(i);
        if (m_readingRepo->latest(sid, q).has_value())
            out.append(QVariantMap{ { QStringLiteral("value"), i },
                                    { QStringLiteral("label"), label(q) } });
    }
    return out;
}

void AppContext::removeRegisteredSensor(const QString &sensorId)
{
    if (!m_sensorDeleter) {
        Q_EMIT sensorRemoved(false, tr("Deleting sensors is unavailable."));
        return;
    }
    const SensorId sid{ QUuid::fromString(sensorId) };
    const std::expected<void, SensorDeleteError> r = m_sensorDeleter->remove(sid);
    if (!r) {
        const QString msg = r.error() == SensorDeleteError::StillBound
            ? tr("This sensor's readings belong to a plant's history. It can be deleted only "
                 "once no plant uses it.")
            : tr("That sensor no longer exists.");
        setStatus(msg);
        Q_EMIT sensorRemoved(false, msg);
        return;
    }

    // Gone: refresh the registered-sensors list + the plant views (a now-removed sensor's
    // closed bindings disappeared, so any plant that once used it loses that history slice).
    if (sensorId == m_selectedSensorId)
        m_selectedSensorId.clear();
    if (m_registeredSensors)
        m_registeredSensors->refresh();
    if (m_plants)
        m_plants->refresh();
    if (m_care && m_care->hasPlant()) {
        m_care->refresh();
        Q_EMIT careChanged();
    }
    if (m_sensorStatus)
        m_sensorStatus->refresh();
    Q_EMIT sensorRemoved(true, tr("Sensor deleted."));
}

void AppContext::importLegacyDatabase(const QString &fileUrl)
{
    if (!m_importer) {
        const QString msg = tr("Import is unavailable.");
        setStatus(msg);
        Q_EMIT importFinished(msg, false);
        return;
    }

    // The QML FileDialog hands back a file:// URL; the importer wants a local path.
    const QString path = QUrl(fileUrl).isLocalFile() ? QUrl(fileUrl).toLocalFile() : fileUrl;
    if (path.isEmpty()) {
        const QString msg = tr("No file selected.");
        setStatus(msg);
        Q_EMIT importFinished(msg, false);
        return;
    }

    try {
        const LegacyImporter::Result r = m_importer->importFrom(path);
        const QString summary = tr("Imported %n plant(s), ", nullptr, r.plants)
                + tr("%n sensor(s) and ", nullptr, r.sensors)
                + tr("%n reading(s).", nullptr, r.readings);
        // No threshold seeding needed: care judgment derives each plant's ranges from its
        // species + the catalog live, so imported plants show health badges immediately.
        // Refresh the list (and the selected plant's care, if any) to reflect the new data.
        if (m_plants)
            m_plants->refresh();
        if (!m_selectedPlantId.isEmpty() && m_care) {
            m_care->refresh();
            Q_EMIT careChanged();
        }
        if (m_registeredSensors)
            m_registeredSensors->refresh(); // imported sensors are now registered
        setStatus(summary);
        qCInfo(lcApp) << "legacy import finished:" << summary;
        Q_EMIT importFinished(summary, true);
    } catch (const StorageError &e) {
        const QString msg = tr("Import failed: %1").arg(QString::fromUtf8(e.what()));
        setStatus(msg);
        Q_EMIT importFinished(msg, false);
    }
}

QString AppContext::legacyImportFolder() const
{
    // WatchFlower stored data.db under <GenericDataLocation>/WatchFlower/WatchFlower
    // (org + app both "WatchFlower"). Point the picker there when it exists, else fall back
    // to the generic data root so the dialog still opens somewhere sensible.
    const QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString legacy = QDir(generic).filePath(QStringLiteral("WatchFlower/WatchFlower"));
    const QString folder = QDir(legacy).exists() ? legacy : generic;
    return QUrl::fromLocalFile(folder).toString();
}

QString AppContext::detectedLegacyDatabase() const
{
    const QString generic = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString db = QDir(generic).filePath(QStringLiteral("WatchFlower/WatchFlower/data.db"));
    return QFileInfo::exists(db) ? QUrl::fromLocalFile(db).toString() : QString();
}

QString AppContext::exportFolder() const
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(QStringLiteral("Klorophylle"));
}

QString AppContext::backupImportFolder() const
{
    const QString folder = exportFolder();
    const QString start = QDir(folder).exists()
        ? folder
        : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(start).toString();
}

QString AppContext::writeExport(const QByteArray &bytes, const QString &ext, const QString &what)
{
    if (!m_clock) { // no Clock injected => cannot stamp a deterministic name
        const QString msg = tr("Export is unavailable.");
        setStatus(msg);
        Q_EMIT exportFinished(msg, false, QString());
        return {};
    }
    const QString folder = exportFolder();
    if (!QDir().mkpath(folder)) {
        const QString msg = tr("Could not create the export folder.");
        setStatus(msg);
        Q_EMIT exportFinished(msg, false, QString());
        return {};
    }
    const QString path = QDir(folder).filePath(stampedName(m_clock->nowMs(), ext));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString msg = tr("Could not write the %1 file: %2").arg(what, file.errorString());
        setStatus(msg);
        Q_EMIT exportFinished(msg, false, QString());
        return {};
    }
    file.write(bytes);
    file.close();

    const QString folderUrl = QUrl::fromLocalFile(folder).toString();
    const QString summary = tr("Exported %1 to %2").arg(what, QDir::toNativeSeparators(path));
    setStatus(summary);
    qCInfo(lcApp) << "export finished:" << summary;
    Q_EMIT exportFinished(summary, true, folderUrl);
    return path;
}

QString AppContext::exportReadingsCsv()
{
    if (!m_csvExporter) {
        const QString msg = tr("Export is unavailable.");
        setStatus(msg);
        Q_EMIT exportFinished(msg, false, QString());
        return {};
    }
    const DisplayUnits units = m_settings ? m_settings->displayUnits() : DisplayUnits{};

    // The window is driven by the persisted period choice (see exportPeriodLabels()).
    int idx = m_settings ? m_settings->exportPeriodIndex() : 0;
    if (idx < 0 || idx >= static_cast<int>(std::size(kExportPeriods)))
        idx = 0;
    const int days = kExportPeriods[idx].days;

    // `to` stays open-ended; only the lower bound moves. "All data" (days <= 0) or a
    // missing clock falls back to the epoch start.
    const QDateTime to(QDate(9999, 12, 31), QTime(23, 59, 59), QTimeZone::UTC);
    QDateTime from(QDate(1970, 1, 1), QTime(0, 0), QTimeZone::UTC);
    if (days > 0 && m_clock)
        from = QDateTime::fromMSecsSinceEpoch(m_clock->nowMs(), QTimeZone::UTC).addDays(-days);

    const QString csv = m_csvExporter->exportCsv(units, from, to);
    return writeExport(csv.toUtf8(), QStringLiteral("csv"), tr("readings"));
}

QStringList AppContext::exportPeriodLabels() const
{
    QStringList labels;
    labels.reserve(static_cast<int>(std::size(kExportPeriods)));
    for (const ExportPeriod &p : kExportPeriods)
        labels.append(tr(p.label));
    return labels;
}

QString AppContext::exportBackup()
{
    if (!m_backupSerializer) {
        const QString msg = tr("Backup is unavailable.");
        setStatus(msg);
        Q_EMIT exportFinished(msg, false, QString());
        return {};
    }
    return writeExport(m_backupSerializer->toJson(), QStringLiteral("json"), tr("backup"));
}

void AppContext::revealExportFolder()
{
    const QString folder = exportFolder();
    QDir().mkpath(folder);
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}

void AppContext::restoreBackup(const QString &fileUrl)
{
    if (!m_backupImporter) {
        const QString msg = tr("Restore is unavailable.");
        setStatus(msg);
        Q_EMIT restoreFinished(msg, false);
        return;
    }
    const QString path = QUrl(fileUrl).isLocalFile() ? QUrl(fileUrl).toLocalFile() : fileUrl;
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly)) {
        const QString msg = tr("Could not open the backup file.");
        setStatus(msg);
        Q_EMIT restoreFinished(msg, false);
        return;
    }
    const QByteArray bytes = file.readAll();
    file.close();

    try {
        const BackupImporter::Result r = m_backupImporter->importFrom(bytes);
        QString summary = tr("Restored %n plant(s), ", nullptr, r.plants)
                + tr("%n sensor(s) and ", nullptr, r.sensors)
                + tr("%n reading(s).", nullptr, r.readings);
        if (!r.warnings.isEmpty())
            summary += QLatin1Char(' ') + tr("%n item(s) skipped.", nullptr, r.warnings.size());
        // Refresh the list (and the selected plant's care, if any), as legacy import does.
        if (m_plants)
            m_plants->refresh();
        if (!m_selectedPlantId.isEmpty() && m_care) {
            m_care->refresh();
            Q_EMIT careChanged();
        }
        if (m_registeredSensors)
            m_registeredSensors->refresh(); // restored sensors are now registered
        setStatus(summary);
        qCInfo(lcApp) << "restore finished:" << summary;
        Q_EMIT restoreFinished(summary, true);
    } catch (const StorageError &e) {
        const QString msg = tr("Restore failed: %1").arg(QString::fromUtf8(e.what()));
        setStatus(msg);
        Q_EMIT restoreFinished(msg, false);
    }
}

void AppContext::editCareThreshold(int quantity, const QString &minText, const QString &maxText)
{
    if (m_thresholdsModel)
        m_thresholdsModel->setRange(quantity, minText, maxText); // changed() refreshes care
}

void AppContext::resetCareThresholds()
{
    if (m_thresholdsModel)
        m_thresholdsModel->resetToSpecies();
}
bool AppContext::scanning() const { return m_scanner && m_scanner->isScanning(); }
bool AppContext::reading() const { return m_scanner && m_scanner->isReading(); }

bool AppContext::historySyncing() const { return m_historySync && m_historySync->isBusy(); }

void AppContext::refreshSelectedStatus()
{
    int liveness = -1;
    QString battery;
    QString lastSeen;
    QString lastSync;
    bool gattOpen = false;

    if (!m_selectedId.isEmpty()) {
        const qint64 nowMs = m_clock ? m_clock->nowMs() : 0;
        const DiscoveredDevice *d = m_scanner ? m_scanner->device(m_selectedId) : nullptr;
        std::optional<qint64> lastBroadcast;
        std::optional<qint64> lastValue;
        if (d) {
            if (d->lastSeen.isValid()) {
                lastBroadcast = d->lastSeen.toMSecsSinceEpoch();
                lastSeen = formatAgo(d->lastSeen, nowMs);
            }
            for (const Reading &r : d->latest) {
                if (!r.value || !r.timestamp.isValid())
                    continue;
                const qint64 t = r.timestamp.toMSecsSinceEpoch();
                lastValue = lastValue ? std::max(*lastValue, t) : t;
            }
        }
        if (m_clock)
            liveness = int(livenessOf(lastBroadcast, lastValue, nowMs));

        // Authoritative battery: the registered sensor's latest Battery reading (history sync
        // fills it for devices that never broadcast it), falling back to a broadcast sample.
        if (m_sensorRepo) {
            if (const std::optional<Sensor> s =
                    m_sensorRepo->findByHandle(HandleKind::Mac, m_selectedId)) {
                if (m_readingRepo) {
                    if (const std::optional<Reading> b =
                            m_readingRepo->latest(s->id, Quantity::Battery);
                        b && b->value)
                        battery = formatValue(*b);
                }
                // When this install last completed a history download for the sensor.
                if (m_syncStateRepo) {
                    if (const std::optional<QDateTime> when = m_syncStateRepo->lastHistorySync(s->id);
                        when && when->isValid())
                        lastSync = formatAgo(*when, nowMs);
                }
            }
        }
        if (battery.isEmpty() && d) {
            for (const Reading &r : d->latest)
                if (r.quantity == Quantity::Battery && r.value) {
                    battery = formatValue(r);
                    break;
                }
        }

        gattOpen = (m_scanner && m_selectedId == m_scanner->currentGattId())
                || (m_historySync && m_selectedId == m_historySync->activeId());
        // A connection is open to this sensor (it's off the air meanwhile): show "connected"
        // (blue) instead of letting the dot fall to offline.
        if (gattOpen)
            liveness = kConnectivityConnected;
    }

    if (liveness == m_selectedLiveness && battery == m_selectedBatteryText
        && lastSeen == m_selectedLastSeenText && gattOpen == m_selectedGattOpen
        && lastSync == m_selectedLastSyncText)
        return;
    m_selectedLiveness = liveness;
    m_selectedBatteryText = battery;
    m_selectedLastSeenText = lastSeen;
    m_selectedGattOpen = gattOpen;
    m_selectedLastSyncText = lastSync;
    Q_EMIT selectedStatusChanged();
}

void AppContext::syncHistoryNow()
{
    if (m_historySync)
        m_historySync->syncNow();
}

void AppContext::setStatus(const QString &s)
{
    if (m_status == s)
        return;
    m_status = s;
    Q_EMIT statusChanged();
}

void AppContext::startScan()
{
    qCDebug(lcApp) << "AppContext::startScan scanner=" << static_cast<void *>(m_scanner);
    setStatus({});
    if (m_scanner)
        m_scanner->start();
}

void AppContext::stopScan()
{
    if (m_scanner)
        m_scanner->stop();
}

void AppContext::selectDevice(const QString &id)
{
    if (m_live)
        m_live->setDeviceId(id);
    m_selectedId = id;
    const DiscoveredDevice *d = m_scanner ? m_scanner->device(id) : nullptr;
    m_selectedName = (d && !d->name.isEmpty()) ? d->name : id;
    m_selectedRssi = d ? d->rssi : 0;
    m_selectedCanRead = d && d->canRead;
    Q_EMIT selectedChanged();
    refreshSelectedStatus(); // populate the detail page's status immediately, not on the next tick
}

void AppContext::readValue()
{
    qCDebug(lcApp) << "AppContext::readValue id=" << m_selectedId;
    setStatus({});
    if (m_scanner && !m_selectedId.isEmpty())
        m_scanner->readValue(m_selectedId);
}

void AppContext::addPlant(const QString &name, const QString &species)
{
    if (!m_plantRepo)
        return;
    Plant p;
    p.id = PlantId::generate();
    p.displayName = name.trimmed();
    p.species = species.trimmed();
    p.trackedSince = QDateTime::currentDateTimeUtc();
    try {
        m_plantRepo->add(p);
        // No threshold seeding: care judgment derives the ranges from the species + catalog
        // live (PlantListModel/PlantCareModel), so the new plant is judged immediately.
        if (m_plants)
            m_plants->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not save plant: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::removePlant(const QString &plantId)
{
    if (!m_plantRepo)
        return;
    const PlantId id{ QUuid::fromString(plantId) };
    try {
        m_plantRepo->remove(id);
        if (m_plants)
            m_plants->refresh();
        if (m_selectedPlantId == plantId) {
            m_selectedPlantId.clear();
            m_selectedPlantName.clear();
            m_selectedPlantSpecies.clear();
            if (m_journal)
                m_journal->clearPlant();
            if (m_care) {
                m_care->setPlant(std::nullopt);
                Q_EMIT careChanged();
            }
            if (m_sensorStatus)
                m_sensorStatus->setPlant(std::nullopt);
            if (m_thresholdsModel)
                m_thresholdsModel->setPlant(std::nullopt);
            Q_EMIT selectedPlantChanged();
        }
    } catch (const StorageError &e) {
        setStatus(tr("Could not delete plant: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::removeSelectedPlant()
{
    if (!m_selectedPlantId.isEmpty())
        removePlant(m_selectedPlantId); // clears the selection as part of the removal
}

QString AppContext::duplicateSelectedPlant(const QString &newName)
{
    if (!m_duplicator || m_selectedPlantId.isEmpty())
        return {};
    const PlantId source{ QUuid::fromString(m_selectedPlantId) };
    try {
        const PlantId copy = m_duplicator->duplicate(source, newName);
        if (m_plants)
            m_plants->refresh();
        // Select the duplicate so an optional follow-up sensor reassignment (detach the
        // shared sensor / pair a new one) targets it through the existing care path.
        const QString copyId = copy.toString();
        selectPlant(copyId);
        return copyId;
    } catch (const StorageError &e) {
        setStatus(tr("Could not duplicate plant: %1").arg(QString::fromUtf8(e.what())));
        return {};
    }
}

void AppContext::selectPlant(const QString &plantId)
{
    m_selectedPlantId = plantId;
    const PlantId id{ QUuid::fromString(plantId) };
    const std::optional<Plant> p = m_plantRepo ? m_plantRepo->get(id) : std::nullopt;
    m_selectedPlantName = p ? p->displayName : QString();
    m_selectedPlantSpecies = p ? p->species : QString();

    // No seeding on open: the care/threshold models derive each plant's ranges from its
    // species + the catalog (overlaid with any saved overrides) when they refresh below.
    if (m_journal)
        m_journal->setPlant(id);
    if (m_care) {
        m_care->setPlant(id);
        Q_EMIT careChanged();
    }
    if (m_sensorStatus)
        m_sensorStatus->setPlant(id);
    if (m_thresholdsModel)
        m_thresholdsModel->setPlant(id);
    Q_EMIT selectedPlantChanged();
}

QString AppContext::selectedPlantSpeciesDisplay() const
{
    if (m_selectedPlantSpecies.isEmpty())
        return {};
    if (m_catalogRepo) {
        const std::optional<CatalogEntry> e = m_catalogRepo->byKey(m_selectedPlantSpecies);
        if (e && !e->commonName.isEmpty())
            return QStringLiteral("%1 · %2").arg(m_selectedPlantSpecies, e->commonName);
    }
    return m_selectedPlantSpecies; // botanical key only (or no catalog wired)
}

void AppContext::searchCatalog(const QString &query)
{
    if (m_catalog)
        m_catalog->setQuery(query);
}

void AppContext::setSelectedPlantSpecies(const QString &speciesKey)
{
    if (!m_plantRepo || m_selectedPlantId.isEmpty())
        return;
    const PlantId id{ QUuid::fromString(m_selectedPlantId) };
    std::optional<Plant> p = m_plantRepo->get(id);
    if (!p)
        return;
    p->species = speciesKey.trimmed();
    try {
        m_plantRepo->update(*p);
        m_selectedPlantSpecies = p->species;
        // Picking a species is a deliberate "use this species' ranges" act: drop any
        // overrides left from the previous species so the new species' catalog ideals
        // apply. Judgment then derives them live (no seeding). Refresh the views.
        if (m_thresholdRepo)
            m_thresholdRepo->clear(id);
        if (m_thresholdsModel)
            m_thresholdsModel->refresh();
        if (m_care) {
            m_care->refresh();
            Q_EMIT careChanged();
        }
        if (m_plants)
            m_plants->refresh();
        Q_EMIT selectedPlantChanged();
    } catch (const StorageError &e) {
        setStatus(tr("Could not update species: %1").arg(QString::fromUtf8(e.what())));
    }
}

QString AppContext::addJournalEntry(int kind, const QString &note)
{
    if (!m_journalRepo || m_selectedPlantId.isEmpty())
        return {};
    if (kind < int(JournalEntryKind::Note) || kind > int(JournalEntryKind::Observation))
        return {};
    JournalEntry e;
    e.id = JournalEntryId::generate();
    e.plant = PlantId{ QUuid::fromString(m_selectedPlantId) };
    e.timestamp = QDateTime::currentDateTimeUtc();
    e.kind = static_cast<JournalEntryKind>(kind);
    e.note = note.trimmed();
    try {
        m_journalRepo->add(e);
        if (m_journal)
            m_journal->refresh();
        return e.id.toString();
    } catch (const StorageError &err) {
        setStatus(tr("Could not save entry: %1").arg(QString::fromUtf8(err.what())));
        return {};
    }
}

QString AppContext::saveJournalEntry(const QString &entryId, int kind, const QString &note,
                                     const QStringList &addPhotoUrls,
                                     const QStringList &removeAttachmentIds)
{
    // Create-or-update first so the entry exists before any photo binds to it.
    QString id = entryId;
    if (id.isEmpty())
        id = addJournalEntry(kind, note);
    else
        editJournalEntry(id, kind, note);
    if (id.isEmpty())
        return {}; // the create failed (status already set) — drop the staged photos.
    // Removals first, then the staged additions (each refreshes the journal model).
    for (const QString &attachmentId : removeAttachmentIds)
        removePhoto(attachmentId);
    for (const QString &url : addPhotoUrls)
        addPhotoToEntry(id, url, QString());
    return id;
}

void AppContext::editJournalEntry(const QString &entryId, int kind, const QString &note)
{
    if (!m_journalRepo || m_selectedPlantId.isEmpty())
        return;
    const PlantId plant{ QUuid::fromString(m_selectedPlantId) };
    const JournalEntryId id{ QUuid::fromString(entryId) };
    const QList<JournalEntry> entries = m_journalRepo->forPlant(plant);
    const auto it = std::find_if(entries.cbegin(), entries.cend(),
                                 [&](const JournalEntry &e) { return e.id == id; });
    if (it == entries.cend())
        return;
    // A user may set any creatable kind, or leave an agent-authored kind (e.g. Memory)
    // unchanged — but never CONVERT an entry into an uncreatable kind.
    const bool creatable = kind >= int(JournalEntryKind::Note) && kind <= int(JournalEntryKind::Observation);
    if (!creatable && kind != int(it->kind))
        return;
    JournalEntry e = *it;                         // keep its timestamp (the entry date)
    e.kind = static_cast<JournalEntryKind>(kind);
    e.note = note.trimmed();
    e.editedAt = QDateTime::currentDateTimeUtc(); // a user edit moves only the edit date
    try {
        m_journalRepo->update(e);
        if (m_journal)
            m_journal->refresh();
    } catch (const StorageError &err) {
        setStatus(tr("Could not save entry: %1").arg(QString::fromUtf8(err.what())));
    }
}

void AppContext::removeJournalEntry(const QString &entryId)
{
    if (!m_journalRepo)
        return;
    try {
        m_journalRepo->remove(JournalEntryId{ QUuid::fromString(entryId) });
        if (m_journal)
            m_journal->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not delete entry: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::addPhotoToEntry(const QString &entryId, const QString &fileUrl,
                                 const QString &caption)
{
    if (!m_attachmentRepo || !m_fileStore || !m_clock)
        return;
    const QString sourcePath = QUrl(fileUrl).toLocalFile();
    if (sourcePath.isEmpty())
        return;
    try {
        Attachment a;
        a.id = AttachmentId::generate();
        a.entry = JournalEntryId{ QUuid::fromString(entryId) };
        // Copy the file in first; the returned ref is what we persist (the row points at a real file).
        a.fileRef = m_fileStore->store(sourcePath, a.id);
        a.caption = caption.trimmed();
        a.addedAt = QDateTime::fromMSecsSinceEpoch(m_clock->nowMs(), QTimeZone::UTC);
        m_attachmentRepo->add(a);
        if (m_journal)
            m_journal->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not add photo: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::setPhotoCaption(const QString &attachmentId, const QString &caption)
{
    if (!m_attachmentRepo)
        return;
    try {
        m_attachmentRepo->updateCaption(AttachmentId{ QUuid::fromString(attachmentId) },
                                        caption.trimmed());
        if (m_journal)
            m_journal->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not update caption: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::removePhoto(const QString &attachmentId)
{
    if (!m_attachmentRepo || !m_fileStore)
        return;
    const AttachmentId id{ QUuid::fromString(attachmentId) };
    // Resolve the fileRef before deleting the row, so we can delete the file after (row-first: a
    // crash leaves a harmless orphan file, never a row pointing at a deleted file — ADR 0024 §5).
    QString fileRef;
    for (const Attachment &a : m_attachmentRepo->all()) {
        if (a.id == id) {
            fileRef = a.fileRef;
            break;
        }
    }
    try {
        m_attachmentRepo->remove(id);
        if (!fileRef.isEmpty())
            m_fileStore->remove(fileRef);
        if (m_journal)
            m_journal->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not delete photo: %1").arg(QString::fromUtf8(e.what())));
    }
}

QVariantList AppContext::photosForEntry(const QString &entryId) const
{
    QVariantList out;
    if (!m_attachmentRepo || !m_fileStore)
        return out;
    const JournalEntryId entry{ QUuid::fromString(entryId) };
    for (const Attachment &a : m_attachmentRepo->forEntry(entry)) {
        out.append(QVariantMap{
            { QStringLiteral("attachmentId"), a.id.toString() },
            { QStringLiteral("url"),
              QUrl::fromLocalFile(m_fileStore->absolutePath(a.fileRef)).toString() },
            { QStringLiteral("caption"), a.caption },
        });
    }
    return out;
}

void AppContext::addGlobalJournalEntry(int kind, const QString &note)
{
    if (!m_journalRepo)
        return;
    // The only user-creatable global kind is Note (globalCreatableJournalKinds); reject anything else.
    if (kind != int(JournalEntryKind::Note))
        return;
    JournalEntry e;
    e.id = JournalEntryId::generate();
    e.plant = std::nullopt;                       // a global (plant-less) entry
    e.timestamp = QDateTime::currentDateTimeUtc();
    e.kind = static_cast<JournalEntryKind>(kind);
    e.note = note.trimmed();
    try {
        m_journalRepo->add(e);
        if (m_globalJournal)
            m_globalJournal->refresh();
    } catch (const StorageError &err) {
        setStatus(tr("Could not save entry: %1").arg(QString::fromUtf8(err.what())));
    }
}

void AppContext::editGlobalJournalEntry(const QString &entryId, int kind, const QString &note)
{
    if (!m_journalRepo)
        return;
    const JournalEntryId id{ QUuid::fromString(entryId) };
    const QList<JournalEntry> entries = m_journalRepo->globalEntries();
    const auto it = std::find_if(entries.cbegin(), entries.cend(),
                                 [&](const JournalEntry &e) { return e.id == id; });
    if (it == entries.cend())
        return;
    // As for per-plant entries: a user may set any creatable kind or leave an agent-authored kind
    // (Memory) unchanged, but never CONVERT an entry into an uncreatable kind. Global-creatable is
    // Note only, so a user may keep Memory or set Note (ADR 0022).
    const bool creatable = kind == int(JournalEntryKind::Note);
    if (!creatable && kind != int(it->kind))
        return;
    JournalEntry e = *it;                         // keep its timestamp (the entry date) and plant scope
    e.kind = static_cast<JournalEntryKind>(kind);
    e.note = note.trimmed();
    e.editedAt = QDateTime::currentDateTimeUtc(); // a user edit moves only the edit date (ADR 0020)
    try {
        m_journalRepo->update(e);
        if (m_globalJournal)
            m_globalJournal->refresh();
    } catch (const StorageError &err) {
        setStatus(tr("Could not save entry: %1").arg(QString::fromUtf8(err.what())));
    }
}

void AppContext::removeGlobalJournalEntry(const QString &entryId)
{
    if (!m_journalRepo)
        return;
    try {
        m_journalRepo->remove(JournalEntryId{ QUuid::fromString(entryId) });
        if (m_globalJournal)
            m_globalJournal->refresh();
    } catch (const StorageError &e) {
        setStatus(tr("Could not delete entry: %1").arg(QString::fromUtf8(e.what())));
    }
}

void AppContext::attachSensor(const QString &deviceId)
{
    if (!m_care || !m_care->hasPlant())
        return;
    const DiscoveredDevice *d = m_scanner ? m_scanner->device(deviceId) : nullptr;
    // The desktop/Android handle is a MAC; Apple's CoreBluetooth UUID is wired later.
    const QString model = d ? d->model : QString();
    const std::span<const Reading> snapshot =
        d ? std::span<const Reading>(d->latest.constData(), d->latest.size())
          : std::span<const Reading>{};
    m_care->attach(HandleKind::Mac, deviceId, model, snapshot);
    if (m_registeredSensors)
        m_registeredSensors->refresh(); // the sensor is now bound (+ maybe newly registered)
    Q_EMIT careChanged();
}

void AppContext::detachSensor(const QString &sensorId)
{
    if (!m_care)
        return;
    m_care->detach(SensorId{ QUuid::fromString(sensorId) });
    if (m_registeredSensors)
        m_registeredSensors->refresh(); // the sensor is now unbound (deletable)
    Q_EMIT careChanged();
}

void AppContext::snoozeNotifications(int hours)
{
    if (!m_settings || !m_clock || hours <= 0)
        return;
    const qint64 until = m_clock->nowMs() + qint64(hours) * 60 * 60 * 1000;
    m_settings->setNotificationsSnoozedUntilMs(until); // emits notificationsChanged → our signal
}

QString AppContext::notificationsSnoozedText() const
{
    if (!m_settings || !m_clock)
        return {};
    const qint64 until = m_settings->notificationsSnoozedUntilMs();
    if (until <= m_clock->nowMs())
        return {}; // not snoozed (or the deadline has passed)
    const QDateTime t = QDateTime::fromMSecsSinceEpoch(until, QTimeZone::UTC).toLocalTime();
    return tr("Snoozed until %1").arg(QLocale().toString(t, QLocale::ShortFormat));
}

} // namespace klr
