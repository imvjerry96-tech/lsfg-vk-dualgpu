/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "lsfg-vk-common/vulkan/timeline_semaphore.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"

#include <cstdint>
#include <optional>
#include <cstdlib>

#include <vulkan/vulkan_core.h>

/// select external semaphore handle type (same as semaphore.cpp)
static VkExternalSemaphoreHandleTypeFlagBits selectedTimelineSemHandleType() {
    return std::getenv("VKBPVK_CROSS_GPU") != nullptr
        ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
        : VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
}

/// when cross-vendor, radv cannot export timeline semaphores via SYNC_FD.
/// fall back to a plain binary semaphore (still SYNC_FD for cross-device share).
/// note: vkb-vk only uses .handle() in queue submits, never .signal()/.wait(),
/// so a binary semaphore preserves submit ordering just fine.
static bool useBinarySemaphoreFallback() {
    return std::getenv("VKBPVK_CROSS_GPU") != nullptr;
}

using namespace vk;

namespace {
    /// create a timeline semaphore
    ls::owned_ptr<VkSemaphore> createTimelineSemaphore(const vk::Vulkan& vk, uint32_t initial,
            std::optional<int> importFd, std::optional<int*> exportFd) {
        VkSemaphore handle{};

        if (useBinarySemaphoreFallback()) {
            // cross-vendor: binary semaphore with SYNC_FD
            const VkExportSemaphoreCreateInfo exportInfo{
                .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
                .handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
            };
            const VkSemaphoreCreateInfo semaphoreInfo{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = ( importFd.has_value() || exportFd.has_value() ) ? &exportInfo : nullptr,
            };
            auto res = vk.df().CreateSemaphore(vk.dev(), &semaphoreInfo, VK_NULL_HANDLE, &handle);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkCreateSemaphore() failed (binary fallback)");

            if (importFd.has_value()) {
                const VkImportSemaphoreFdInfoKHR importInfo{
                    .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
                    .semaphore = handle,
                    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
                    .fd = *importFd
                };
                res = vk.df().ImportSemaphoreFdKHR(vk.dev(), &importInfo);
                if (res != VK_SUCCESS)
                    throw ls::vulkan_error(res, "vkImportSemaphoreFdKHR() failed");
            }
            if (exportFd.has_value()) {
                const VkSemaphoreGetFdInfoKHR getFdInfo{
                    .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
                    .semaphore = handle,
                    .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
                };
                int fd{};
                res = vk.df().GetSemaphoreFdKHR(vk.dev(), &getFdInfo, &fd);
                if (res != VK_SUCCESS)
                    throw ls::vulkan_error(res, "vkGetSemaphoreFdKHR() failed");
                **exportFd = fd;
            }
            return ls::owned_ptr<VkSemaphore>(
                new VkSemaphore(handle),
                [dev = vk.dev(), defunc = vk.df().DestroySemaphore](VkSemaphore& semaphore) {
                    defunc(dev, semaphore, VK_NULL_HANDLE);
                }
            );
        }

        const VkExportSemaphoreCreateInfo exportInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = selectedTimelineSemHandleType()
        };
        const VkSemaphoreTypeCreateInfo typeInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = ( importFd.has_value() || exportFd.has_value() ) ? &exportInfo : nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = initial
        };
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &typeInfo,
        };
        auto res = vk.df().CreateSemaphore(vk.dev(), &semaphoreInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateSemaphore() failed");

        if (importFd.has_value()) {
            // import semaphore from fd
            const VkImportSemaphoreFdInfoKHR importInfo{
                .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
                .semaphore = handle,
                .handleType = selectedTimelineSemHandleType(),
                .fd = *importFd // closes the fd
            };
            res = vk.df().ImportSemaphoreFdKHR(vk.dev(), &importInfo);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkImportSemaphoreFdKHR() failed");
        }

        if (exportFd.has_value()) {
            // export semaphore to fd
            const VkSemaphoreGetFdInfoKHR getFdInfo{
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
                .semaphore = handle,
                .handleType = selectedTimelineSemHandleType()
            };
            int fd{};
            res = vk.df().GetSemaphoreFdKHR(vk.dev(), &getFdInfo, &fd);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkGetSemaphoreFdKHR() failed");

            **exportFd = fd;
        }

        return ls::owned_ptr<VkSemaphore>(
            new VkSemaphore(handle),
            [dev = vk.dev(), defunc = vk.df().DestroySemaphore](VkSemaphore& semaphore) {
                defunc(dev, semaphore, VK_NULL_HANDLE);
            }
        );
    }
}

TimelineSemaphore::TimelineSemaphore(const vk::Vulkan& vk, uint32_t initial,
        std::optional<int> importFd, std::optional<int*> exportFd)
    : semaphore(createTimelineSemaphore(vk, initial, importFd, exportFd)),
      isBinary(useBinarySemaphoreFallback()) {}

void TimelineSemaphore::signal(const vk::Vulkan& vk, uint64_t value) const {
    const VkSemaphoreSignalInfo signalInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO,
        .semaphore = *this->semaphore,
        .value = value
    };
    auto res = vk.df().SignalSemaphoreKHR(vk.dev(), &signalInfo);
    if (res != VK_SUCCESS)
        throw ls::vulkan_error(res, "vkSignalSemaphore() failed");
}

bool TimelineSemaphore::wait(const vk::Vulkan& vk, uint64_t value, uint64_t timeout) const {
    const VkSemaphoreWaitInfo waitInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .semaphoreCount = 1,
        .pSemaphores = &*this->semaphore,
        .pValues = &value
    };
    auto res = vk.df().WaitSemaphoresKHR(vk.dev(), &waitInfo, timeout);
    if (res != VK_SUCCESS && res != VK_TIMEOUT)
        throw ls::vulkan_error(res, "vkWaitSemaphores() failed");

    return res == VK_SUCCESS;
}
