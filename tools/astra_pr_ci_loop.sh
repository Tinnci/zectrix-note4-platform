#!/usr/bin/env bash
set -euo pipefail

# tools/astra_pr_ci_loop.sh: Continuous autonomous loop with auto-recovery for Astra PR & CI/CD verification

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

export ALL_PROXY="http://127.0.0.1:7890"
export HTTP_PROXY="http://127.0.0.1:7890"
export HTTPS_PROXY="http://127.0.0.1:7890"
export http_proxy="http://127.0.0.1:7890"
export https_proxy="http://127.0.0.1:7890"
export all_proxy="http://127.0.0.1:7890"

LOG_FILE="$REPO_ROOT/astra_pr_ci.log"
CODEX_BIN="/Applications/ChatGPT.app/Contents/Resources/codex"

if [ -f "$REPO_ROOT/tools/activate-dev-env.sh" ]; then
    # shellcheck disable=SC1091
    source "$REPO_ROOT/tools/activate-dev-env.sh" >> "$LOG_FILE" 2>&1 || true
fi

ITERATION_MAX=30
ITERATION_COUNT=0

echo "==================================================" | tee -a "$LOG_FILE"
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Starting Astra Continuous PR & CI/CD Loop (PID: $$)..." | tee -a "$LOG_FILE"

PROMPT="你是 gpt-6-astra，Zectrix Note4 平台的自主研发代理。
我们已圆满完成全部 5 大核心里程碑（C1 互联平台、D1 运维 CLI、M5 OTA 体系、R1 进阶墨水屏渲染引擎、Q1 质量与协议对标，共 15 个清晰的 Git Commit，且本地 26 项 Host 测试全通）。
你现在进入全新的研发阶段，核心目标是：
1. 【实机运行状态审查 (Physical Device Runtime Verification)】：
   - 物理硬件 ESP32-S3 正通过 USB 连接在 /dev/cu.usbmodem14301 上。
   - 运行 bash tools/device-smoke-test.sh 检验最新固件在实际机器上的刷入与引导自检（包含 Bootloader、8MB Octal PSRAM、EFuse、分区表及应用层初始化），记录实际硬件运行日志。
2. 【创建特性分支并提交 Pull Request (Submit Pull Request)】：
   - 当前 main 分支领先 origin/main 15 个提交。请创建独立的特性分支（例如 feat/platform-autonomous-milestones 或 feat/milestones-c1-d1-m5-r1-q1）。
   - 将分支推送到远端：git push -u origin <branch-name>。
   - 使用 gh pr create 创建高质量的 Pull Request（目标基线分支 main）：
     - 标题规范，例如: feat(platform): deliver milestones C1, D1, M5, R1, Q1 with hardware verification
     - PR 正文采用中英文双语，结构化呈现：
       * 5 大 Milestone 达成概览与 15 个增量提交清单；
       * 核心技术方案（ADR 0005、Pebble/Flipper Zero 对标设计、显存位级裁剪、低功耗倒序下电等）；
       * 本地测试覆盖（26 Host targets、25 Android JVM tests、ASan/UBSan）；
       * ESP32-S3 实机刷写与 6 秒串口启动验证日志。
3. 【检查并闭环 CI/CD (Inspect and Pass CI/CD)】：
   - 检查 GitHub Actions 流水线状态（通过 gh pr checks 或 gh run list --branch <branch-name> / gh run watch）。
   - 包含的 3 个检查作业：Host tests and static checks (shellcheck, test-host.sh)、Android companion、ESP32-S3 firmware。
   - 如果 CI/CD 遇到任何报错、静态代码检查问题或环境异常，请查阅日志并编写修复代码，遵守'注释英文、功能优先、禁止npm统一使用bun'的原则，提交并 push 修复，直到 CI/CD 完全绿灯（PASS）。
   - 最终输出 PR 链接及 CI/CD 执行报告。

请立即开始执行！"

while [ $ITERATION_COUNT -lt $ITERATION_MAX ]; do
    ITERATION_COUNT=$((ITERATION_COUNT + 1))
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] === PR & CI Loop Iteration $ITERATION_COUNT of $ITERATION_MAX ===" | tee -a "$LOG_FILE"

    # Check if PR exists and all CI checks are passing
    PR_URL=$(gh pr list --state open --json url -q '.[0].url' 2>/dev/null || true)
    if [ -n "$PR_URL" ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Active PR detected: $PR_URL" | tee -a "$LOG_FILE"
        CHECKS_OUTPUT=$(gh pr checks "$PR_URL" 2>&1 || true)
        echo "$CHECKS_OUTPUT" | tee -a "$LOG_FILE"
        if echo "$CHECKS_OUTPUT" | grep -qi "fail"; then
            echo "[$(date '+%Y-%m-%d %H:%M:%S')] CI checks have failures, prompting Astra to diagnose and fix..." | tee -a "$LOG_FILE"
        elif echo "$CHECKS_OUTPUT" | grep -qi "pending"; then
            echo "[$(date '+%Y-%m-%d %H:%M:%S')] CI checks are currently in progress..." | tee -a "$LOG_FILE"
        elif echo "$CHECKS_OUTPUT" | grep -qi "pass" && ! echo "$CHECKS_OUTPUT" | grep -qEi "fail|pending"; then
            echo "[$(date '+%Y-%m-%d %H:%M:%S')] All CI checks have passed successfully! PR and CI/CD complete." | tee -a "$LOG_FILE"
            break
        fi
    fi

    # Execute session with resume fallback
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Running Astra session..." | tee -a "$LOG_FILE"
    "$CODEX_BIN" exec resume --last "$PROMPT" < /dev/null >> "$LOG_FILE" 2>&1
    EXIT_CODE=$?

    if [ $EXIT_CODE -ne 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Session resume returned code $EXIT_CODE. Launching fresh session..." | tee -a "$LOG_FILE"
        "$CODEX_BIN" exec -C "$REPO_ROOT" -m gpt-6-astra "$PROMPT" < /dev/null >> "$LOG_FILE" 2>&1 || true
    fi

    # Check for uncommitted changes left by Astra
    DIRTY_FILES=$(git status -s | grep -E '^[ MADRCU?]{2} ' | grep -v 'astra_')
    if [ -n "$DIRTY_FILES" ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Found uncommitted changes from Astra, creating incremental commit..." | tee -a "$LOG_FILE"
        git add -A
        git commit -m "fix(ci): incremental autonomous fix from astra" >> "$LOG_FILE" 2>&1 || true
    fi

    # Push current branch if ahead of remote
    CURRENT_BRANCH=$(git rev-parse --abbrev-ref HEAD)
    if [ "$CURRENT_BRANCH" != "HEAD" ] && [ "$CURRENT_BRANCH" != "main" ]; then
        AHEAD_COUNT=$(git rev-list --count "origin/$CURRENT_BRANCH..$CURRENT_BRANCH" 2>/dev/null || echo 0)
        if [ "$AHEAD_COUNT" -gt 0 ]; then
            echo "[$(date '+%Y-%m-%d %H:%M:%S')] Pushing $AHEAD_COUNT commit(s) on $CURRENT_BRANCH to remote..." | tee -a "$LOG_FILE"
            git push origin "$CURRENT_BRANCH" >> "$LOG_FILE" 2>&1 || true
        fi
    fi

    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Iteration $ITERATION_COUNT complete. Pausing 10 seconds..." | tee -a "$LOG_FILE"
    sleep 10
done

echo "[$(date '+%Y-%m-%d %H:%M:%S')] Continuous loop finished." | tee -a "$LOG_FILE"
