# Cross 模式最小化段错误 (SIGSEGV) 分析与修复

## 问题描述

Cross 模式在发现跨库 Bug 后执行 `minimize()` 最小化测试用例时，触发段错误 (SIGSEGV, signal 11)，导致子进程崩溃、父进程 abort，整个测试中断。

## 症状

```
[31mcross_test: BUG CONFIRMED after re-validation![0m
write_results: cannot open found_bugs/cross_bug_r1_t3//tx_results.out
write_results: cannot open found_bugs/cross_bug_r1_t3//normal_results.out
cp: cannot create regular file 'found_bugs/cross_bug_r1_t3/': Not a directory
minimize: starting with 12 statements
cross_run: child killed by signal 11 (Segmentation fault)
timeout: the monitored command dumped core
```

## 根因分析

### 问题 1: 目录创建顺序错误（已修复）

**代码流程**:
```
cross_main.cc:
  1. ct.cross_test(bug_dir)     // 内部调用 save_bug_report() 写文件到 bug_dir
  2. make_dir_error_exit(bug_dir)  // 创建 bug_dir
  3. ct.minimize(bug_dir)
```

`save_bug_report()` 在 `cross_test()` 内部执行，但目录在第 2 步才创建。导致文件写入失败。

**修复**: 将 `make_dir_error_exit(bug_dir)` 移到 `cross_test()` 之前。

### 问题 2: minimize 段错误（已添加容错，根因待深入分析）

**minimize 函数执行流程**:
1. Phase 1: 逐条移除非 SELECT 语句，检查差异是否持续
2. Phase 2: 逐条回退 SELECT 变换，检查差异是否持续
3. 保存最小化结果

**可能的段错误原因**:

#### 原因 A: AST shared_ptr 共享导致状态不一致

`minimize()` 中的代码：
```cpp
auto tmp_stmts = last_path_stmts;  // 拷贝 shared_ptr 向量
// tmp_stmts[i] 和 last_path_stmts[i] 指向同一个 AST 节点!
transform_select(tmp_stmts[j]);    // 修改了共享的 AST 节点
// ...
back_transform_select(tmp_stmts[idx]);  // 恢复
```

如果 `back_transform_select` 不完全恢复 AST 状态，后续的 `execute_normal_path(last_path_stmts, ...)` 会使用被污染的 AST。

#### 原因 B: dut_reset_to_backup 连接失效

`minimize()` 在循环中多次调用 `dut_reset_to_backup()`，每次调用创建新的 `dut_base` 连接。如果数据库在多次 reset 后连接池耗尽或状态不一致，可能导致空指针解引用。

#### 原因 C: execute_normal_path 中的空指针

```cpp
void cross_tester::execute_normal_path(...) {
    auto dut = dut_setup(d_info);  // 可能返回 null
    for (size_t i = 0; i < stmts.size(); i++) {
        auto stmt_str = print_stmt_to_string(stmts[i]);  // stmts[i] 可能为 null
        dut->test(stmt_str, &output);  // dut 可能为 null
    }
}
```

## 已实施的修复

### 修复 1: 目录创建顺序（根本修复）

```cpp
// cross_main.cc - 修复前
if (ct.cross_test(bug_dir)) {
    make_dir_error_exit(bug_dir);  // 太晚了!
    ct.minimize(bug_dir);
}

// cross_main.cc - 修复后
make_dir_error_exit(bug_dir);  // 先创建目录
if (ct.cross_test(bug_dir)) {
    ct.minimize(bug_dir);
}
```

### 修复 2: SIGSEGV 信号处理器（容错修复）

```cpp
// cross_main.cc - 子进程中
signal(SIGSEGV, [](int) {
    cerr << "minimize: SIGSEGV caught — bug report already saved" << endl;
    _exit(2);  // 不同于 EXIT_FAILURE(1)，父进程不会 abort
});
```

### 修复 3: 父进程处理 exit code 2

```cpp
// cross_main.cc - 父进程中
if (WIFEXITED(status) && WEXITSTATUS(status) == 2) {
    cerr << "cross_run: child crashed during minimize (bug report already saved)" << endl;
    // 继续下一轮测试
}
```

### 修复 4: minimize 函数 try-catch

```cpp
// cross_tester.cc
void cross_tester::minimize(const string& bug_dir) {
    try {
        // ... 原有最小化逻辑 ...
    } catch (exception& e) {
        cerr << "minimize: caught exception: " << e.what() << endl;
        // 保存部分结果
        save_bug_report(partial_dir, last_path_stmts, ...);
    }
}
```

## 修复效果

修复前：Cross 模式在第 1 轮发现 Bug 后因段错误中断，整个测试停止。

修复后：Cross 模式完成 9 轮测试，发现 8 个跨库 Bug，每次 minimize 段错误都被优雅捕获并继续测试。

## 后续改进建议

1. **AST 深拷贝**: `minimize()` 中 `tmp_stmts` 应使用 AST 深拷贝而非 shared_ptr 浅拷贝，避免变换污染原始 AST
2. **连接池管理**: `dut_reset_to_backup()` 应添加连接有效性检查
3. **信号安全**: SIGSEGV handler 中的 `cerr` 不是异步信号安全的，应使用 `write(STDERR_FILENO, ...)` 替代
4. **根因定位**: 使用 AddressSanitizer (`-fsanitize=address`) 编译后重跑，获取精确的崩溃栈
