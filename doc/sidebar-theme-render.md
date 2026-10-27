# 侧栏、主题与绘制隔离

实现基线：`39b926e381463b15266f72dd95f7a4c8b6bc47b0`。工作分支：`usability/sidebar-theme-render`。本说明记录实现和发布前验证；后续已获准合并推送，准确提交及 CI 状态以发布交付记录为准。没有创建 release 或部署。

## 使用变化

- 目录和书签列表保留原生纵向、横向滚动条；内容超出时显示。长标题可横向滚动、查看信息提示，或选中后按 `Ctrl+C` 复制完整标签。滚轮按指针所在区域分发；`Shift+滚轮`、横向滚轮滚动当前列表的水平方向。正文滚轮继续原有翻行、透明度等操作。
- 停靠目录与正文间有 6dp 分隔条。鼠标指针显示横向调整形状；拖动时捕获鼠标，放开后保存宽度。分隔条可用 Tab 聚焦，左右方向键每次调整 8dp，Home 恢复 240dp。Esc、捕获丢失、焦点丢失、尺寸或 DPI 变化取消当前拖动。
- 侧栏宽度按 96 DPI 逻辑单位保存，范围 160–640dp，同时为正文留下至少 320dp。窗口收窄只限制本次实际布局，不覆盖用户保存的宽度。小于 700dp 的窗口使用临时目录抽屉并隐藏分隔条；抽屉最多 360dp，同时为关闭操作留下至少 40dp 正文区域。
- “显示设置”增加明亮／深色界面主题，以及“使用自定义正文颜色和背景图（保留原设置）”。新缓存默认正文跟随界面主题；从旧缓存迁移时默认继续使用原正文配色和背景图。取消设置不会修改主题或自定义开关；确认时统一应用、保存。原字体、颜色及图片值始终保留，可重新启用。
- 集中 palette 覆盖自有窗口客户区、工具栏按钮、状态栏、分隔条、目录、书库、搜索、书签、设置、菜单与信息提示。按钮和菜单保留原生输入／命令处理；绘制焦点、选中、悬停、按下和禁用状态。菜单提供 MSAA 文本与助记键处理。应用主题时尊重系统高对比配色；明确开启的自定义正文颜色仍由用户控制。
- 系统文件／文件夹选择器、字体／颜色选择器、消息框、系统标题栏，以及原生控件的非客户区边框／滚动条由 Windows 管理。本轮没有使用私有深色 API，也不声称这些系统 UI 会跟随应用主题。

## 闪烁的源码根因与修复

原调用链为 `Book::JumpChapter` → `Page::ReDraw` → `Invalidate(main, TRUE, FALSE)`。旧 `Invalidate` 对父窗口除状态栏外的客户区失效；旧 `OnPaint` 为该完整区域分配位图、填背景、最后整区 `BitBlt`。侧栏和顶栏也是父窗口的子控件，主窗口缺少 `WS_CLIPCHILDREN`，所以正文滚动／翻页／搜索高亮／自动翻页同样进入跨越其他 UI 的绘制路径。在线加载回调还会重复重建未变化的目录。这是源码确认，并非 Windows 录屏或实机观测。

正常绘制现在经过独立的 `ReaderContentView` 子窗口：

1. 停靠目录、分隔条、顶栏与正文采用不重叠的布局；父窗口 `WS_CLIPCHILDREN` 和正文 `WS_CLIPSIBLINGS` 保证各自绘制边界。窄窗抽屉有意覆盖，但正文 DC 同样排除上层兄弟控件。
2. `Invalidate(..., TRUE, ...)` 只使正文子窗口失效，并禁止背景擦除；正文 `WM_PAINT` 使用子窗口局部坐标和持久 DIB，最后一次复制受 `BeginPaint` 脏区域约束。父窗口只填充界面空隙，不再绘制正文。
3. 阅读跳转不重建、隐藏／重显其他控件；布局只移动实际几何发生变化的控件。目录与书签模型未改变时跳过删除重建。菜单更新仅准备和重绘菜单，不再使整套子控件失效；后台更新通过消息回到 UI 线程。
4. 自动章节同步仅使旧／新章节行失效，不调用 `TreeView_SelectItem` 或 `EnsureVisible`；只有用户打开目录的显式定位会选择、滚动。程序同步处于激活保护中，不会再次跳章。进度与标题文字未变化时不重复发送更新。
5. 普通绘制、透明合成和透明文字使用可复用 DIB/DC，尺寸变化时才替换，释放前先取消选择。正文缩得放不下一个字时，纯重排保持原阅读锚点；透明文字像素循环裁剪到缓冲边界。

`UpdateLayeredWindow` 逐像素透明路径仍需整帧提交，无法声明为系统局部提交。现在复用合成和文字缓冲，不重建子控件。透明／全屏／无边框模式显示目录、书签或顶栏时沿用整体透明的正常子窗口绘制；隐藏界面后恢复逐像素合成。

## 缓存与工作流

缓存 schema 为 4。根 `ui` 保存 `theme`（0/1）、`reading_custom`（0/1）和 `sidebar_width`（160–640）。读取旧 schema 时保留原 ID、顺序、进度、书签、时间及配色；首次保存前保留对应源版本备份，包括 `.cache.dat.v3.bak`。字段错误、截断或未来版本缓存拒绝覆盖。拖拽中只改变预览宽度，正文重绘引发的保存不会写入未确认的宽度；鼠标释放后才更新设置。

工作流只更新三个 Action，其他触发、权限、MSBuild 参数、打包命令及产物名称／路径均保留：

- `actions/checkout@v7`：[官方 action.yml](https://raw.githubusercontent.com/actions/checkout/v7/action.yml)，`runs.using: node24`。
- `actions/upload-artifact@v7`：[官方 action.yml](https://raw.githubusercontent.com/actions/upload-artifact/v7/action.yml)，Node.js 24；保留默认 `archive: true`，不更改现有包含 `.7z` 的 zip 产物语义。
- `microsoft/setup-msbuild@v3`：[官方 action.yml](https://raw.githubusercontent.com/microsoft/setup-msbuild/v3/action.yml)，`runs.using: node24`。

## 本地验证与边界

执行环境为 Linux，不具备真实 Windows 桌面。

- **通过：57 项可移植回归**（原有格式／解析 20，缓存／书库／桥接 21，搜索／文本 7，绘制／UI 9）。新用例覆盖 v3→v4 备份、设置持久化与损坏保护、100–200% DPI 下的宽度限制／取消／重复拖动、主题对比度／自定义语义、实际正文 `OnPaint`／`Invalidate`／章节同步函数，以及实际 `CalcPageDown` 在无法排下一字时保留锚点。GDI 替身验证 200 次重复绘制、分配复用和释放顺序；不是原生控件像素测试。
- **通过：57 项 ASan/UBSan 回归**。环境中关闭 LeakSanitizer，不能宣称泄漏检查通过；绘制资源计数和释放顺序另外由替身断言验证。
- **通过：20 个相关编译单元的 MinGW x64 类型／语法检查**，联网与无网络配置各一轮。检查脚本保留对既有 MSVC 特性的兼容参数；不等同于 MSVC 链接构建。
- **通过：完整中文资源脚本的 MinGW windres 编译**。仅在临时目录中把 UTF-16 脚本／资源头无损转换为 UTF-8，并使用相应 codepage 和大小写头文件别名；仓库资源仍保留 UTF-16。
- **通过：差异空白检查、项目 XML 注册和工作流参数回归**。远端 master 核对仍为上述基线，没有覆盖并发变更。
- **未运行：真实 Windows GUI、原生 GDI 像素、快速点击录屏、100–200% 实机 DPI、透明／全屏／无边框、屏幕阅读器及 MSVC 完整构建**。数学布局、源码契约和 GDI 替身结果不能替代这些检查。
- **发布前未运行：本轮 Windows CI**。此前手动触发 `build-package.yml` 被 `Forbidden` 拒绝；本轮按限制未重试或绕过。推送后仅查询自动或用户触发的运行；构建与包状态以准确提交的交付结果为准，空 CI 列表不能算作通过。

复验命令见 [tests/README.md](../tests/README.md)。原始本地日志在 `/tmp/reader-verify-tests.log`、`/tmp/reader-verify-sanitizers.log`、`/tmp/reader-complete-syntax-network.log`、`/tmp/reader-complete-syntax-offline.log`、`/tmp/reader-resource-check.log`，不作为发布产物。

## 待 Windows 实机复核

- [ ] 正常窗口和 100/125/150/175/200% DPI：滚轮在目录、书签、正文间移动，横向滚动长标题，键盘操作和完整标签复制。
- [ ] 调整宽度到两侧极限、连续拖动、移出窗口后释放、Esc、Alt+Tab、失去捕获、缩窄为抽屉、DPI 变化；取消后重启仍是原保存宽度。
- [ ] 在普通／全屏／无边框／逐像素透明及整体透明回退间切换；开关目录、书签、专注，确认锚点、焦点与侧栏滚动位置保持。
- [ ] 快速反复点击目录，在线成功／失败／取消／过期响应，正文滚轮、翻页、自动翻页、查找高亮；录屏检查只更新必要区域，监测 GDI 对象数量。
- [ ] 明暗主题确认／取消／重启，旧缓存自定义配色与背景图，所有自有弹窗、下拉列表、菜单、禁用／悬停／焦点，以及系统高对比模式。
- [ ] Narrator／其他屏幕阅读器识别控件名称、菜单助记键和分隔条说明；验证 Tab、Shift+Tab、空格、Enter、Esc 的原生行为。

## 文件清单

| 范围 | 文件（相对仓库） |
| --- | --- |
| 正文与布局 | `Reader/Reader.cpp`、`Reader/ReadingState.h`、`Reader/GdiSurface.h`、`Reader/Page.cpp`、`Reader/Page.h` |
| 主题与设置 | `Reader/Theme.cpp`、`Reader/Theme.h`、`Reader/ThemePalette.h`、`Reader/DisplaySet.cpp`、`Reader/Reader_zh-cn.rc`、`Reader/resource.h` |
| 设置存储 | `Reader/types.h`、`Reader/Cache.cpp`、`Reader/Jsondata.cpp` |
| 自有界面接入 | `Reader/Advset.cpp`、`Reader/BooksourceDlg.cpp`、`Reader/Editctrl.cpp`、`Reader/Keyset.cpp`、`Reader/LibraryDlg.cpp`、`Reader/OnlineDlg.cpp`、`Reader/SearchDlg.cpp`、`Reader/WebDlg.cpp`、`Reader/tagset.cpp` |
| 项目与工作流 | `Reader/Reader.vcxproj`、`Reader/Reader.vcxproj.filters`、`.github/workflows/build-package.yml` |
| 测试 | `tests/check_windows_syntax.py`、`tests/library_state_test.cpp`、`tests/test_library_state.py`、`tests/reader_paint_compat.h`、`tests/reader_paint_test.cpp`、`tests/test_reader_ui.py` |
| 文档 | `doc/sidebar-theme-render.md`、`doc/library-usability.md`、`tests/README.md` |
