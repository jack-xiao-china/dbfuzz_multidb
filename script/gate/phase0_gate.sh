#!/bin/bash
# Phase 0 Gate: 基础搭建验证
# 用法: bash script/gate/phase0_gate.sh

set -e
echo "===== Phase 0 Gate: 基础搭建验证 ====="

PASS=0
FAIL=0

# Test 1: 目录结构检查
echo -n "[1/6] 目录结构检查... "
DIRS=("src/core" "src/grammar" "src/expr" "src/expr/bool_expr" "src/expr/bool_expr/bool_binop" \
      "src/schema" "src/txcheck" "src/eet" "src/eet/qcn_tester" "src/cross")
ALL_DIRS_OK=true
for d in "${DIRS[@]}"; do
    if [ ! -d "$d" ]; then
        echo "MISSING: $d"
        ALL_DIRS_OK=false
    fi
done
if $ALL_DIRS_OK; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 2: 关键文件存在性
echo -n "[2/6] 关键文件检查... "
FILES=("src/core/relmodel.cc" "src/core/relmodel.hh" "src/core/prod.cc" "src/core/prod.hh" \
       "src/core/random.cc" "src/core/random.hh" "src/core/impedance.cc" "src/core/impedance.hh" \
       "src/core/dump.cc" "src/core/dump.hh" "src/core/log.cc" "src/core/log.hh" \
       "src/core/dbms_info.cc" "src/core/dbms_info.hh" "src/core/general_process.cc" "src/core/general_process.hh" \
       "src/expr/printed_expr.cc" "src/expr/printed_expr.hh" \
       "src/expr/value_expr.cc" "src/expr/value_expr.hh" \
       "src/expr/bool_expr/bool_expr.cc" "src/expr/bool_expr/bool_expr.hh" \
       "src/expr/bool_expr/bool_binop/bool_term.cc" "src/expr/bool_expr/bool_binop/bool_term.hh" \
       "src/schema/schema.cc" "src/schema/schema.hh" "src/schema/dut.cc" "src/schema/dut.hh" \
       "src/main.cc" "CMakeLists.txt" "src/CMakeLists.txt")
ALL_FILES_OK=true
for f in "${FILES[@]}"; do
    if [ ! -f "$f" ]; then
        echo "MISSING: $f"
        ALL_FILES_OK=false
    fi
done
if $ALL_FILES_OK; then
    echo "PASS (${#FILES[@]} files)"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 3: CMake 配置
echo -n "[3/6] CMake 配置... "
if cmake -B build 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 4: CMake 编译
echo -n "[4/6] CMake 编译... "
if cmake --build build -j$(nproc) 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 5: 双命名机制检查（代码级）
echo -n "[5/6] 双命名机制（get_version_key_name）... "
if grep -q "get_version_key_name" src/schema/schema.hh 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

# Test 6: env_setting_stmts 检查（代码级）
echo -n "[6/6] env_setting_stmts 机制... "
if grep -q "env_setting_stmts" src/schema/dut.hh 2>/dev/null && \
   grep -q "supported_setting" src/schema/schema.hh 2>/dev/null; then
    echo "PASS"; ((PASS++))
else
    echo "FAIL"; ((FAIL++))
fi

echo ""
echo "===== Phase 0 Gate 结果: $PASS PASS, $FAIL FAIL ====="
[ $FAIL -eq 0 ] && echo "GATE PASSED ✅" || echo "GATE FAILED ❌"
exit $FAIL
