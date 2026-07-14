/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "lsfg-vk-common/vulkan/semaphore.hpp"
#include "lsfg-vk-common/helpers/errors.hpp"
#include "lsfg-vk-common/helpers/pointers.hpp"
#include "lsfg-vk-common/vulkan/vulkan.hpp"

#include <optional>
#include <cstdlib>

#include <vulkan/vulkan_core.h>

/// select external semaphore handle type
/// OPAQUE_FD works for same-vendor; SYNC_FD is required cross-vendor
static VkExternalSemaphoreHandleTypeFlagBits selectedSemHandleType() {
    return std::getenv("VKBPVK_CROSS_GPU") != nullptr
        ? VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT
        : VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
}

using namespace vk;

namespace {
    /// create a semaphore
    ls::owned_ptr<VkSemaphore> createSemaphore(const vk::Vulkan& vk, std::optional<int> fd) {
        VkSemaphore handle{};

        const VkExportSemaphoreCreateInfo exportInfo{
            .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .handleTypes = selectedSemHandleType()
        };
        const VkSemaphoreCreateInfo semaphoreInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = fd.has_value() ? &exportInfo : nullptr
        };
        auto res = vk.df().CreateSemaphore(vk.dev(), &semaphoreInfo, VK_NULL_HANDLE, &handle);
        if (res != VK_SUCCESS)
            throw ls::vulkan_error(res, "vkCreateSemaphore() failed");

        if (fd.has_value()) {
            // import semaphore from fd
            const VkImportSemaphoreFdInfoKHR importInfo{
                .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
                .semaphore = handle,
                .handleType = selectedSemHandleType(),
                .fd = *fd // closes the fd
            };
            res = vk.df().ImportSemaphoreFdKHR(vk.dev(), &importInfo);
            if (res != VK_SUCCESS)
                throw ls::vulkan_error(res, "vkImportSemaphoreFdKHR() failed");
        }

        return ls::owned_ptr<VkSemaphore>(
            new VkSemaphore(handle),
            [dev = vk.dev(), defunc = vk.df().DestroySemaphore](VkSemaphore& semaphore) {
                defunc(dev, semaphore, VK_NULL_HANDLE);
            }
        );
    }
}

Semaphore::Semaphore(const vk::Vulkan& vk, std::optional<int> fd)
    : semaphore(createSemaphore(vk, fd)) {}
