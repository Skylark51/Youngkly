#include "Archive.h"
#include <QFile>
#include <QSaveFile>
#include <QtEndian>
#include <stdexcept>
#include <zlib.h>

namespace youngkly {
namespace {
[[noreturn]] void fail(const QString &s) {
    throw std::runtime_error(s.toStdString());
}
quint16 u16(const QByteArray &b, qsizetype p) {
    if (p < 0 || p + 2 > b.size())
        fail("Truncated ZIP");
    return qFromLittleEndian<quint16>(b.constData() + p);
}
quint32 u32(const QByteArray &b, qsizetype p) {
    if (p < 0 || p + 4 > b.size())
        fail("Truncated ZIP");
    return qFromLittleEndian<quint32>(b.constData() + p);
}
void put16(QByteArray &b, quint16 n) {
    char p[2];
    qToLittleEndian(n, p);
    b.append(p, 2);
}
void put32(QByteArray &b, quint32 n) {
    char p[4];
    qToLittleEndian(n, p);
    b.append(p, 4);
}
} // namespace
QByteArray readFile(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        fail(path + ": " + f.errorString());
    return f.readAll();
}
void writeFile(const QString &path, const QByteArray &bytes) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size() || !f.commit())
        fail(path + ": write failed");
}
Archive readZip(const QString &path) {
    const auto b = readFile(path);
    qsizetype end = b.size() - 22;
    for (; end >= qMax<qsizetype>(0, b.size() - 65557); --end)
        if (u32(b, end) == 0x06054b50)
            break;
    if (end < 0 || u32(b, end) != 0x06054b50)
        fail("ZIP directory missing: " + path);
    if (u16(b, end + 4) || u16(b, end + 6) || u16(b, end + 8) != u16(b, end + 10))
        fail("Multi-disk ZIP unsupported");
    auto pos = qsizetype(u32(b, end + 16));
    Archive result;
    for (int i = 0; i < u16(b, end + 10); ++i) {
        if (u32(b, pos) != 0x02014b50)
            fail("Invalid ZIP directory");
        const auto method = u16(b, pos + 10);
        const auto compressed = u32(b, pos + 20), size = u32(b, pos + 24), offset = u32(b, pos + 42);
        const auto nl = u16(b, pos + 28), extra = u16(b, pos + 30), comment = u16(b, pos + 32);
        if (size > 256 * 1024 * 1024 || compressed > 256 * 1024 * 1024 || (u16(b, pos + 8) & 1))
            fail("Unsupported ZIP entry");
        if (pos + 46 + nl + extra + comment > b.size())
            fail("Truncated ZIP directory entry");
        const auto name = QString::fromUtf8(b.mid(pos + 46, nl));
        if (u32(b, offset) != 0x04034b50)
            fail("Invalid ZIP local entry");
        const qsizetype start = qsizetype(offset) + 30 + u16(b, offset + 26) + u16(b, offset + 28);
        if (start + compressed > b.size())
            fail("Truncated ZIP payload");
        QByteArray data;
        if (method == 0)
            data = b.mid(start, compressed);
        else if (method == 8) {
            data.resize(size);
            z_stream z{};
            z.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(b.constData() + start));
            z.avail_in = compressed;
            z.next_out = reinterpret_cast<Bytef *>(data.data());
            z.avail_out = size;
            if (inflateInit2(&z, -MAX_WBITS) != Z_OK)
                fail("ZIP inflate init failed");
            const int status = inflate(&z, Z_FINISH);
            inflateEnd(&z);
            if (status != Z_STREAM_END || z.total_out != size)
                fail("ZIP inflate failed");
        } else
            fail("ZIP compression unsupported");
        if (data.size() != size ||
            ::crc32(0, reinterpret_cast<const Bytef *>(data.constData()), data.size()) != u32(b, pos + 16))
            fail("ZIP checksum failed");
        if (result.contains(name))
            fail("Duplicate ZIP entry");
        result.insert(name, data);
        pos += 46 + nl + extra + comment;
    }
    return result;
}
void writeZip(const QString &path, const Archive &entries) {
    QByteArray out, central;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        const auto name = it.key().toUtf8(), data = it.value();
        const auto crc = ::crc32(0, reinterpret_cast<const Bytef *>(data.constData()), data.size());
        const auto offset = out.size();
        put32(out, 0x04034b50);
        put16(out, 20);
        put16(out, 0x800);
        put16(out, 0);
        put16(out, 0);
        put16(out, 0);
        put32(out, crc);
        put32(out, data.size());
        put32(out, data.size());
        put16(out, name.size());
        put16(out, 0);
        out += name;
        out += data;
        put32(central, 0x02014b50);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x800);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, crc);
        put32(central, data.size());
        put32(central, data.size());
        put16(central, name.size());
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += name;
    }
    const auto offset = out.size();
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, entries.size());
    put16(out, entries.size());
    put32(out, central.size());
    put32(out, offset);
    put16(out, 0);
    writeFile(path, out);
}
} // namespace youngkly
