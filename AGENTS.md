# AGENTS.md — DemonCAT 代理指南

## 构建与测试

本仓库为 C11 POSIX 项目（`dlfcn.h`/`dirent.h`/`/proc/self/exe`），**仅 Linux/WSL 可构建**。
当前 win32 环境无 cmake/gcc/wsl，代码改动仅做静态验证。**下次有 WSL 环境时必须运行：**

```bash
cmake -B build && cmake --build build && ctest --test-dir build --output-on-failure
# 插件专项：
ctest --test-dir build -R plugin --output-on-failure
```

如 `plugins/libsample.so` 缺失（已从 git 取消跟踪，须本地构建）：
```bash
cmake --build build --target sample_plugin
```

## 约定

- 提交信息：Conventional Commits 中文（`fix(范围): 描述` / `feat: ...` / `refactor: ...`）
- 代码风格：`-Wall -Wextra -Werror`，C11，`_POSIX_C_SOURCE=200809L`
- 三层 dispatch 优先级：cnf > 编译注入器 > 动态插件，未命中退出码 4
