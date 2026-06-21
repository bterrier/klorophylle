// SPDX-License-Identifier: GPL-3.0-or-later
#include "mibeacon_auth.h"

namespace klr {

QByteArray rc4(const QByteArray &key, const QByteArray &data)
{
    if (key.isEmpty())
        return data;

    int m[256];
    for (int i = 0; i < 256; ++i)
        m[i] = i;

    const auto *kp = reinterpret_cast<const quint8 *>(key.constData());
    const int klen = int(key.size());
    for (int i = 0, j = 0, k = 0; i < 256; ++i) {
        const int a = m[i];
        j = (j + a + kp[k]) & 0xff;
        m[i] = m[j];
        m[j] = a;
        if (++k >= klen)
            k = 0;
    }

    QByteArray out(data);
    auto *dp = reinterpret_cast<quint8 *>(out.data());
    int x = 0, y = 0;
    for (int i = 0; i < out.size(); ++i) {
        x = (x + 1) & 0xff;
        const int a = m[x];
        y = (y + a) & 0xff;
        const int b = m[y];
        m[x] = m[y];
        m[y] = a;
        dp[i] ^= quint8(m[(a + b) & 0xff]);
    }
    return out;
}

MiBeaconHandshake mibeaconHandshake(const QByteArray &mac, quint16 productId)
{
    if (mac.size() < 6)
        return {};

    const auto *m = reinterpret_cast<const quint8 *>(mac.constData());
    const quint8 pid0 = quint8(productId & 0xff);

    // The fixed seeds + the MAC/PID mix, verbatim from WatchFlower's implementation.
    const QByteArray token1 =
        QByteArray::fromRawData("\x01\x22\x03\x04\x05\x06\x06\x05\x04\x03\x02\x01", 12);
    const QByteArray magicEnd = QByteArray::fromRawData("\x92\xab\x54\xfa", 4);
    const quint8 mixaBytes[8] = { m[5], m[3], m[0], pid0, pid0, m[1], m[0], m[4] };
    const QByteArray mixa(reinterpret_cast<const char *>(mixaBytes), 8);

    return { rc4(mixa, token1), rc4(token1, magicEnd) };
}

} // namespace klr
