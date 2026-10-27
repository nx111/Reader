# 可用性阶段发布记录

## 第一、二阶段

- 推送提交：[2a87bac8aebd6207da892e0fabd38938457146a5](https://github.com/nx111/Reader/commit/2a87bac8aebd6207da892e0fabd38938457146a5)，基于 `2166e07243b96568ed5471380e6afd8d24016bf9`，普通快进推送至 `master`；远端 ref 已核对。
- 本地验证：32/32 可移植回归通过（原有格式／解析 20 项，书库／缓存／导航 12 项）。解析与书库启用 ASan/UBSan 通过，LeakSanitizer 未启用。
- Windows 类型／语法：11 个改动编译单元，MinGW x64 网络／无网络配置均通过；不是 MSVC 链接构建。
- 对准确提交查询 GitHub Actions 的结果为 0 个运行；不能据此判定通过。`gh workflow run build-package.yml --repo nx111/Reader --ref master` 被 `Forbidden` 拒绝，未确认产生运行。现有 GitHub 连接器没有提供工作流触发动作，未改用其他通道重试被拒绝动作。
- 因此本阶段 Windows CI 为“未运行”，没有可交付的新构建包。仓库中已存在的旧格式任务运行不是本次可用性变更的验证。
- 真实 Windows 正常／全屏／无边框／透明模式、焦点／滚轮、100–200% DPI 和屏幕阅读器操作未运行。

## 第三阶段

本阶段提交包含搜索结果列表、高亮、章名与摘要书签、稀疏详情的缓存 v3，以及书库多选收藏／阅读状态／移出。限定范围与人工检查项目见 [使用与迁移说明](library-usability.md)。测试命令见 [tests/README.md](../tests/README.md)。最终推送 SHA、准确提交的 Actions 结果及构建包状态以交付记录为准；此文不将可移植测试或空 CI 列表作为 Windows 构建成功。

第三阶段发布前本地终验：44/44 可移植回归通过；解析、缓存和搜索启用 ASan/UBSan，LeakSanitizer 关闭。13 个改动编译单元在 MinGW x64 网络／无网络配置下均通过类型／语法检查。真实 Windows GUI、DPI、透明模式和屏幕阅读器测试仍未运行。
