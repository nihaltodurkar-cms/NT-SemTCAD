"""M30 Phase 12 GUI test fixture: a local stand-in for the `ssh`
binary.

Strips the same flags RemoteJobRunner/SSHTransport pass
(-p/-i/-o KEY=VAL), then just runs the remote command STRING locally
via the shell -- "a second local process pretending to be remote over
loopback," the plan doc's own suggested test strategy, re-used here at
the GUI/QProcess layer instead of the library-level Transport layer.
No real network or sshd involved.

Usage: python fake_ssh.py [-p PORT] [-o K=V]... [-i FILE] TARGET COMMAND

NATIVE-DESKTOP-PLAN.md 18.2: the real command is now POSIX-shell-quoted
(`workbench.remote_executor.quote`, one token per argv element) before
it is joined into COMMAND, since a real remote shell parses it that
way. `shlex.split(..., posix=True)` undoes exactly that quoting here,
so the parsed argv is run DIRECTLY (no `shell=True`) -- a real system
shell would have to be cmd.exe on this machine, which does not
understand POSIX single-quoting, and mis-parsed it (measured: "The
filename, directory name, or volume label syntax is incorrect").
"""
import os
import shlex
import subprocess
import sys


def main(argv):
    i = 0
    while i < len(argv):
        if argv[i] in ("-p", "-i"):
            i += 2
        elif argv[i] == "-o":
            i += 2
        elif argv[i] == "--":  # NATIVE-DESKTOP-PLAN.md 18.2: ends option parsing
            i += 1
            break
        else:
            break
    # argv[i] is TARGET (ignored -- there is no real remote host),
    # argv[i + 1] is the command string to execute "remotely."
    command = argv[i + 1]
    # The pretend remote is a POSIX host, but the local shell may be
    # cmd.exe, whose `mkdir` takes "-p" as a directory NAME (it left a
    # stray "-p" folder in the project and failed on every later run).
    # Do what POSIX `mkdir -p` does, portably. The path is taken
    # verbatim: RemoteJobRunner sends exactly one, and a Windows path's
    # backslashes must not be read as shell escapes.
    parts = shlex.split(command, posix=True)
    if parts[:2] == ["mkdir", "-p"]:
        os.makedirs(parts[2], exist_ok=True)
        return 0
    return subprocess.run(parts).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
