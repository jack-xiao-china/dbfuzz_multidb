#!/bin/bash
# Phase 2 Gate: TxCheck 模式集成验证
# 用法: bash script/gate/phase2_gate.sh

set -e
echo "===== Phase 2 Gate: TxCheck 模式集成验证 ====="

PASS=0
FAIL=0

# Test 1: TxCheck 模块文件完整性
echo -n "[1/12] TxCheck 模块文件完整性... "
FILES=("src/txcheck/transaction_test.cc" "src/txcheck/transaction_test.hh" \
       "src/txcheck/instrumentor.cc" "src/txcheck/instrumentor.hh" \
       "src/txcheck/dependency_analyzer.cc" "src/txcheck/dependency_analyzer.hh" \
       "src/txcheck/tx_general_process.cc" "src/txcheck/tx_general_process.hh" \
       "src/txcheck/tx_main.cc")
ALL_OK=true
for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then echo "MISSING: $f"; ALL_OK=false; fi
done
if $ALL_OK; then echo "PASS"; ((PASS++))
else echo "FAIL"; ((FAIL++)); fi

# Test 2: 编译通过
echo -n "[2/12] TxCheck 模式编译... "
if cmake --build build -j$(nproc) 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 3: 已知 bug 修复 - compare_content
echo -n "[3/12] compare_content bug 修复... "
if grep -q "a_content.end()" src/txcheck/tx_general_process.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL (仍为 a_content.begin())"; ((FAIL++))
fi

# Test 4: 已知 bug 修复 - set_intersection
echo -n "[4/12] set_intersection bug 修复... "
if grep -q "i_primary_set.end()" src/txcheck/dependency_analyzer.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 5: txn_mode 参数
echo -n "[5/12] txn_mode 参数约束... "
if grep -q "txn_mode" src/grammar/grammar.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 6: write_op_id / row_id 计数器
echo -n "[6/12] write_op_id / row_id 计数器... "
if grep -q "write_op_id" src/grammar/grammar.cc 2>/dev/null && \
   grep -q "row_id" src/grammar/grammar.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 7: P0 - retry_block_stmt 递归重试
echo -n "[7/12] retry_block_stmt 递归重试... "
if grep -q "retry_block_stmt" src/txcheck/transaction_test.cc 2>/dev/null && \
   grep -A5 "is_commit\|is_abort" src/txcheck/transaction_test.cc 2>/dev/null | grep -q "retry_block_stmt"; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL (递归调用缺失)"; ((FAIL++))
fi

# Test 8: P0 - infer_instrument_after_blocking
echo -n "[8/12] infer_instrument_after_blocking... "
if grep -q "infer_instrument_after_blocking" src/txcheck/transaction_test.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 9: P0 - 多轮状态恢复
echo -n "[9/12] multi_stmt_round_test 状态恢复... "
if grep -q "init_stmt_queue" src/txcheck/transaction_test.cc 2>/dev/null && \
   grep -q "init_tid_queue" src/txcheck/transaction_test.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 10: G2/GSI 检查代码保留（注释状态）
echo -n "[10/12] G2/GSI 检查代码保留... "
if grep -q "check_G2_item" src/txcheck/dependency_analyzer.cc 2>/dev/null && \
   grep -q "check_GSIa" src/txcheck/dependency_analyzer.cc 2>/dev/null && \
   grep -q "check_GSIb" src/txcheck/dependency_analyzer.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 11: DBMS 语法分支保留
echo -n "[11/12] DBMS 特定语法分支... "
DBMS_BRANCHES=0
grep -q 'target_dbms.*clickhouse' src/grammar/grammar.cc 2>/dev/null && ((DBMS_BRANCHES++))
grep -q 'target_dbms.*sqlite' src/grammar/grammar.cc 2>/dev/null && ((DBMS_BRANCHES++))
grep -q 'target_dbms.*cockroach' src/grammar/grammar.cc 2>/dev/null && ((DBMS_BRANCHES++))
grep -q 'target_dbms.*yugabyte' src/grammar/grammar.cc 2>/dev/null && ((DBMS_BRANCHES++))
if [ $DBMS_BRANCHES -ge 3 ]; then
    echo "PASS ($DBMS_BRANCHES DBMS branches)"; ((PASS++))
else
    echo "FAIL (only $DBMS_BRANCHES branches)"; ((FAIL++))
fi

# Test 12: instrumentor 适配 get_version_key_name
echo -n "[12/12] instrumentor 适配双命名... "
if grep -q "get_version_key_name\|version_key" src/txcheck/instrumentor.cc 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

echo ""
echo "===== Phase 2 Gate 结果: $PASS PASS, $FAIL FAIL ====="
[ $FAIL -eq 0 ] && echo "GATE PASSED ✅" || echo "GATE FAILED ❌"
exit $FAIL
