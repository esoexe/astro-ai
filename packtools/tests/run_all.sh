#!/usr/bin/env bash
# Runs every pack tool test against the engine binary (default ./workspace_server).
cd "$(dirname "$0")/../.." || exit 1
export T="${T:-./workspace_server tool}"
rc=0
for t in packtools/tests/*.sh; do case "$t" in */lib.sh|*/run_all.sh) continue;; esac; bash "$t" || rc=1; done
exit $rc
