#!/usr/bin/env bash
# Forward to the continuous autonomous loop runner
exec "$(dirname "$0")/astra_pr_ci_loop.sh" "$@"
