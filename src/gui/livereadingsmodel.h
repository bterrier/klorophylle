// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "reading.h"

#include <QtCore/QAbstractListModel>
#include <QtCore/QList>
#include <QtCore/QString>

namespace klr {

class BleScanner;
class SettingsStore;

// The selected device's latest readings, live. Thin per-screen model: formatting
// (label, value text) is done in C++ via klr_core's format.h and surfaced as
// read-only roles, so the QML carries no presentation logic.
class LiveReadingsModel final : public QAbstractListModel {
    Q_OBJECT

public:
    enum Role {
        QuantityRole = Qt::UserRole + 1,
        LabelRole,
        ValueTextRole, // formatted "42.0 %" / "—" (avoids Controls' FINAL "display")
        UnitRole,
        PresentRole,
    };

    LiveReadingsModel(BleScanner &scanner, const SettingsStore &settings,
                      QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDeviceId(const QString &id);

private:
    void refresh();

    BleScanner &m_scanner;
    const SettingsStore &m_settings;
    QString m_id;
    QList<Reading> m_rows;
};

} // namespace klr
