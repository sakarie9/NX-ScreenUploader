# NX-ScreenUploader

English | [中文](./README_CN.md)

`NX-ScreenUploader` automatically sends screen captures (both images and videos) taken on your Nintendo Switch to Telegram and/or ntfy.sh so that you can more easily access and share them.

This project was forked from [bakatrouble/sys-screenuploader](https://github.com/bakatrouble/sys-screenuploader) and [musse/sys-screen-capture-uploader](https://github.com/musse/sys-screen-capture-uploader) and [yuno-kojo/sys-screen-capture-uploader](https://github.com/yuno-kojo/sys-screen-capture-uploader). It differs from the original project because no intermediate server is needed. The screen captures are directly uploaded to the Telegram or ntfy.sh or Discord.

## Features

- Automatically uploads screenshots and screen recordings taken on the Switch
- Multiple upload destinations: Telegram/Discord/ntfy.sh
- Custom Telegram Bot API URL support (for reverse proxy)
- Custom new screen capture check interval support
- Less memory usage compared to the original project (from ~1.852 MB to ~1.339 MB)
- No more fatal crashes when opening nxmenu in applet mode alongside other sysmodules

## Requirements

You need a Nintendo Switch running [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere), a custom firmware which will allow you to run homebrew. Check [this guide](https://switch.homebrew.guide/index.html) if you don't know how to hack your Switch.

## Setup

You can choose to upload to Telegram, ntfy.sh, or both. Configure at least one upload destination.

### Prepare Destinations

At least one upload destination must be configured.

#### Telegram

To use Telegram, you must create your own Telegram bot. It will send your Switch screen captures to you (and only to you) on a private chat.

1. [Create a Telegram bot](https://core.telegram.org/bots#how-do-i-create-a-bot). Choose any name and username of your preference. Note down its `API Token`.
2. Send any message to your bot from the Telegram user that should receive the screenshots.
3. Open `https://api.telegram.org/bot<your-bot-token>/getUpdates` on your browser. You should see a single result with your message. Note down the `chat ID` (`.result[0].message.chat.id` in [`jq`](https://stedolan.github.io/jq/) filter notation).

#### ntfy.sh

[ntfy.sh](https://ntfy.sh) is a simple HTTP-based pub-sub notification service. You can use the public instance or host your own.

Note that ntfy.sh topics are public by default. Anyone who knows your topic name can publish or subscribe to it. Therefore, it's recommended to choose a unique and hard-to-guess topic name. You can also protect your topic with an access token.

1. Choose a unique, hard-to-guess topic name (e.g., `my-switch-captures-abcdefg`)
2. (Optional) If you want to protect your topic, create an access token at [ntfy.sh/account](https://ntfy.sh/account)
3. Subscribe to your topic using the ntfy mobile app or web interface (e.g., `https://ntfy.sh/my-switch-captures-abcdefg`)

#### Discord

To use Discord, you must create your own Discord APP and Bot. It will send your Switch screen captures to a specified channel in your server.

Discord configuration is a little more complex than Telegram and ntfy.sh.

Follow [Creating a Bot Account](https://discordpy.readthedocs.io/en/stable/discord.html) to obtain your `Bot Token` and invite the bot to your server with the appropriate permissions (at least "Send Messages").

Then get the `Channel ID` of the channel where you want the screenshots to be sent. You can do this by enabling Developer Mode in Discord settings, then right-clicking on the desired channel and selecting "Copy Channel ID".

### Installation

Once you have prepared your upload destination(s), you can install the sysmodule on your Switch:

1. Download [the latest release](https://github.com/sakarie9/NX-ScreenUploader) and extract it somewhere.
2. Copy `config/NX-ScreenUploader/config.ini.template` to `config/NX-ScreenUploader/config.ini` and configure your upload destination(s):
   - **For Telegram**: Set `telegram = true` in `[general]`, then configure `bot_token` and `chat_id` in `[telegram]` section
   - **For ntfy.sh**: Set `ntfy = true` in `[general]`, then configure `topic` (and optionally `token`) in `[ntfy]` section
   - **For Discord**: Set `discord = true` in `[general]`, then configure `bot_token` and `channel_id` in `[discord]` section
   - You can enable destinations simultaneously
3. Copy the release contents to the root of your SD card.

## Development

### Dependencies

You can install [`devkitpro-pacman`](https://github.com/devkitPro/pacman/releases/latest) and then install the needed dependencies with:

```bash
sudo dkp-pacman -Syu
sudo dkp-pacman -S devkitA64 switch-dev switch-curl switch-zlib
```

Check the following links for additional details on how to set up your environment for Switch homebrew development:

- [Homebrew app development](https://switch.homebrew.guide/homebrew_dev/app_dev.html)
- [Setting up Development Environment](https://switchbrew.org/wiki/Setting_up_Development_Environment)

### Build

> To build the project, you also need [`CMake`](https://cmake.org/).

```bash
bash scripts/build.sh
bash scripts/release.sh
```

Or manually:

```bash
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=../devkita64-libnx.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

You should now have a `Makefile` inside the `build` directory and can simply run `make` to build the project. The relevant binary file is `NX-ScreenUploader.nsp`.

After building the project, you can generate a release by running `scripts/release.sh` from the repository root. This will create the correct directory structure that should be copied to the root of your SD card and also a zip file containing all these files.

## Credits

- [bakatrouble/sys-screenuploader](https://github.com/bakatrouble/sys-screenuploader): project from which this project was forked;
- [musse/sys-screen-capture-uploader](https://github.com/musse/sys-screen-capture-uploader): project from which this project was forked;
- [yuno-kojo/sys-screen-capture-uploader](https://github.com/yuno-kojo/sys-screen-capture-uploader): project from which this project was forked;
- [vbe0201/libnx-template](https://github.com/vbe0201/libnx-template/): homebrew template project;
- [SunTheCourier/SwitchPresence-Rewritten](https://github.com/SunTheCourier/SwitchPresence-Rewritten): initial sysmodule code.
