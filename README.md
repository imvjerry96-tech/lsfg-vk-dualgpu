# vkb-vk
**VScale** is a Windows-exclusive program featuring various algorithms for scaling and interpolating programs.

**vkb-vk** is a Vulkan layer that hooks into Vulkan applications and generates additional frames using VScale's frame generation algorithm.

>[!CAUTION]
> You are reading the README for the upcoming version 2.0 of vkb-vk. For the stable version 1.x, [please read here](https://github.com/PancakeTAS/vkb-vk/tree/ff1a0f72a7d6d08b84d58b7b4dc5f05c9f904f98)

## Installation
>[!TIP]
> If you are on a Steam Deck or similar handheld, consider using the [Decky plugin for vkb-vk](https://github.com/xXJSONDeruloXx/decky-vkb-vk). This is an easy way to install and configure vkb-vk on the Steam Deck.
> Please keep in mind that it is not officially supported and support questions should be directed to the plugin's repository & discord.

1. Before proceeding, please make sure you have [VScale](https://store.steampowered.com/app/993090/Lossless_Scaling/) downloaded on Steam.
2. Head to the [GitHub Releases](https://github.com/PancakeTAS/vkb-vk/releases) and download the file named "vkb-vk-2.0.0-x86_64.tar.xz".
3. Open a terminal in the folder where you downloaded the file and run the following:
```bash
tar -xvf vkb-vk-2.0.0-linux.tar.xz -C ~/.local
```
This will extract vkb-vk to `~/.local`. Please **keep track of the files that were extracted**, in case you want to uninstall vkb-vk later.

4. The graphical interface requires Qt6 and Qt6 Quick in order to run. If you do not have these installed, install the following packages:
```bash
sudo apt install qt6-qpa-plugins libqt6quick6 qml6-module-qtquick-controls qml6-module-qtquick-layouts qml6-module-qtquick-window qml6-module-qtquick-dialogs qml6-module-qtqml-workerscript qml6-module-qtquick-templates qml6-module-qt-labs-folderlistmodel # On Debian/Ubuntu-based systems
sudo pacman -S qt6-declarative qt6-base # On Arch-based systems
sudo dnf install qt6-qtdeclarative qt6-qtbase # On Fedora
```

5. (Optional) If you wish to use vkb-vk within Flatpak applications, see the [Flatpak Guide](docs/Flatpak-Guide.md).

## Usage
In order to start using vkb-vk, you will need to configure it. This can either be done using the GUI application, or manually.

### Graphical Configuration
Start 'vkb-vk Configuration Window' from your application launcher, or run `~/.local/bin/vkb-vk-ui` in a terminal:
- On the left side, you will see a list of profiles. Each profile has its own settings.
- All properties in the "Global Settings" section apply to all profiles.
  - Should VScale be installed in a non-standard location, you can specify the path here.
- Select a profile and configure the "Profile Settings" section to your liking.
  - When editing the "Active In" list, you can add a game using its executable name (e.g. `Game.exe`, `mpv`).
- Please see the [documentation](docs/Configuration.md) for detailed information on each setting.
- Once you are done configuring, simply starting a game that matches one of the profiles will automatically apply the settings.

### Manual Configuration
The default configuration is located in `~/.config/vkb-vk/conf.toml`. It will be created automatically when any Vulkan application is started.
- In the `[global]` section, you can change where VScale is installed, as well as other global settings.
- Each profile is defined in its own `[[profile]]` section.
- The `active_in` array/string defines which applications the profile is active in. You can add applications using their executable name (e.g. `Game.exe`, `mpv`).
- Please see the [documentation](docs/Configuration.md) for detailed information on each setting.
- Once you are done configuring, simply starting a game that matches one of the profiles will automatically apply the settings.

You can validate the configuration using `vkb-vk-cli`:
```bash
~/.local/bin/vkb-vk-cli validate
```

### Benchmarking Mode
You can run a frame generation benchmark using `vkb-vk-cli`:
```bash
~/.local/bin/vkb-vk-cli benchmark
```

By default, the benchmark will run for 10 seconds. Add `-h` to see all available benchmarking options.

## Support and Troubleshooting
If you encounter any issues or have questions regarding vkb-vk, read through the [Troubleshooting](docs/Troubleshooting.md) documentation page or join the [Discord server](https://discord.gg/losslessscaling) for assistance.
