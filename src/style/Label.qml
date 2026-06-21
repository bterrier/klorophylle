// SPDX-License-Identifier: GPL-3.0-or-later
// Klorophylle.Style implementation of Label — Inter body type in the theme text colour,
// links in the AI/sensor cyan. Rooted on the QtQuick.Templates type.
import QtQuick
import QtQuick.Templates as T

T.Label {
    color: Theme.colorText
    linkColor: Theme.colorAI
    font.family: Theme.fontBody
    font.pixelSize: Theme.fontSizeBody
}
