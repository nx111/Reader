# 固定不透明窗口

基于 `fd161532db06949a2c4ba3fd614230c601018b34`，按最新要求取消旧整窗透明偏好。正常、专注、退出专注、全屏、无边框及重启均使用普通不透明主窗；专注只改变应用内配色。

## 实现

- `WindowAppearance.h` 统一清除 `WS_EX_LAYERED`／`WS_EX_TRANSPARENT`；创建主窗、所有主窗扩展样式写入及全屏恢复参数均应用同一策略。
- 旧 JSON `alpha` 字段为兼容保留，载入和保存时规范化为 255；普通／全屏恢复的扩展样式仅去掉透明相关位，不改字号、配色、位置和书籍数据。
- 字段结构不变，沿用缓存 schema 5。即使来源已经是 v5，只要透明字段发生规范化，也触发迁移备份：原始编码字节写入一次性 `.cache.dat.v5.bak` 及原有滚动 `.bak`，再走临时文件、flush、替换流程。备份／写入／flush／替换失败保留原文件，并允许重试；原文件仍含旧透明参数也不会使下一次启动透明，因为创建窗口前再次规范化。
- 删除主窗 `OnDraw`／`ResetLayerd`、全帧 layered surface、运行态 layered 标志和未用的透明背景位图助手；应用不再调用 `SetLayeredWindowAttributes`／`UpdateLayeredWindow`。全部模式继续使用已有独立阅读子窗和正文局部失效。
- 退休 Alt＋滚轮的文字透明操作；Ctrl＋滚轮缩放保持原实现。没有独立透明度设置控件需要保留或隐藏。文字位图覆盖度固定为 255；PNG、图标、字体抗锯齿等内部位图 alpha 仍可在应用背景上合成，不是窗口透出桌面。

## 定向验证

遵照用户最新要求，只运行本次直接涉及的 **7 项检查**，不运行整套 99 项、格式／PDF／书库 UI 回归或 sanitizer 矩阵。

- 旧透明配置备份、恢复、失败重试和不相关字段保留。
- 旧格式迁移保留进度、书签、顺序与设置。
- 损坏、未来版本和不完整缓存不被覆盖。
- 生产样式策略覆盖正常／专注／退出／全屏／无边框／重启、保存参数恢复和重复应用。
- 旧透明 API／入口不能恢复窗口透明。
- 实际正文失效函数保持 child 范围。
- 跳转、滚动和不透明绘制调用链约束。

7／7 通过。Reader／Cache／Page 三个修改单元在联网、无网络配置下分别通过 MinGW 语法／API 类型检查；新增头文件工程注册、XML 和差异检查通过。资源未改，未重编资源；没有 MSVC 链接或 Windows 桌面运行环境，实际模式切换、DPI／焦点／UIA／触摸和闪烁仍未实测。CI／构建包须以最终发布提交核验。

复现命令：

```sh
PYTHONPATH=tests python -m unittest test_opaque_window \
  test_reader_ui.PaintBoundaryTests.test_actual_reading_invalidation_excludes_parent_and_siblings \
  test_reader_ui.UiSourceContractTests.test_jump_scroll_and_opaque_render_call_chains -v
```
