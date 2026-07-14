# Flatpak Guide

If you want to use **vkb-vk** with Flatpak applications, you must install the Vulkan layer for Flatpak. You can also optionally install the graphical configuration editor **vkb-vk-ui** as a Flatpak application.

## Installation

You can install vkb-vk for Flatpak through three different methods.

### Through Flathub

The main vkb-vk layer is available on Flathub and can be installed with the following commands (you can omit the `--user` in a system installation):
```bash
flatpak install --user org.freedesktop.Platform.VulkanLayer.vkbp//24.08
flatpak install --user org.freedesktop.Platform.VulkanLayer.vkbp//25.08
```

If you require an older runtime (23.08), you must install it manually as shown in the next section. Similarly, if you want to install the graphical configuration editor **vkb-vk-ui**, you must also install it manually.

### Through GitHub Releases

Head over to the [GitHub Releases](https://github.com/PancakeTAS/vkb-vk/releases) page and download the Flatpak files for the Vulkan layer and/or the graphical configuration editor.

It can be installed with the following commands (you can omit the `--user` in a system installation):
```bash
tar -xf vkb-vk-2.0.0-flatpaks-x86_64.tar.xz
flatpak --user install ./org.freedesktop.Platform.VulkanLayer.vkb-vk-23.08.flatpak
flatpak --user install ./org.freedesktop.Platform.VulkanLayer.vkb-vk-24.08.flatpak
flatpak --user install ./org.freedesktop.Platform.VulkanLayer.vkb-vk-25.08.flatpak
flatpak --user install ./gay.pancake.vkb-vk-ui.flatpak
```

You can then run the graphical configuration editor with:
```bash
flatpak run gay.pancake.lsfg_vk_ui
```

### Through Custom Build

If you want to build vkb-vk yourself, install `flatpak-builder` and run the following commands:
```bash
git clone --depth=1 https://github.com/PancakeTAS/vkb-vk.git
# optional: git checkout <desired-version>
cd vkb-vk
flatpak-builder --force-clean --user --install-deps-from=flathub --install flatpak-build \
    dist/flatpak/vkb-vk-ui/gay.pancake.vkb-vk-ui.yml
flatpak-builder --force-clean --user --install-deps-from=flathub --install flatpak-build \
    dist/flatpak/vkb-vk-layer/org.freedesktop.Platform.VulkanLayer.vkbp_23.08.yml
flatpak-builder --force-clean --user --install-deps-from=flathub --install flatpak-build \
    dist/flatpak/vkb-vk-layer/org.freedesktop.Platform.VulkanLayer.vkbp_24.08.yml
flatpak-builder --force-clean --user --install-deps-from=flathub --install flatpak-build \
    dist/flatpak/vkb-vk-layer/org.freedesktop.Platform.VulkanLayer.vkbp_25.08.yml
```

## Configuration

Before using vkb-vk with Flatpak applications, you need to give them access to the configuration directory, as well as VScale.
```bash
export appid=  # e.g. io.mpv.Mpv
mkdir -p ~/.config/vkb-vk
flatpak override --user --filesystem=/home/$USER/.config/vkb-vk:rw $appid
flatpak override --user --filesystem=/home/$USER/local/share/Steam/steamapps/common:ro $appid
flatpak override --user --env=VKBPVK_CONFIG=/home/$USER/.config/vkb-vk/conf.toml $appid
```
