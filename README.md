# 显示器输入源任务栏插件

DDE/统信 UOS 任务栏插件。点击任务栏中的显示器图标，在面板中为每个显示器选择 HDMI、DisplayPort、DVI 等输入源并点击「切换」。

**兼容范围：能够识别屏幕不代表能够切换其输入源。并非每台显示器都支持 DDC/CI；支持 DDC/CI 也不保证实现 VCP `0x60` 输入源控制。本版本仅为支持相应控制且连接链路允许访问的显示器提供切换，其他屏幕显示不可用原因。通用软件无法保证切换所有型号的硬件输入源。**

## 功能

- 合并 Linux DRM 已连接显示器、Qt 桌面屏幕与 libddcutil 检测结果；显示名称、接口、序列号和当前输入源。
- 从显示器 capabilities 的 VCP `0x60` 字段获取可选输入源；不将通用输入源列表冒充实际硬件能力。
- 内置屏、虚拟屏、DDC/CI 不支持及无权限的显示器保留在列表中，并说明原因。
- 手动模式支持未报告的常见输入源及自定义十六进制 VCP 值。USB-C 没有通用的输入源编号，必须按显示器资料填写，未知值按 `0xNN` 展示。
- 桌面屏幕增删时自动刷新，每次打开面板时重新检测，也支持手动刷新。
- libddcutil 在独立进程中运行，操作串行，60 秒超时；进程间互斥，避免两个面板同时操作。
- 每次切换重新检测，并核对 I²C 总线及完整的 128 字节基础 EDID。相同型号或重复 EDID 不按列表顺序猜测。
- 只有点击「切换」才写入硬件。指令成功发送后不假称验证了画面，不自动重试写入。

输入源切换依赖外接显示器支持 DDC/CI，且在显示器 OSD 菜单中启用。输入源是**显示器自身的硬件输入**，例如将一块显示器从当前电脑的 DP 切至另一台电脑的 HDMI；不是桌面复制/扩展设置。切至其他设备后，此电脑可能失去画面和 DDC 控制，恢复时可能需要使用显示器物理按键。

## 构建与调试

目标：提供 `PluginsItemInterface` 1.x 兼容接口的 DDE Dock，Qt 5.11+，C++14，libddcutil 0.9.0+。本项目使用兼容插件元数据 `api: 1.1.1`。新版仅支持 Qt6/QML 接口的任务栏需要另行适配。

```bash
sudo apt-get install build-essential cmake pkg-config qtbase5-dev dde-dock-dev libddcutil-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j4
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure

# 示例面板，不访问硬件
./build/input-source-panel --demo
# 真实显示器调试面板，不需要加载插件
./build/input-source-panel
# 只读检测，输出 JSON
./build/dde-dock-input-source-helper scan
```

构建生成：`libdock-input-source.so`、`dde-dock-input-source-helper` 和 `input-source-panel`。也可用 `-DDOCK_INCLUDE_DIR=/path/to/dde-dock/interfaces` 指定源码接口目录。

## 安装

```bash
sudo cmake --install build
```

默认插件安装到 `/usr/lib/dde-dock/plugins/`，辅助程序到 `/usr/libexec/dde-dock-input-source/`（取决于 `CMAKE_INSTALL_LIBEXECDIR`），调试面板到 `/usr/bin/`。请保证**配置时的前缀与最终安装位置一致**；helper 路径在配置时确定。安装完成后重新登录桌面，再点击任务栏显示器图标。插件支持任务栏中的启用/禁用与位置记忆。

开发时可通过 `DDE_INPUT_SOURCE_HELPER=/absolute/path/to/build/dde-dock-input-source-helper` 覆盖辅助程序路径。独立调试面板优先查找与自身同目录的辅助程序。安装包包含 AMD DisplayPort AUX 设备的 udev 规则，仅授权当前活动的本地桌面用户；不会开放主板 SMBus。安装后需由管理员重新加载规则并触发对应设备事件，或重启系统使规则生效。

## 硬件条件与故障排查

1. 在显示器自身的 OSD 菜单中启用 DDC/CI。部分 KVM、扩展坞、转接器不透传 DDC。
2. 如果没有 `/dev/i2c-*`，可执行 `sudo modprobe i2c-dev` 后重新检测。持久加载方式由系统配置决定。
3. AMD Display Core 的 DP 控制通道可能是接口目录内的 AUX I²C 适配器（例如 i2c-5），而非 `ddc` 链接指向的普通 I²C（例如 i2c-2）。本插件优先使用该接口唯一的 AUX 适配器。当前用户须具备相关 `/dev/i2c-N` 的读写权限。使用发行版的 udev 规则或已有 `i2c` 用户组管理权限；组变更后重新登录。不要以 root 启动任务栏。
4. capabilities 可能缺失或错误。确认硬件实际支持后可使用手动模式，自定义值为十六进制。仅列出标准名称，厂商自定义值不会自动推断为 USB-C。
5. 切换发送成功只代表 DDC 写操作成功。切换离开当前电脑后，屏幕可能从列表消失，无法保证能从此电脑切回。
6. 当前控制通道为 I²C DDC/CI（包括能够透传的 HDMI、DP、USB-C DP Alt Mode）；不支持仅通过 USB HID 控制的显示器、MCCS 3.0 表格式输入源及厂商私有分屏协议。
7. Qt 未收到通知的 DRM 变化可点击刷新。DDC 设备繁多或响应很慢时，60 秒检测可能超时；桌面屏幕仍保留在面板中。

## 测试与预览

```bash
QT_QPA_PLATFORM=offscreen ./build/input-source-panel --demo --screenshot build/panel-demo.png
QT_QPA_PLATFORM=offscreen ./build/input-source-panel --quit-after 1500
```

自动化测试覆盖后台程序缺失与无效响应、非法目标拒绝、输入源编号、EDID 与总线匹配、重复 EDID、多屏 DRM 枚举、AMD DisplayPort AUX 与普通 I²C 的区分、不支持的设备、手动模式、真实 Qt 插件加载、插件生命周期及面板交互。硬件写操作应在连接真实显示器的桌面会话中手动验证。

## Debian 打包

适用于已提供 DDE Dock 开发接口的 Debian/统信 UOS 环境。`debian/` 包含控制文件、debhelper 构建规则、变更记录、版权声明与原生源码包格式；测试使用离屏 Qt，不需要图形会话。

```bash
sudo apt-get install build-essential debhelper cmake pkg-config qtbase5-dev dde-dock-dev libddcutil-dev
dpkg-checkbuilddeps
dpkg-buildpackage -us -uc -b -j4
```

安装包生成在项目上一级目录，例如 `../dde-dock-input-source_0.1.2_amd64.deb`。运行依赖由 `dpkg-shlibdeps` 自动生成，包内包含任务栏插件、后台程序、独立调试面板和 AMD AUX 的 udev 规则。

```bash
sudo apt-get install ../dde-dock-input-source_0.1.2_amd64.deb
```

安装后重新打开任务栏会话以加载新版插件。需要构建源码包时使用 `dpkg-buildpackage -us -uc`；建议从干净的 Git 检出目录构建。


参考：[ddcutil C API](https://www.ddcutil.com/api_main/)、[输入源 capabilities 示例](https://www.ddcutil.com/cap_u3011_verbose_output/)、[显示器能力信息与 USB-C 编号的限制](https://www.ddcutil.com/faq/)。
