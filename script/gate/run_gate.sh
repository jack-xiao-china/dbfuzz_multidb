#!/bin/bash
# 统一门禁运行器：运行指定 Phase 的 Gate 脚本
# 用法: bash script/gate/run_gate.sh [0|1|2|3]

PHASE=${1:-0}
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GATE_SCRIPT="$SCRIPT_DIR/phase${PHASE}_gate.sh"

if [ ! -f "$GATE_SCRIPT" ]; then
    echo "错误: Gate 脚本不存在: $GATE_SCRIPT"
    echo "可用 Phase: 0, 1, 2, 3"
    exit 1
fi

echo "运行 Phase $PHASE Gate..."
echo ""
bash "$GATE_SCRIPT"
EXIT_CODE=$?

echo ""
if [ $EXIT_CODE -eq 0 ]; then
    echo "🎉 Phase $PHASE 门禁通过，可以进入下一阶段"
else
    echo "⚠️  Phase $PHASE 门禁未通过，有 $EXIT_CODE 项失败需要修复"
fi

exit $EXIT_CODE
