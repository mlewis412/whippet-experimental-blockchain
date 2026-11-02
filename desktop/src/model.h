#pragma once
#include <QtCore>
#include <limits>

namespace Model {
inline bool parseAmount(const QString& text, qint64& value) {
    static const QRegularExpression pattern("^(0|[1-9][0-9]{0,10})(?:\\.([0-9]{1,8}))?$");
    const auto m = pattern.match(text.trimmed());
    if (!m.hasMatch()) return false;
    const qint64 whole = m.captured(1).toLongLong();
    const qint64 fraction = m.captured(2).leftJustified(8, '0').toLongLong();
    if (whole > (std::numeric_limits<qint64>::max() - fraction) / 100000000) return false;
    value = whole * 100000000 + fraction;
    return value > 0;
}
inline QString coins(double value) {
    QString s = QLocale::c().toString(value, 'f', 8);
    while (s.endsWith('0') && !s.endsWith(".00")) s.chop(1);
    return s;
}
inline QString duration(qint64 seconds) {
    return seconds >= 3600 ? QString("%1 h %2 min").arg(seconds/3600).arg((seconds%3600)/60)
         : seconds >= 60 ? QString("%1 min %2 sec").arg(seconds/60).arg(seconds%60)
         : QString("%1 sec").arg(qMax<qint64>(0, seconds));
}
inline bool needsAuxpow(const QString& network, int nextHeight) {
    return network == "main" ? nextHeight >= 371337 : network == "test" ? nextHeight >= 158100 : nextHeight >= 20;
}
inline bool canMine(const QString& network, bool initialDownload, int peers, bool ready) {
    return ready && (network == "regtest" || (!initialDownload && peers > 0));
}
inline QString networkName(const QString& network) {
    return network == "regtest" ? "Local test chain" : network == "test" ? "Testnet" : "Experimental mainnet";
}
}
