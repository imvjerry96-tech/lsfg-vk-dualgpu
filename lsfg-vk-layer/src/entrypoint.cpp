/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "instance.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"
#include "swapchain.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>
#include <dlfcn.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan_core.h>

using namespace vkbp::layer;

namespace {
    // global layer info initialized at layer negotiation
    struct LayerInfo {
        std::unordered_map<std::string, PFN_vkVoidFunction> map; //!< function pointer override map
        PFN_vkGetInstanceProcAddr GetInstanceProcAddr;

        Root root;
    }* layer_info; // NOLINT (global variable)

    // instance-wide info initialized at instance creation(s)
    struct InstanceInfo {
        std::vector<VkInstance> handles; // there may be several instances
        vk::VulkanInstanceFuncs funcs;

        std::unordered_map<VkDevice, vk::Vulkan> devices;
        std::unordered_map<VkSwapchainKHR, ls::R<vk::Vulkan>> swapchains;
        std::unordered_map<VkSwapchainKHR, SwapchainInfo> swapchainInfos;
    }* instance_info; // NOLINT (global variable)

    // create instance
    VkResult myvkCreateInstance(
            const VkInstanceCreateInfo* info,
            const VkAllocationCallbacks* alloc,
            VkInstance* instance) {
        // NULL-safe: if the layer has not been negotiated yet, we cannot chain.
        // Pass through to the next layer/driver instead of failing — some
        // processes (gamescope's vulkan-helper) call vkCreateInstance before
        // the loader has negotiated us. Failing here aborts the whole compositor.
        if (!layer_info) {
            static auto* nextCreateInstance =
                reinterpret_cast<PFN_vkCreateInstance>(dlsym(RTLD_NEXT, "vkCreateInstance"));
            if (nextCreateInstance)
                return nextCreateInstance(info, alloc, instance);
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // apply layer chaining
        auto* layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO
                || layerInfo->function != VK_LAYER_LINK_INFO)) {
            layerInfo = reinterpret_cast<VkLayerInstanceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "vkb-vk: no layer info found in pNext chain, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto* linkInfo = layerInfo->u.pLayerInfo;
        if (!linkInfo) {
            std::cerr << "vkb-vk: link info is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        layer_info->GetInstanceProcAddr = linkInfo->pfnNextGetInstanceProcAddr;
        if (!layer_info->GetInstanceProcAddr) {
            std::cerr << "vkb-vk: next layer's vkGetInstanceProcAddr is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        fprintf(stderr, "[vkb] myvkCreateInstance ENTER active=%d\n", (int)layer_info->root.active());
        fflush(stderr);

        // TRUE passthrough when no profile is loaded: do not build any wrapper,
        // do not touch the dispatch chain. The game (and its anti-tamper) only
        // tolerates a clean Vulkan chain when frame-gen is off.
        if (!layer_info->root.profileLoaded()) {
            if (!layer_info->GetInstanceProcAddr) {
                fprintf(stderr, "[vkb] passthrough: GetInstanceProcAddr NULL\n");
                fflush(stderr);
                return VK_ERROR_INITIALIZATION_FAILED;
            }
            auto* nextCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
                layer_info->GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
            fprintf(stderr, "[vkb] passthrough vkCreateInstance next=%p\n", (void*)nextCreateInstance);
            fflush(stderr);
            if (!nextCreateInstance)
                return VK_ERROR_INITIALIZATION_FAILED;
            VkResult res = nextCreateInstance(info, alloc, instance);
            // ensure instance_info exists so myvkCreateDevice (wrap path) has valid funcs
            if (res == VK_SUCCESS && !instance_info)
                instance_info = new InstanceInfo{
                    .funcs = vk::initVulkanInstanceFuncs(*instance, layer_info->GetInstanceProcAddr, true),
                };
            if (res == VK_SUCCESS)
                instance_info->handles.push_back(*instance);
            return res;
        }

        layerInfo->u.pLayerInfo = linkInfo->pNext; // advance for next layer (only when wrapping)

        // create instance
        auto* vkCreateInstance = reinterpret_cast<PFN_vkCreateInstance>(
            layer_info->GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
        if (!vkCreateInstance) {
            std::cerr << "vkb-vk: failed to get next layer's vkCreateInstance, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        try {
            VkInstanceCreateInfo newInfo = *info;
            layer_info->root.modifyInstanceCreateInfo(newInfo,
                [=, newInfo = &newInfo]() {
                    auto res = vkCreateInstance(newInfo, alloc, instance);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateInstance() failed");
                }
            );

            if (!instance_info)
                instance_info = new InstanceInfo{ // NOLINT (memory management)
                    .funcs = vk::initVulkanInstanceFuncs(*instance,
                        layer_info->GetInstanceProcAddr, true),
                };

            instance_info->handles.push_back(*instance);

            return VK_SUCCESS;
        } catch (const ls::vulkan_error& e) {
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "vkb-vk: required Vulkan instance extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        }
    }

    // create device
    VkResult myvkCreateDevice(
            VkPhysicalDevice physdev,
            const VkDeviceCreateInfo* info,
            const VkAllocationCallbacks* alloc,
            VkDevice* device) {
        fprintf(stderr, "[vkb] myvkCreateDevice ENTER profileLoaded=%d\n",
            (int)(layer_info ? layer_info->root.profileLoaded() : -1));
        fflush(stderr);
        // apply layer chaining
        auto* layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                || layerInfo->function != VK_LAYER_LINK_INFO)) {
            layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "vkb-vk: no layer info found in pNext chain, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        fprintf(stderr, "[vkb] myvkCreateDevice layerInfo=%p\n", (void*)layerInfo);
        fflush(stderr);

        auto* linkInfo = layerInfo->u.pLayerInfo;
        if (!linkInfo) {
            std::cerr << "vkb-vk: link info is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // advance link ALWAYS so the next layer in the chain sees the correct link
        layerInfo->u.pLayerInfo = linkInfo->pNext;

        // ALWAYS passthrough vkCreateDevice (do not wrap). Wrapping requires a fully
        // initialized instance_info which the passthrough instance path doesn't build,
        // and dereferencing it here segfaults gamescope. The layer is still inserted
        // (anti-tamper bypass holds); FG engages via the swapchain hook instead.
        {
            // Use the next device proc from instance_info->funcs (initialized during
            // instance passthrough). This keeps the loader dispatch chain valid so
            // later vkDevice* calls don't hit "Bad destination in trampoline dispatch".
            if (!instance_info)
                return VK_ERROR_INITIALIZATION_FAILED;
            VkResult res = instance_info->funcs.CreateDevice(physdev, info, alloc, device);
            fprintf(stderr, "[vkb] passthrough vkCreateDevice res=%d\n", (int)res);
            fflush(stderr);
            // register the device into the loader chain (same as the wrap path does),
            // so later vkDevice* calls dispatch correctly.
            if (res == VK_SUCCESS && device && *device && !instance_info->handles.empty()) {
                auto* cb = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
                while (cb && (cb->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                        || cb->function != VK_LOADER_DATA_CALLBACK))
                    cb = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(cb->pNext));
                auto* sld = cb ? cb->u.pfnSetDeviceLoaderData : nullptr;
                // the wrap path sets this before constructing vk::Vulkan; without it
                // initVulkanDeviceFuncs dereferences a null GetDeviceProcAddr and crashes.
                instance_info->funcs.GetDeviceProcAddr = linkInfo->pfnNextGetDeviceProcAddr;
                // register the device with the loader FIRST so that when vk::Vulkan's
                // ctor calls initVulkanDeviceFuncs -> GetDeviceProcAddr(device, ...),
                // the loader already knows the device and returns valid core procs
                // (otherwise vkSignalSemaphoreKHR etc. fail with -3).
                if (sld)
                    sld(*device, *device);
                try {
                    instance_info->devices.emplace(
                        *device,
                        vk::Vulkan(
                            instance_info->handles.front(), *device, physdev,
                            instance_info->funcs, vk::initVulkanDeviceFuncs(
                                instance_info->funcs, *device,
                                instance_info->handles.front(), true),
                            true, sld
                        )
                    );
                } catch (const std::exception& e) {
                    std::cerr << "vkb-vk: device registration failed: " << e.what() << '\n';
                }
            }
            return res;
        }

        if (!instance_info)
            return VK_ERROR_INITIALIZATION_FAILED;

        instance_info->funcs.GetDeviceProcAddr = linkInfo->pfnNextGetDeviceProcAddr;
        if (!linkInfo->pfnNextGetDeviceProcAddr) {
            std::cerr << "vkb-vk: next layer's vkGetDeviceProcAddr is null, "
                "the previous layer does not follow spec\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // fetch device loader functions
        layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(info->pNext));
        while (layerInfo && (layerInfo->sType != VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO
                || layerInfo->function != VK_LOADER_DATA_CALLBACK)) {
            layerInfo = reinterpret_cast<VkLayerDeviceCreateInfo*>(const_cast<void*>(layerInfo->pNext));
        }
        if (!layerInfo) {
            std::cerr << "vkb-vk: no layer loader data found in pNext chain.\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        auto* setLoaderData = layerInfo->u.pfnSetDeviceLoaderData;
        if (!setLoaderData) {
            std::cerr << "vkb-vk: instance loader data function is null.\n";
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // TRUE passthrough when no profile is loaded: skip the vk::Vulkan wrapper
        // and all dispatch-chain interception (anti-tamper rejects dirty chains).
        if (!layer_info->root.profileLoaded()) {
            return instance_info->funcs.CreateDevice(physdev, info, alloc, device);
        }

        // create device
        try {
            VkDeviceCreateInfo newInfo = *info;
            layer_info->root.modifyDeviceCreateInfo(newInfo,
                [=, newInfo = &newInfo]() {
                    auto res = instance_info->funcs.CreateDevice(physdev, newInfo, alloc, device);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateDevice() failed");
                }
            );
        } catch (const ls::vulkan_error& e) {
            if (e.error() == VK_ERROR_EXTENSION_NOT_PRESENT)
                std::cerr << "vkb-vk: required Vulkan device extensions are not present. "
                    "Your GPU driver is not supported.\n";
            return e.error();
        }

        // create layer instance
        try {
            instance_info->devices.emplace(
                *device,
                vk::Vulkan(
                    instance_info->handles.front(), *device, physdev,
                    instance_info->funcs, vk::initVulkanDeviceFuncs(instance_info->funcs, *device,
                        instance_info->handles.front(), true),
                    true, setLoaderData
                )
            );
        } catch (const std::exception& e) {
            std::cerr << "vkb-vk: something went wrong during vkb-vk initialization:\n";
            std::cerr << "- " << e.what() << '\n';
        }

        return VK_SUCCESS;
    }

    // destroy device
    void myvkDestroyDevice(VkDevice device, const VkAllocationCallbacks* alloc) {
        if (!instance_info) return; // nothing to tear down

        // destroy layer instance
        auto it = instance_info->devices.find(device);
        if (it != instance_info->devices.end())
            instance_info->devices.erase(it);

        // destroy device
        auto vkDestroyDevice = reinterpret_cast<PFN_vkDestroyDevice>(
            instance_info->funcs.GetDeviceProcAddr(device, "vkDestroyDevice"));
        if (!vkDestroyDevice) {
            std::cerr << "vkb-vk: failed to get next layer's vkDestroyDevice, "
                "the previous layer does not follow spec\n";
            return;
        }

        vkDestroyDevice(device, alloc);
    }

    // destroy instance
    void myvkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* alloc) {
        if (!instance_info || !layer_info) return; // nothing to tear down

        // remove instance handle
        auto it = std::ranges::find(instance_info->handles, instance);
        if (it != instance_info->handles.end())
            instance_info->handles.erase(it);

        // destroy instance info if no handles remain
        if (instance_info->handles.empty()) {
            delete instance_info; // NOLINT (memory management)
            instance_info = nullptr;
        }

        // destroy instance
        auto vkDestroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
            layer_info->GetInstanceProcAddr(instance, "vkDestroyInstance"));
        if (!vkDestroyInstance) {
            std::cerr << "vkb-vk: failed to get next layer's vkDestroyInstance, "
                "the previous layer does not follow spec\n";
            return;
        }

        vkDestroyInstance(instance, alloc);
    }

    // get optional function pointer override
    PFN_vkVoidFunction getProcAddr(const std::string& name) {
        // NULL-safe: if negotiate hasn't run yet, do not dereference the
        // uninitialized global. Return nullptr -> loader uses the next layer.
        if (!layer_info) return nullptr;
        // NOTE: we always return our intercepted functions (vkCreateInstance etc.)
        // even when the profile isn't loaded yet. myvkCreateInstance internally
        // checks profileLoaded() and passthroughs cleanly. Returning nullptr here
        // made the loader skip our vkCreateInstance and call the driver directly
        // with the layer still injected into the pNext chain -> -3.
        auto it = layer_info->map.find(name);
        if (it != layer_info->map.end())
            return it->second;
        return nullptr;
    }

    // get instance-level function pointers
    PFN_vkVoidFunction myvkGetInstanceProcAddr(VkInstance instance, const char* name) {
        if (!name) return nullptr;
        if (!layer_info) return nullptr; // not negotiated yet -> let loader chain

        if (strcmp(name, "vkCreateInstance") == 0)
            fprintf(stderr, "[vkb] GetInstanceProcAddr(vkCreateInstance) profileLoaded=%d\n",
                (int)layer_info->root.profileLoaded());

        auto func = getProcAddr(name);
        if (func) return func;

        // Fallback chain: pass through to the next layer/driver. When our profile
        // is not loaded yet (gamescope queries many extensions at init), we must
        // NOT return nullptr (loader aborts with -3). Resolve the real system
        // symbol directly via dlsym(RTLD_NEXT) — this skips any misbehaving
        // intermediate layer and reaches the driver's implementation.
        void* sym = dlsym(RTLD_NEXT, name);
        if (sym) return (PFN_vkVoidFunction)sym;

        if (layer_info->GetInstanceProcAddr)
            return layer_info->GetInstanceProcAddr(instance, name);
        return nullptr;
    }

    // get device-level function pointers
    PFN_vkVoidFunction myvkGetDeviceProcAddr(VkDevice device, const char* name) {
        if (!name) return nullptr;
        if (!layer_info || !instance_info) return nullptr; // not ready -> passthrough

        auto func = getProcAddr(name);
        if (func) return func;

        if (!instance_info->funcs.GetDeviceProcAddr) return nullptr;
        return instance_info->funcs.GetDeviceProcAddr(device, name);
    }
}

namespace {
    VkResult myvkCreateSwapchainKHR(
            VkDevice device,
            const VkSwapchainCreateInfoKHR* info,
            const VkAllocationCallbacks* alloc,
            VkSwapchainKHR* swapchain) {
        fprintf(stderr, "[vkb] myvkCreateSwapchainKHR ENTER instance_info=%p\n", (void*)instance_info);
        fflush(stderr);
        // NULL-safe: if instance_info was never built (passthrough init path), we
        // cannot intercept. Pass through to the next layer's vkCreateSwapchainKHR.
        if (!instance_info || instance_info->devices.empty()) {
            auto* next = reinterpret_cast<PFN_vkCreateSwapchainKHR>(
                layer_info->GetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateSwapchainKHR"));
            if (!next)
                return VK_ERROR_INITIALIZATION_FAILED;
            return next(device, info, alloc, swapchain);
        }
        const auto& it = instance_info->devices.find(device);
        if (it == instance_info->devices.end())
            return VK_ERROR_INITIALIZATION_FAILED;

        // if no profile is loaded for this process, pass through to the next layer.
        // When a profile IS loaded we take the real path (this is also where the
        // layer becomes "engaged" - see Root::createSwapchainContext).
        if (!layer_info->root.profileLoaded()) {
            return it->second.df().CreateSwapchainKHR(device, info, alloc, swapchain);
        }

        try {
            // retire old swapchain
            if (info->oldSwapchain) {
                const auto& info_mapping = instance_info->swapchainInfos.find(info->oldSwapchain);
                if (info_mapping != instance_info->swapchainInfos.end())
                    instance_info->swapchainInfos.erase(info_mapping);

                const auto& mapping = instance_info->swapchains.find(info->oldSwapchain);
                if (mapping != instance_info->swapchains.end())
                    instance_info->swapchains.erase(mapping);

                layer_info->root.removeSwapchainContext(info->oldSwapchain);
            }

            layer_info->root.update(); // ensure config is up to date

            // create swapchain
            VkSwapchainCreateInfoKHR newInfo = *info;
            layer_info->root.modifySwapchainCreateInfo(it->second, newInfo,
                [=, newInfo = &newInfo]() {
                    auto res = it->second.df().CreateSwapchainKHR(
                        device, newInfo, alloc, swapchain);
                    if (res != VK_SUCCESS)
                        throw ls::vulkan_error(res, "vkCreateSwapchainKHR() failed");
                }
            );

            // get all swapchain images
            uint32_t imageCount{};
            auto res = it->second.df().GetSwapchainImagesKHR(device, *swapchain,
                &imageCount, VK_NULL_HANDLE);
            if (res != VK_SUCCESS || imageCount == 0)
                throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed");

            std::vector<VkImage> swapchainImages(imageCount);
            res = it->second.df().GetSwapchainImagesKHR(device, *swapchain,
                &imageCount, swapchainImages.data());
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkGetSwapchainImagesKHR() failed");

            auto& info = instance_info->swapchainInfos.emplace(*swapchain, SwapchainInfo {
                .images = std::move(swapchainImages),
                .format = newInfo.imageFormat,
                .colorSpace = newInfo.imageColorSpace,
                .extent = newInfo.imageExtent,
                .presentMode = newInfo.presentMode
            }).first->second;

            // create vkb-vk swapchain
            layer_info->root.createSwapchainContext(it->second, *swapchain, info);

            instance_info->swapchains.emplace(*swapchain,
                ls::R<vk::Vulkan>(it->second));

            return res;
        } catch (const ls::vulkan_error& e) {
            std::cerr << "vkb-vk: something went wrong during vkb-vk swapchain creation:\n";
            std::cerr << "- " << e.what() << '\n';
            return e.error();
        } catch (const std::exception& e) {
            std::cerr << "vkb-vk: something went wrong during vkb-vk swapchain creation:\n";
            std::cerr << "- " << e.what() << '\n';
            return VK_ERROR_INITIALIZATION_FAILED;
        }
    }

    VkResult myvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunknown-warning-option"
#pragma clang diagnostic ignored "-Wunsafe-buffer-usage"
        VkResult result = VK_SUCCESS;

        // NULL-safe: if instance_info was never built (passthrough init path),
        // pass through to the next layer's vkQueuePresentKHR cleanly.
        if (!instance_info || instance_info->devices.empty()) {
            auto* nextPresent = reinterpret_cast<PFN_vkQueuePresentKHR>(
                layer_info->GetInstanceProcAddr(VK_NULL_HANDLE, "vkQueuePresentKHR"));
            if (!nextPresent)
                return VK_ERROR_INITIALIZATION_FAILED;
            return nextPresent(queue, info);
        }

        // if no profile is loaded for this process, pass through to the next layer
        if (!layer_info->root.profileLoaded()) {
            VkDevice dev = VK_NULL_HANDLE;
            if (!instance_info->devices.empty())
                dev = instance_info->devices.begin()->first;
            if (dev != VK_NULL_HANDLE) {
                auto* nextPresent = reinterpret_cast<PFN_vkQueuePresentKHR>(
                    instance_info->funcs.GetDeviceProcAddr(dev, "vkQueuePresentKHR"));
                if (nextPresent)
                    return nextPresent(queue, info);
            }
            return VK_ERROR_INITIALIZATION_FAILED;
        }

        // ensure layer config is up to date
        bool reload{};
        try {
            reload = layer_info->root.update();
        } catch (const std::exception&) {
            reload = false; // ignore parse errors
        }

        if (reload) {
            try {
                for (const auto& [swapchain, vk] : instance_info->swapchains) {
                    auto& info = instance_info->swapchainInfos.at(swapchain);

                    layer_info->root.removeSwapchainContext(swapchain);
                    layer_info->root.createSwapchainContext(vk, swapchain, info);
                }

                std::cerr << "vkb-vk: updated vkb-vk configuration\n";
            } catch (const std::exception& e) {
                std::cerr << "vkb-vk: something went wrong during vkb-vk configuration update:\n";
                std::cerr << "- " << e.what() << '\n';
            }
        }

        // present each swapchain
        for (size_t i = 0; i < info->swapchainCount; i++) {
            const auto& swapchain = info->pSwapchains[i];

            const auto& it = instance_info->swapchains.find(swapchain);
            if (it == instance_info->swapchains.end())
                return VK_ERROR_INITIALIZATION_FAILED;

            try {
                std::vector<VkSemaphore> waitSemaphores;
                waitSemaphores.reserve(info->waitSemaphoreCount);

                for (size_t j = 0; j < info->waitSemaphoreCount; j++)
                    waitSemaphores.push_back(info->pWaitSemaphores[j]);

                auto& context = layer_info->root.getSwapchainContext(swapchain);
                result = context.present(it->second,
                    queue, swapchain,
                    const_cast<void*>(info->pNext),
                    info->pImageIndices[i],
                    { waitSemaphores.begin(), waitSemaphores.end() }
                );
            } catch (const ls::vulkan_error& e) {
                if (e.error() != VK_ERROR_OUT_OF_DATE_KHR) {
                    std::cerr << "vkb-vk: something went wrong during vkb-vk swapchain presentation:\n";
                    std::cerr << "- " << e.what() << '\n';
                } // silently swallow out-of-date errors

                result = e.error();
            } catch (const std::exception& e) {
                std::cerr << "vkb-vk: something went wrong during vkb-vk swapchain presentation:\n";
                std::cerr << "- " << e.what() << '\n';
                result = VK_ERROR_UNKNOWN;
            }

            if (result != VK_SUCCESS && info->pResults)
                info->pResults[i] = result;
        }

        return result;
#pragma clang diagnostic pop
    }

    void myvkDestroySwapchainKHR(
            VkDevice device,
            VkSwapchainKHR swapchain,
            const VkAllocationCallbacks* alloc) {
        if (!instance_info || !layer_info) return; // nothing to tear down

        const auto& it = instance_info->devices.find(device);
        if (it == instance_info->devices.end())
            return;

        const auto& info_mapping = instance_info->swapchainInfos.find(swapchain);
        if (info_mapping != instance_info->swapchainInfos.end())
            instance_info->swapchainInfos.erase(info_mapping);

        const auto& mapping = instance_info->swapchains.find(swapchain);
        if (mapping != instance_info->swapchains.end())
            instance_info->swapchains.erase(mapping);

        layer_info->root.removeSwapchainContext(swapchain);

        // destroy swapchain
        it->second.df().DestroySwapchainKHR(device, swapchain, alloc);
    }
}

/// Vulkan layer entrypoint
__attribute__((visibility("default")))
VkResult vkNegotiateLoaderLayerInterfaceVersion(VkNegotiateLayerInterface* pVersionStruct) {
    // identify current process for debugging
    char comm[256]{};
    char exe[4096]{};
    ssize_t cl = readlink("/proc/self/exe", exe, sizeof(exe)-1);
    if (cl > 0) exe[cl] = '\0';
    FILE* c = fopen("/proc/self/comm", "r");
    if (c) { fgets(comm, sizeof(comm), c); fclose(c); }
    fprintf(stderr, "[vkb] negotiate ENTER layer_info=%p comm=%s exe=%s\n",
        (void*)layer_info, comm, exe);
    fflush(stderr);
    // ensure loader compatibility
    if (!pVersionStruct
        || pVersionStruct->sType != LAYER_NEGOTIATE_INTERFACE_STRUCT
        || pVersionStruct->loaderLayerInterfaceVersion < 2)
        return VK_ERROR_INITIALIZATION_FAILED;

    // if the layer has already been initialized, skip
    if (layer_info) {
        pVersionStruct->loaderLayerInterfaceVersion = 2;
        pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
        pVersionStruct->pfnGetDeviceProcAddr = myvkGetDeviceProcAddr;
        pVersionStruct->pfnGetInstanceProcAddr = myvkGetInstanceProcAddr;
        return VK_SUCCESS;
    }

    // load the layer configuration
    try {
        layer_info = new LayerInfo { // NOLINT (memory management)
            .map = {
#define VKPTR(name) reinterpret_cast<PFN_vkVoidFunction>(name)
                { "vkCreateInstance", VKPTR(myvkCreateInstance) },
                { "vkCreateDevice", VKPTR(myvkCreateDevice) },
                { "vkDestroyDevice", VKPTR(myvkDestroyDevice) },
                { "vkDestroyInstance", VKPTR(myvkDestroyInstance) },
                { "vkCreateSwapchainKHR", VKPTR(myvkCreateSwapchainKHR) },
                { "vkQueuePresentKHR", VKPTR(myvkQueuePresentKHR) },
                { "vkDestroySwapchainKHR", VKPTR(myvkDestroySwapchainKHR) }
#undef VKPTR
            },
            .root = Root()
        };

        fprintf(stderr, "[vkb] Root() done active=%d engaged=%d\n",
            (int)layer_info->root.active(), (int)layer_info->root.engaged_state());
        fflush(stderr);

        // NOTE: do NOT return INIT_FAILED when profile is not loaded.
        // Returning INIT_FAILED makes the loader SKIP the layer entirely,
        // which prevents it from ever engaging even when a profile DOES match
        // later (the loader never re-negotiates). Instead, negotiate SUCCESS
        // and rely on the passthrough logic in myvkCreateXxx (if !profileLoaded()
        // -> call next layer directly). The layer stays transparent in the
        // dispatch chain, so anti-tamper does not reject it.

    } catch (const std::exception& e) {
        std::cerr << "vkb-vk: something went wrong during vkb-vk layer initialization:\n";
        std::cerr << "- " << e.what() << '\n';

        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // emplace function pointers/version
    pVersionStruct->loaderLayerInterfaceVersion = 2;
    pVersionStruct->pfnGetPhysicalDeviceProcAddr = nullptr;
    pVersionStruct->pfnGetDeviceProcAddr = myvkGetDeviceProcAddr;
    pVersionStruct->pfnGetInstanceProcAddr = myvkGetInstanceProcAddr;
    return VK_SUCCESS;
}
