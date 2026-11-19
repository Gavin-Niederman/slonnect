#include "dbusintrospect.h"

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusSignature>
#include <QDBusVariant>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QXmlStreamReader>
#include <cmath>

namespace introspect {

const Interface *Node::interface(const QString &name) const
{
    for (const auto &i : interfaces)
        if (i.name == name)
            return &i;
    return nullptr;
}

Node parse(const QString &xml)
{
    Node node;
    QXmlStreamReader r(xml);
    int depth = 0;
    Interface *iface = nullptr;
    Method *method = nullptr;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement()) {
            ++depth;
            const auto name = r.name();
            const auto attrs = r.attributes();
            if (name == QLatin1String("node") && depth == 2) {
                node.children << attrs.value(QLatin1String("name")).toString();
            } else if (name == QLatin1String("interface")) {
                node.interfaces << Interface{attrs.value(QLatin1String("name")).toString(), {}, {}};
                iface = &node.interfaces.last();
            } else if (name == QLatin1String("method") && iface) {
                iface->methods << Method{attrs.value(QLatin1String("name")).toString(), {}, {}, {}};
                method = &iface->methods.last();
            } else if (name == QLatin1String("property") && iface) {
                iface->properties << attrs.value(QLatin1String("name")).toString();
            } else if (name == QLatin1String("arg") && method) {
                const QString type = attrs.value(QLatin1String("type")).toString();
                if (attrs.value(QLatin1String("direction")) == QLatin1String("out")) {
                    method->out += type;
                } else {
                    method->in += type;
                    method->inNames << attrs.value(QLatin1String("name")).toString();
                }
            }
        } else if (r.isEndElement()) {
            --depth;
            if (r.name() == QLatin1String("method"))
                method = nullptr;
            else if (r.name() == QLatin1String("interface"))
                iface = nullptr, method = nullptr;
        }
    }
    return node;
}

void fetch(const QDBusConnection &bus, const QString &service, const QString &path, QObject *context, std::function<void(bool, const Node &)> done)
{
    if (service.isEmpty() || !path.startsWith(QLatin1Char('/')))
        return done(false, {});
    auto msg = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Introspectable"), QStringLiteral("Introspect"));
    auto *w = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), context);
    QObject::connect(w, &QDBusPendingCallWatcher::finished, w, [done](QDBusPendingCallWatcher *w) {
        w->deleteLater();
        const QDBusMessage reply = w->reply();
        if (reply.type() != QDBusMessage::ReplyMessage)
            return done(false, {});
        done(true, parse(reply.arguments().value(0).toString()));
    });
}

// Length of the complete type starting at `i`.
static int typeLength(const QString &s, int i)
{
    if (i >= s.size())
        return 0;
    const QChar c = s.at(i);
    if (c == QLatin1Char('a'))
        return 1 + typeLength(s, i + 1);
    if (c == QLatin1Char('(') || c == QLatin1Char('{')) {
        const QChar close = c == QLatin1Char('(') ? QLatin1Char(')') : QLatin1Char('}');
        int j = i + 1;
        while (j < s.size() && s.at(j) != close) {
            const int n = typeLength(s, j);
            if (n == 0)
                return 0;
            j += n;
        }
        return j < s.size() ? j - i + 1 : 0;
    }
    return 1;
}

QStringList splitSignature(const QString &signature)
{
    QStringList types;
    for (int i = 0; i < signature.size();) {
        const int n = typeLength(signature, i);
        if (n == 0)
            break;
        types << signature.mid(i, n);
        i += n;
    }
    return types;
}

namespace {

bool toInteger(const QVariant &v, double lo, double hi, qint64 *out)
{
    bool ok = false;
    const double d = v.metaType() == QMetaType::fromType<QString>() ? v.toString().trimmed().toDouble(&ok) : (ok = v.canConvert<double>(), v.toDouble());
    if (!ok || std::floor(d) != d || d < lo || d > hi)
        return false;
    *out = qint64(d);
    return true;
}

template<typename T>
QVariant listOf(const QVariant &value, const QString &element, bool *ok)
{
    QList<T> list;
    for (const QVariant &e : value.toList()) {
        const QVariant c = coerce(e, element, ok);
        if (!*ok)
            return {};
        list << c.value<T>();
    }
    return QVariant::fromValue(list);
}

} // namespace

QVariant coerce(const QVariant &value, const QString &type, bool *ok)
{
    *ok = true;
    if (type.isEmpty()) {
        *ok = false;
        return {};
    }
    const QChar c = type.at(0);
    qint64 n = 0;
    if (type.size() == 1) {
        switch (c.unicode()) {
        case 'y':
            if (!(*ok = toInteger(value, 0, 255, &n))) return {};
            return QVariant::fromValue(uchar(n));
        case 'n':
            if (!(*ok = toInteger(value, -32768, 32767, &n))) return {};
            return QVariant::fromValue(short(n));
        case 'q':
            if (!(*ok = toInteger(value, 0, 65535, &n))) return {};
            return QVariant::fromValue(ushort(n));
        case 'i':
            if (!(*ok = toInteger(value, -2147483648.0, 2147483647.0, &n))) return {};
            return int(n);
        case 'u':
            if (!(*ok = toInteger(value, 0, 4294967295.0, &n))) return {};
            return uint(n);
        case 'x':
            if (!(*ok = toInteger(value, -9.2e18, 9.2e18, &n))) return {};
            return qlonglong(n);
        case 't':
            if (!(*ok = toInteger(value, 0, 1.8e19, &n))) return {};
            return qulonglong(n);
        case 'd':
            return value.metaType() == QMetaType::fromType<QString>() ? QVariant(value.toString().toDouble(ok)) : QVariant(value.toDouble(ok));
        case 'b':
            if (value.metaType() == QMetaType::fromType<QString>())
                return value.toString().trimmed().compare(QLatin1String("true"), Qt::CaseInsensitive) == 0 || value.toString().trimmed() == QLatin1String("1");
            return value.toBool();
        case 's':
            return value.toString();
        case 'o':
            if (!value.toString().startsWith(QLatin1Char('/'))) {
                *ok = false;
                return {};
            }
            return QVariant::fromValue(QDBusObjectPath(value.toString()));
        case 'g':
            return QVariant::fromValue(QDBusSignature(value.toString()));
        case 'v': {
            // Strings stay strings; whole numbers go as int32, like most APIs expect.
            const QVariant inner = value.metaType() == QMetaType::fromType<double>() && std::floor(value.toDouble()) == value.toDouble()
                                       && std::abs(value.toDouble()) <= 2147483647.0
                                   ? QVariant(int(value.toDouble()))
                                   : value;
            return QVariant::fromValue(QDBusVariant(inner));
        }
        }
    }
    if (c == QLatin1Char('a')) {
        const QString element = type.mid(1);
        if (element == QLatin1String("y"))
            return value.toString().toUtf8();
        if (element.startsWith(QLatin1Char('{'))) {
            // Dictionaries: QVariantMap marshals as a{sv}; only that form is supported.
            if (element != QLatin1String("{sv}") || value.metaType() != QMetaType::fromType<QVariantMap>()) {
                *ok = false;
                return {};
            }
            QVariantMap map;
            const QVariantMap in = value.toMap();
            for (auto it = in.begin(); it != in.end(); ++it)
                map.insert(it.key(), coerce(it.value(), QStringLiteral("v"), ok));
            return map;
        }
        if (value.metaType() != QMetaType::fromType<QVariantList>() && value.metaType() != QMetaType::fromType<QStringList>()) {
            *ok = false;
            return {};
        }
        if (element == QLatin1String("s"))
            return value.toStringList();
        if (element == QLatin1String("i"))
            return listOf<int>(value, element, ok);
        if (element == QLatin1String("u"))
            return listOf<uint>(value, element, ok);
        if (element == QLatin1String("x"))
            return listOf<qlonglong>(value, element, ok);
        if (element == QLatin1String("t"))
            return listOf<qulonglong>(value, element, ok);
        if (element == QLatin1String("d"))
            return listOf<double>(value, element, ok);
        if (element == QLatin1String("b"))
            return listOf<bool>(value, element, ok);
        if (element == QLatin1String("o"))
            return listOf<QDBusObjectPath>(value, element, ok);
        if (element == QLatin1String("v")) {
            QVariantList list;
            for (const QVariant &e : value.toList())
                list << coerce(e, element, ok);
            return list;
        }
    }
    // Structs and nested containers aren't built from JSON.
    *ok = false;
    return {};
}

QString argsSkeleton(const QString &signature)
{
    QJsonArray a;
    for (const QString &t : splitSignature(signature)) {
        switch (t.at(0).unicode()) {
        case 's':
        case 'g':
            a << QString();
            break;
        case 'o':
            a << QStringLiteral("/");
            break;
        case 'b':
            a << false;
            break;
        case 'a':
            if (t.startsWith(QLatin1String("a{")))
                a << QJsonObject();
            else
                a << QJsonArray();
            break;
        case 'v':
            a << QString();
            break;
        default:
            a << 0;
        }
    }
    return QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));
}

} // namespace introspect
