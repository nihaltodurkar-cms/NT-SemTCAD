#include "remote_host.hpp"

namespace tcad::desktop {
namespace {

// The exact ASCII set Python's shlex.quote (re.ASCII \w, plus
// @%+=:,./-) treats as never needing quoting -- posixQuote must match
// it precisely so a string this quotes is byte-identical to what
// workbench.remote_executor.quote would produce for the same input,
// and so Python's shlex.split(..., posix=True) (fake_ssh.py's own
// un-quoting) parses either one back the same way.
bool isSafeChar(QChar c) {
    const ushort u = c.unicode();
    if ((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '_') return true;
    static const QString extra = QStringLiteral("@%+=:,./-");
    return extra.contains(c);
}

}  // namespace

QString validateRemoteHostField(const QString& value, const QString& field) {
    if (value.isEmpty()) return field + QStringLiteral(" must not be empty");
    if (value.at(0) == QLatin1Char('-'))
        return field + QStringLiteral(" '") + value +
               QStringLiteral("' must not start with '-' (ssh/scp would read it as an option, not a name)");
    for (const QChar c : value)
        if (c.isSpace()) return field + QStringLiteral(" '") + value + QStringLiteral("' must not contain whitespace");
    return QString();
}

QString validateRemoteHost(const RemoteHostConfig& host) {
    QString err = validateRemoteHostField(host.host, QStringLiteral("host"));
    if (!err.isEmpty()) return err;
    if (!host.user.isEmpty()) return validateRemoteHostField(host.user, QStringLiteral("user"));
    return QString();
}

QString posixQuote(const QString& s) {
    if (s.isEmpty()) return QStringLiteral("''");
    bool safe = true;
    for (const QChar c : s) {
        if (!isSafeChar(c)) {
            safe = false;
            break;
        }
    }
    if (safe) return s;
    QString escaped = s;
    escaped.replace(QLatin1Char('\''), QStringLiteral("'\"'\"'"));
    return QLatin1Char('\'') + escaped + QLatin1Char('\'');
}

QString remoteHostTarget(const RemoteHostConfig& host) {
    return host.user.isEmpty() ? host.host : host.user + QLatin1Char('@') + host.host;
}

}  // namespace tcad::desktop
