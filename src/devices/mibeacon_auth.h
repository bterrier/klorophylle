// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QtCore/QByteArray>

// Xiaomi MiBeacon "verify" handshake for the Flower Care family (HHCCJCY01 / HHCCPOT002 / Grow Care
// Garden), required before the history service answers on current firmware (ADR 0014). Pure: no I/O,
// no Bluetooth — the byte math only. The interactive write→await→write sequence that consumes these
// tokens lives in GattHistorySession (klr_ble). Ported from WatchFlower's device_flowercare.cpp +
// thirdparty/RC4 (ARC4, © Christophe Devine, GPL).
namespace klr {

// ARC4 keystream XOR. Returns a copy of `data` en/decrypted under `key` (symmetric). Empty key →
// data unchanged. The standard RC4 KAT (key "Key", data "Plaintext" → BB F3 16 E8 D9 40 AF 0A D3)
// holds, so this is the canonical algorithm.
QByteArray rc4(const QByteArray &key, const QByteArray &data);

// The two tokens of the handshake. `challenge` (12 bytes) is written to the challenge
// characteristic; `finish` (4 bytes) is written after the device's challenge response.
struct MiBeaconHandshake {
    QByteArray challenge;
    QByteArray finish;
};

// Derive the handshake from the device's 6-byte MAC (natural order: mac[0] is the first printed
// octet) and its MiBeacon product id (0x0098 Flower Care, 0x015D RoPot, 0x03BC Grow Care Garden).
// `mac` shorter than 6 bytes yields empty tokens.
MiBeaconHandshake mibeaconHandshake(const QByteArray &mac, quint16 productId);

} // namespace klr
