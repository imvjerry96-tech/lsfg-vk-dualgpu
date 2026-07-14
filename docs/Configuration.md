# Configuration Options

Configuring vkb-vk is done either through **vkb-vk-ui** (graphical interface) or by manually editing the configuration file located at `~/.config/vkb-vk/conf.toml`.

Regardless of the method you choose, the concept of profiles remains the same.
- **Profiles**: Profiles allow you to create different sets of configurations for different applications or use cases. A profile can automatically be selected through the "active_in" property.
- **Profile Settings**: Settings related to a profile are stored under the "Profile Settings" or `[[profile]]` section.
- **Global Settings**: Any settings in the "Global Settings" or `[global]` section apply to all profiles.

### All Configuration Options

Below is a list of all available **global** configuration options:
- **Path to VScale / `dll`**: By default, vkb-vk will search certain directories for VScale. If you have VScale installed in a custom location, you can specify the full path to the "VScale.dll" file inside of VScale here.
- **Allow half-precision / `allow_fp16`**: If enabled, this will allow vkb-vk to take advantage of half-precision shader operations if supported by the GPU. This has a giant performance uplift on AMD GPUs, but does not affect NVIDIA GPUs (GTX 1000-series or older cards will actually see a big performance **decrease**). This option **does not** influence quality. (Default: `true`)

Next is a list of all available **profile** configuration options:
- **Profile Name / `name`**: The name of the profile, displayed in **vkb-vk-ui**. Additionally, this is used when selecting a profile through the `VKBPVK_PROFILE` environment variable.
- **Active In / `active_in`**: A list of 1) linux binary names, such as `mpv`, 2) windows executables, such as `GenshinImpact.exe` and 3) process names, such as `GameThread`. It is also possible to specify the last part of a path (e.g. `Ghostrunner2/Binaries/Win64/Ghostrunner2-Win64-Shipping.exe`). When a process matching one of these rules is detected, this profile will be activated.
- **Multiplier / `multiplier`**: The frame generation multiplier. A value of 3 means that for every frame rendered by the application, vkb-vk will generate 2 additional frames. (Default: `2`)
- **Flow Scale / `flow_scale`**: The resolution scale at which the motion vectors are calculated. A lower value means better performance, but worse quality. (Default: `1.0`)
- **Performance Mode / `performance_mode`**: When enabled, a significantly lighter frame generation model is used. This has a minor quality impact, but greatly improves performance. 
(Default: `false`)
- **Pacing Mode / `pacing`**: This option is explained in greater detail below. Supported values are **None / `none`**.
- **GPU / `gpu`**: The GPU to use for frame generation. This MUST be the **same GPU** as the one being used by the application. **Dual GPU is NOT supported**. You can identify a GPU through its name (e.g. `NVIDIA GeForce RTX 3080`), uppercase-only ID (e.g. `0x10DE:0x2C02`) or PCI bus ID (e.g. `3:0.0`). If not specified, the primary GPU will be used, which may lead to issues.

The "Multiplier", "Flow Scale" and "Performance Mode" options can be **hot-reloaded**, meaning that changes to these options will take effect immediately without needing to restart the application. Options such as "Pacing Mode" or removal of the profile require a swapchain recreation, which usually means resizing or restarting the application. Any other change requires an application restart. 

### Pacing Modes

**Pacing modes** determine how vkb-vk synchronizes frame generation with the application's frame rate.

Traditionally, vkb-vk did not have frame pacing and would present frames to the screen as soon as they are generated. This approach is flawed, because frames are generated and presented much quicker than your screen refreshes, causing frames to be skipped. This effect is countered by force-enabling V-Sync, as the compositor will then wait for the next screen update, before presenting the next frame.

Enabling V-Sync is not a "get-out-of-jail-free" card, because it introduces input latency. Additionally, not every compositor (such as gamescope) respects the V-Sync setting, leading to the same issues as before. As a result of this, additional pacing modes have been introduced to properly handle frame pacing.

Here are all available pacing modes:
- `none`: Traditional vkb-vk behavior. Forces V-Sync. Might require workarounds on some compositors.
- *... there are no other pacing modes yet ...*

### Environment Variables

The following environment variables affect vkb-vk:
- `DISABLE_VKBPVK`: If set, vkb-vk will be completely disabled.
- `VKBPVK_CONFIG`: Path to the configuration file.
- `VKBPVK_PROFILE`: Name of the profile to use. If set, this will override automatic profile detection.

If you do not wish to use a configuration file, you can also set configuration options through environment variables. To do this, set `VKBPVK_ENV=1` and then any of the following variables:
- `VKBPVK_DLL_PATH`: Path to VScale DLL.
- `VKBPVK_NO_FP16`: If set to `1`, half-precision will be disabled.
- `VKBPVK_MULTIPLIER`: Frame generation multiplier.
- `VKBPVK_FLOW_SCALE`: Flow scale value.
- `VKBPVK_PERFORMANCE_MODE`: If set to `1`, performance mode will be enabled.
- `VKBPVK_PACING`: Pacing mode to use.
- `VKBPVK_GPU`: GPU to use for frame generation.
