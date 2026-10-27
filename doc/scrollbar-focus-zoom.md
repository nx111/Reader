# 隐藏滚动条、专注栏桌面透明与 Ctrl 滚轮

基线 `19fab82d760ecfd2cc19c0001799b6c4814e909b`。包含顶部退出去重调整；实现阶段没有手动触发 CI，发布构建须按准确提交单独核验。未增加第三方依赖、网络请求或缓存格式。

## 滚动条

`Scrollbar.cpp` 为 Reader 自有 TreeView、ListView、带滚动条的子 ListBox／Edit、导航 viewport 与 PDF viewport 接入统一条带。普通可重排正文原来按行／页导航，没有像素滚动范围，本轮不伪造第二套滚动位置。系统文件选择器、消息框及原生组合框弹出列表沿用系统界面。

保留原生非客户区滚动条的范围、条带宽度和 `SCROLLBARINFO` 几何，在同一父容器内用不获取焦点的 sibling 条带覆盖原有条带。目标已有 `WS_CLIPSIBLINGS`；隐藏与悬停只绘制条带，不增删目标窗口样式，不调整正文／列宽，不重新分页。范围确实消失时停止动画；横纵交汇只填相同背景。左右位置与 thumb 起止均取系统屏幕坐标，因此不会另猜 RTL 的方向。应用主窗仍为 LTR。

默认绘制背景；指针进入原有整条边缘命中区时显示 6 DIP 细滑块，视觉厚度随 DPI 缩放且不超出条带。轨道／滑块／悬停／按下采用主题背景混合的低对比灰阶。离开延迟 250 ms，再以 180 ms 短淡出。动画只在需要时启动 16 ms timer，重新进入、拖动结束、失活、取消、范围消失和销毁均处理计时器状态。

点击转交真实的 `WM_NCLBUTTONDOWN` 原生跟踪循环；由原控件持有 capture，保留行／页点击、长按、拖动、32 位 `nTrackPos` 和触摸晋升行为，不把 thumb 位置截成 16 位。滚轮继续交给原控件，键盘与内容控件的 UIA／MSAA 接口不替换；条带的 MSAA client 查询转交原始滚动条对象。高对比度模式直接显示系统原生滚动条。真实 UIA／屏幕阅读器、触摸与原生跟踪像素仍须 Windows 实测。

PDF／导航在修改原生范围后主动同步条带。PDF 打开期间固定保留两轴非客户区度量，用 `SIF_DISABLENOSCROLL` 保持范围为空时的宽度，空轨仅画背景，防止临界适宽时增删竖条导致反复重排；离开 PDF 后取消这些度量。悬停和淡出不调用 `ShowScrollBar`。

## 真实专注栏透明

此前颜色淡化已移除。`FocusRail.cpp` 建立同线程、同 DPI 感知上下文的 owned `WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE` 宿主，将已有导航 pane 及原生按钮整体迁入。宿主的背景和按钮一起使用真实窗口 alpha：静置 176／255，悬停、焦点、菜单、捕获或系统高对比度时 255。

仅把子控件半透明仍会看到主窗，因此主窗 region 用 `RGN_DIFF` 排除这块已预留的 48 DIP 客户区，桌面／后方应用才可进入合成。正文继续由独立阅读子窗绘制，专注期间主窗 alpha 强制 255（在全屏／无边框最终绘制状态确定后应用，避免过渡状态跳过设置）；用户原先配置的窗口 alpha 不改写，退出专注后恢复。不存在截图背景伪装、色键或 `WS_EX_TRANSPARENT` 点击穿透。

宿主跟随主窗位置／大小／可见／最小化／禁用状态，owned tool window 不另占任务栏；窗口管理命令转回主窗。坐标使用客户区屏幕原点与窗口边界换算，标题栏不在透明区域内。纯移动只移动宿主，region 只在相对形状／大小改变时更新；强调改变仅更新宿主 alpha。退出／API 失败回退／销毁时恢复 pane 的父窗与原 region，正确转移／释放 region 所有权。退出前先清除 active，避免 `SetWindowRgn` 同步位置消息重新挖孔。宿主和 pane 同步 UI state，导航焦点、Alt／F10、Tab、快捷键、菜单和 Esc 明确接受这个 owned 窗口层级。

API 失败时安全恢复普通不透明原生栏，不将混色当作桌面透明。当前没有 Windows 桌面，因此上述是已实现且经过 API 替身验证的合成路径；**没有实测后方应用像素、DWM 阴影／窗口圆角、AltTab／置顶／多屏移动、跨屏 DPI、全屏／无边框、模态焦点或闪烁**。

顶部重复退出按钮及 44 DIP 占位已删除；列表中的“退出专注，固定侧栏（Esc）”和原 F8／Esc 保留。临时目录、侧栏宽度和正文锚点恢复逻辑沿用原实现。

## Ctrl＋滚轮

仅指针位于正文、书籍已就绪、没有编辑和鼠标 capture 时，纵向 Ctrl wheel 才缩放；同一消息会被消费，不再翻页或改变窗口 alpha。先按区域路由目录／导航等控件，书库与搜索不改变正文字号。

`ReaderCtrlWheel` 累计高精度 delta，每 120 产生一步；反向自然抵消，Ctrl 按下／松开、离开作用域的 wheel、失活／取消、切书 token 变化清空余量。文字按当前窗口 DPI 换算字号，1–72 pt、每步 1 pt，重排从现有字符 index 开始；不添加导航历史或书签。PDF 复用页面缩放 25–400%、每步 25 个百分点，并保留页号与归一化位置。设置沿用已有 `Save`。

## 检查与 Windows 复核

本轮最终重跑 **99／99 通过，ASan／UBSan 99／99 通过**（LSan 关闭，预编译 PDFium 本身未重编 sanitizer）。正常／无网络各 **26／26** 翻译单元通过 MinGW 语法／API 类型检查，两种配置资源编译通过，项目 XML 与差异空白检查通过；这些不能代替 MSVC 链接或 Windows 桌面测试。

全量 99 项：原有 89 项（修订专注透明语义）＋新增 10 项。新增检查执行生产透明宿主和文字缩放桥接、原生 tracking 转发，覆盖 100–200% DPI 几何、32 位范围、命中、hover／capture／fade、重复拖动／进入退出、API 失败与同步 region 事件重入、Ctrl 余量／边界／作用域／anchor 及正文 opacity。独立执行真实 PDFium 核心；窗口／GDI／桌面部分使用替身，不是 Windows GUI。

Windows 实机复核应覆盖：默认隐藏和横纵交汇、长目录与虚拟列表、原生拖动／键盘／触摸／UIA、高对比度切换；任意应用作背景时左栏透出、正文保持不透明且不点穿；AltTab／模态／最小化／置顶与多屏 DPI；文字和 PDF 快速正反缩放及切书，确认锚点不漂移。CI 和构建包须以实际发布提交对应的运行单独核验，不将实现阶段的替身测试算作 Windows 验收。

实现依据：[GetScrollBarInfo](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getscrollbarinfo)、[WM_VSCROLL 的 32 位范围说明](https://learn.microsoft.com/en-us/windows/win32/controls/wm-vscroll)、[Layered／owned windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features)、[SetWindowRgn 坐标、同步消息与所有权](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowrgn)、[SetParent 与 UI state／DPI](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setparent)。
