#!/bin/sh
# Save terminal attributes so the normal debug console is restored after exit.
settings=$(stty -g)
trap 'stty "$settings"' EXIT HUP INT TERM
stty raw -echo
/opt/evidence-bridge/evidence_bridge --stdio
