# NX-ScreenUploader

`NX-ScreenUploader` automatically sends screen captures (both images and videos) taken on your Nintendo Switch to a Telegram chat so that you can more easily access and share them.

This project was forked from [bakatrouble/sys-screenuploader](https://github.com/bakatrouble/sys-screenuploader) and [musse/sys-screen-capture-uploader](https://github.com/musse/sys-screen-capture-uploader) and [yuno-kojo/sys-screen-capture-uploader](https://github.com/yuno-kojo/sys-screen-capture-uploader). It differs from the original project because no intermediate server is needed. The screen captures are directly uploaded to the Telegram API. However, creating a Telegram bot is necessary (see [Setup](#setup) below).

## Features

- Automatically uploads screenshots and screen recordings taken on the Switch to a Telegram chat
- Supports uploading both images and videos
- Supports uploading in compressed (photo/video) or original (document) or both modes
- Custom Telegram Bot API URL support (for reverse proxy)
- Custom new screen capture check interval support
- Less memory usage compared to the original project (from ~1.852 MB to ~1.336 MB)
- No more fatal crashes when opening nxmenu in applet mode alongside other sysmodules

## Requirements

You need a Nintendo Switch running [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere), a custom firmware which will allow you to run homebrew. Check [this guide](https://switch.homebrew.guide/index.html) if you don't know how to hack your Switch.

## Setup

To use this homebrew, you must create your own Telegram bot. It will send your Switch screen captures to you (and only to you) on a private chat.

1. [Create a Telegram bot](https://core.telegram.org/bots#3-how-do-i-create-a-bot). Choose any name and username of your preference. Note down its authorization token.
2. Send any message to your bot from the Telegram user that should receive the screenshots.
3. Open `https://api.telegram.org/bot<your-bot-token>/getUpdates` on your browser. You should see a single result with your message. Note down the chat ID (`.result[0].message.chat.id` in [`jq`](https://stedolan.github.io/jq/) filter notation).

After getting these two values, you are ready to configure the homebrew and install it to your Switch:

1. Download [the latest release](https://github.com/sakarie9/NX-ScreenUploader) and extract it somewhere.
2. Open `config/sys-screen-uploader/config.ini` and set `telegram_bot_token` and `telegram_chat_id` to the values you have noted down.
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
