# NX-ScreenUploader

[English](./README.md) | [中文](./README_CN.md)

`NX-ScreenUploader` 自动将你在 Nintendo Switch 上拍摄的截图和录像发送到 Telegram 和/或 ntfy.sh，让你更容易地访问和分享它们。

本项目是从 [bakatrouble/sys-screenuploader](https://github.com/bakatrouble/sys-screenuploader)、[musse/sys-screen-capture-uploader](https://github.com/musse/sys-screen-capture-uploader) 和 [yuno-kojo/sys-screen-capture-uploader](https://github.com/yuno-kojo/sys-screen-capture-uploader) 分叉而来。与原项目不同的是，不需要中间服务器。屏幕截图直接上传到 Telegram API 或 ntfy.sh 服务。

## 功能特性

- 自动上传 Switch 上拍摄的截图和录像
- 多个上传目标：Telegram 和/或 ntfy.sh
- 支持自定义 Telegram Bot API URL（用于反向代理）
- 支持自定义新屏幕截图检查间隔
- 相比原项目内存使用量更少（从 ~1.852 MB 降低到 ~1.336 MB）
- 在 applet 模式下打开 nxmenu 时不再发生致命崩溃

## 需求

你需要一个运行 [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere) 的 Nintendo Switch，这是一个自定义固件，允许你运行自制软件。如果你不知道如何破解你的 Switch，请查看 [这个指南](https://switch.homebrew.guide/index.html)。

## 设置

你可以选择上传到 Telegram、ntfy.sh 或两者。至少配置一个上传目标。

### 选项 1：Telegram

要使用 Telegram，你必须创建自己的 Telegram 机器人。它将把你的 Switch 屏幕截图发送给你（仅你一个人）的私聊。

1. [创建一个 Telegram 机器人](https://core.telegram.org/bots#3-how-do-i-create-a-bot)。选择任何你喜欢的名字和用户名。记下它的授权令牌。
2. 从应该接收截图的 Telegram 用户向你的机器人发送任何消息。
3. 在浏览器中打开 `https://api.telegram.org/bot<your-bot-token>/getUpdates`。你应该会看到一个包含你的消息的单一结果。记下聊天 ID（在 [`jq`](https://stedolan.github.io/jq/) 过滤器符号中为 `.result[0].message.chat.id`）。

### 选项 2：ntfy.sh

[ntfy.sh](https://ntfy.sh) 是一个简单的基于 HTTP 的发布-订阅通知服务。你可以使用公共实例或托管自己的实例。

注意，ntfy.sh 主题默认是公开的。任何知道你的主题名称的人都可以发布或订阅它。因此，建议选择一个唯一且难以猜测的主题名称。你也可以使用访问令牌保护你的主题。

1. 选择一个唯一、难以猜测的主题名称（例如 `my-switch-captures-abcdefg`）
2. （可选）如果你想保护你的主题，请在 [ntfy.sh/account](https://ntfy.sh/account) 创建访问令牌
3. 使用 ntfy 移动应用或网页界面订阅你的主题（例如 `https://ntfy.sh/my-switch-captures-abcdefg`）

### 安装

1. 下载 [最新版本](https://github.com/sakarie9/NX-ScreenUploader)并将其解压到某处。
2. 将 `config/NX-ScreenUploader/config.ini.template` 复制到 `config/NX-ScreenUploader/config.ini` 并配置你的上传目标：
   - **对于 Telegram**：在 `[general]` 中设置 `telegram = true`，然后在 `[telegram]` 部分配置 `bot_token` 和 `chat_id`
   - **对于 ntfy.sh**：在 `[general]` 中设置 `ntfy = true`，然后在 `[ntfy]` 部分配置 `topic`（和可选的 `token`）
   - 你可以同时启用两个目标
3. 将发布内容复制到你的 SD 卡的根目录。

## 开发

### 依赖

你可以安装 [`devkitpro-pacman`](https://github.com/devkitPro/pacman/releases/latest)，然后使用以下命令安装所需的依赖：

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S devkitA64 switch-dev switch-curl switch-zlib
```

查看以下链接以获取有关如何为 Switch 自制软件开发设置环境的更多详细信息：

- [自制应用开发](https://switch.homebrew.guide/homebrew_dev/app_dev.html)
- [设置开发环境](https://switchbrew.org/wiki/Setting_up_Development_Environment)

### 构建

> 要构建项目，你还需要 [`CMake`](https://cmake.org/)。

```bash
bash scripts/build.sh
bash scripts/release.sh
```

或手动构建：

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../devkita64-libnx.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

你现在应该在 `build` 目录中有一个 `Makefile`，可以简单地运行 `make` 来构建项目。相关的二进制文件是 `NX-ScreenUploader.nsp`。

构建项目后，你可以从存储库根目录运行 `scripts/release.sh` 生成发布版本。这将创建正确的目录结构，应该复制到你的 SD 卡的根目录，以及包含所有这些文件的 zip 文件。

## 鸣谢

- [bakatrouble/sys-screenuploader](https://github.com/bakatrouble/sys-screenuploader)：本项目的源分叉项目
- [musse/sys-screen-capture-uploader](https://github.com/musse/sys-screen-capture-uploader)：本项目的源分叉项目
- [yuno-kojo/sys-screen-capture-uploader](https://github.com/yuno-kojo/sys-screen-capture-uploader)：本项目的源分叉项目
- [vbe0201/libnx-template](https://github.com/vbe0201/libnx-template/)：自制应用模板项目
- [SunTheCourier/SwitchPresence-Rewritten](https://github.com/SunTheCourier/SwitchPresence-Rewritten)：初始 sysmodule 代码
