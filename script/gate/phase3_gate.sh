#!/bin/bash
# Phase 3 Gate: 交叉测试模式验证
# 用法: bash script/gate/phase3_gate.sh

set -e
echo "===== Phase 3 Gate: 交叉测试模式验证 ====="

PASS=0
FAIL=0

# Test 1: Cross 模块文件完整性
echo -n "[1/6] Cross 模块文件完整性... "
FILES=("src/cross/cross_tester.cc" "src/cross/cross_tester.hh" "src/cross/cross_main.cc")
ALL_OK=true
for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then echo "MISSING: $f"; ALL_OK=false; fi
done
if $ALL_OK; then echo "PASS"; ((PASS++))
else echo "FAIL"; ((FAIL++)); fi

# Test 2: 编译通过
echo -n "[2/6] Cross 模式编译... "
if cmake --build build -j$(nproc) 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 3: 状态管理流程（三次执行间的 backup/restore）
echo -n "[3/6] 状态管理流程... "
if grep -q "dut_reset_to_backup\|reset_to_backup" src/cross/cross_main.cc 2>/dev/null; then
    RESTORE_COUNT=$(grep -c "reset_to_backup" src/cross/cross_main.cc 2>/dev/null || echo 0)
    if [ "$RESTORE_COUNT" -ge 2 ]; then
        echo "PASS ($RESTORE_COUNT restore points)"; ((PASS++))
    else
        echo "FAIL (仅 $RESTORE_COUNT 个 restore 点，需要 ≥2)"; ((FAIL++))
    fi
else
    echo "FAIL"; ((FAIL++))
fi

# Test 4: 三路比较逻辑
echo -n "[4/6] 三路比较逻辑... "
# 检查是否有 txcheck/eet/cross 三种 bug 报告类型
TYPES=0
grep -q "txcheck" src/cross/cross_main.cc 2>/dev/null && ((TYPES++))
grep -q "eet" src/cross/cross_main.cc 2>/dev/null && ((TYPES++))
grep -q "cross" src/cross/cross_main.cc 2>/dev/null && ((TYPES++))
if [ $TYPES -ge 3 ]; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL (仅 $TYPES 种比较)"; ((FAIL++))
fi

# Test 5: SELECT-only 变换约束
echo -n "[5/6] SELECT-only 变换约束... "
if grep -q "is_select_like_stmt\|SELECT_READ\|select.*transform" src/cross/cross_main.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 6: 假阳性二次验证
echo -n "[6/6] Cross 模式二次验证... "
if grep -q "validat\|re.*execut\|double.*check\|二次" src/cross/cross_main.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

echo ""
echo "===== Phase 3 Gate 结果: $PASS PASS, $FAIL FAIL ====="
[ $FAIL -eq 0 ] && echo "GATE PASSED ✅" || echo "GATE FAILED ❌"
exit $FAIL
