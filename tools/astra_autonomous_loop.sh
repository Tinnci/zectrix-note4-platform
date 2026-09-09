#!/usr/bin/env bash
# astra_autonomous_loop.sh
# Long-running continuous autonomous iteration harness for gpt-6-astra on zectrix-note4-platform

set -u

PROJECT_DIR="/Users/driezy/Downloads/zectrix-note4-platform"
INSTANCE_HOME="/Users/driezy/.antigravity_cockpit/instances/codex/86c6f79a63e09b7c"
CODEX_BIN="/Applications/ChatGPT.app/Contents/Resources/codex"
BACKLOG_FILE="${PROJECT_DIR}/ASTRA_TASKS.md"
LOOP_LOG="${PROJECT_DIR}/astra_loop.log"
LOCK_DIR="/tmp/astra_autonomous_loop.lock"
ITERATION_MAX=100
ITERATION_COUNT=0

export ALL_PROXY="http://127.0.0.1:7890"
export HTTP_PROXY="http://127.0.0.1:7890"
export HTTPS_PROXY="http://127.0.0.1:7890"
export http_proxy="http://127.0.0.1:7890"
export https_proxy="http://127.0.0.1:7890"
export all_proxy="http://127.0.0.1:7890"

# Single instance lock guard
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
    PID=$(cat "$LOCK_DIR/pid" 2>/dev/null || echo "")
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Another Astra autonomous loop is running (PID: $PID). Exiting."
        exit 0
    else
        rm -rf "$LOCK_DIR"
        mkdir "$LOCK_DIR" 2>/dev/null || exit 0
    fi
fi
echo $$ > "$LOCK_DIR/pid"
trap 'rm -rf "$LOCK_DIR"' EXIT INT TERM

export CODEX_HOME="$INSTANCE_HOME"
export SYNC_GH_IDENTITY=0
cd "$PROJECT_DIR" || exit 1

# Activate ESP-IDF dev environment so idf.py and esptool.py are available to Codex
if [ -f "${PROJECT_DIR}/tools/activate-dev-env.sh" ]; then
    # shellcheck disable=SC1091
    source "${PROJECT_DIR}/tools/activate-dev-env.sh" >/dev/null 2>&1 || true
fi
export ZECTRIX_PORT="${ZECTRIX_PORT:-/dev/cu.usbmodem14301}"

echo "==================================================" | tee -a "$LOOP_LOG"
echo "[$(date '+%Y-%m-%d %H:%M:%S')] Starting Astra Autonomous Iteration Loop (PID: $$)..." | tee -a "$LOOP_LOG"

while [ $ITERATION_COUNT -lt $ITERATION_MAX ]; do
    ITERATION_COUNT=$((ITERATION_COUNT + 1))
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] === Iteration $ITERATION_COUNT of $ITERATION_MAX ===" | tee -a "$LOOP_LOG"

    # Read the first pending task from backlog
    PENDING_TASK=$(grep '^- \[ \]' "$BACKLOG_FILE" | head -n 1)
    if [ -z "$PENDING_TASK" ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] All tasks in ASTRA_TASKS.md are completed! Loop exiting cleanly." | tee -a "$LOOP_LOG"
        break
    fi

    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Current Target Task: $PENDING_TASK" | tee -a "$LOOP_LOG"

    PROMPT="请继续执行当前工程的连续迭代任务：
1. 当前目标任务：${PENDING_TASK}
2. 阅读 ASTRA_TASKS.md 及相关契约文档。重点参考与对标 CrossPoint (crosspoint-reader: https://github.com/crosspoint-reader/crosspoint-reader) 的轻量流式排版、电子书分页阅读、待机画报与局域网传书，以及 Flipper Zero 的 SceneManager / ViewPort 场景状态机设计，将测试演示型 Launcher 升级为真正实用的掌上随身墨水屏终端系统。
3. 遵循'功能优先、避免繁琐 gate 卡点'的原则，写出简洁健壮的生产代码与驱动。
4. 代码行间注释使用英文（English comments）。
5. 编写完毕后在本地运行基础编译/Host 测试（如 bash tools/test-host.sh 或相应测试脚本），确认功能正常。若涉及硬件固件或底层驱动变动，可选择性执行 bash tools/device-smoke-test.sh 在连接的 ESP32-S3 实机上验证引导自检，切勿被硬件阻塞。
6. 完成后执行 git add 并使用规范的 git commit 提交该功能的改动（例如: feat(launcher): ... 或 feat(reader): ...）。
7. 更新 ASTRA_TASKS.md 将该条目勾选为 [x]，并简要输出本次迭代实现的总结。"

    # Execute with session resumption if available, otherwise new session
    "$CODEX_BIN" exec resume --last "$PROMPT" < /dev/null >> "$LOOP_LOG" 2>&1
    EXIT_CODE=$?

    if [ $EXIT_CODE -ne 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Session resume returned code $EXIT_CODE. Launching fresh session..." | tee -a "$LOOP_LOG"
        "$CODEX_BIN" exec -C "$PROJECT_DIR" -m gpt-6-astra "$PROMPT" < /dev/null >> "$LOOP_LOG" 2>&1
    fi

    # Check git status for uncommitted changes if Astra did not commit directly
    DIRTY_FILES=$(git status -s | grep -E '^[ MADRCU?]{2} ' | grep -v 'astra_')
    if [ -n "$DIRTY_FILES" ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] Found uncommitted changes from Astra, creating incremental commit..." | tee -a "$LOOP_LOG"
        git add -A
        git commit -m "feat(auto-iterate): incremental progress on ${PENDING_TASK#*- }" >> "$LOOP_LOG" 2>&1 || true
    fi

    # Brief cooldown between turns to allow network proxy buffers to reset
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Iteration $ITERATION_COUNT finished. Pausing 5 seconds before next loop..." | tee -a "$LOOP_LOG"
    sleep 5
done

echo "[$(date '+%Y-%m-%d %H:%M:%S')] Autonomous loop complete." | tee -a "$LOOP_LOG"
