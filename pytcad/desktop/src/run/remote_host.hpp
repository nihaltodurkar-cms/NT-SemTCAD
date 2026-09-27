// One remote worker (NATIVE-DESKTOP-PLAN.md 18.2/18.3), and the two
// checks that close the vulnerabilities 18.1 found in the PYTHON
// runners (workbench/remote_executor.py, gui/services/remote_job_runner.py)
// -- ported here so the native runner never re-introduces them:
//
//   1. SSH argument injection via the host/user string: a host
//      starting with '-' (e.g. "-oProxyCommand=...") is parsed by
//      ssh/scp as a LOCAL option, not a hostname. Closed by
//      `validateRemoteHost` (fails closed: reject empty, a leading
//      '-', or embedded whitespace) plus a `--` before the target in
//      every ssh/scp argv (defense in depth -- see remote_job_runner.cpp).
//   2. Remote shell injection via unquoted paths: `remoteWorkdir`/
//      `python` are NOT validated (any path character is legal) but
//      MUST be quoted with `posixQuote` wherever they reach a command
//      string a remote shell parses (the mkdir/run stages).
//
// One source of truth, same as the Python side's `validate_remote_host`/
// `quote` in workbench/remote_executor.py -- the same two functions,
// the same rules, so a host string is accepted or refused identically
// whichever app it is typed into.
#pragma once

#include <QString>

namespace tcad::desktop {

struct RemoteHostConfig {
    QString host;
    QString user;                              // empty: no "user@"
    int port = 22;
    QString identity_file;                      // empty: none (-i omitted)
    QString python = "python";
    QString remote_workdir = "/tmp/pytcad-remote";
};

// Empty on success; otherwise the reason `field` (host/user) is rejected.
// Fails closed: nothing a real hostname/username looks like is rejected.
QString validateRemoteHostField(const QString& value, const QString& field);

// Both fields of `host`; empty on success.
QString validateRemoteHost(const RemoteHostConfig& host);

// POSIX-shell-quotes one token for a command string a REMOTE login
// shell parses (ssh(1): the trailing arguments are concatenated and
// handed to the remote shell as one string) -- wraps in '...', an
// embedded ' escaped as '\''. The same algorithm as Python's
// shlex.quote (and workbench.remote_executor.quote, which wraps it).
QString posixQuote(const QString& s);

// "user@host" or "host" (no user configured) -- never itself validated
// or quoted here; call validateRemoteHost first.
QString remoteHostTarget(const RemoteHostConfig& host);

}  // namespace tcad::desktop
