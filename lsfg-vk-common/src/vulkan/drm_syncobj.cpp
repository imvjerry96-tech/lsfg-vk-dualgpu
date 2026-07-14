/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "lsfg-vk-common/vulkan/drm_syncobj.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <libdrm/drm.h>
#include <xf86drm.h>

#include <stdexcept>
#include <string>

namespace vk {

    DrmSyncTimeline::DrmSyncTimeline(const char* drmPath) {
        m_drmFd = open(drmPath, O_RDWR);
        if (m_drmFd < 0)
            throw std::runtime_error(
                std::string("DrmSyncTimeline: cannot open ") + (drmPath ? drmPath : "nullptr"));

        struct drm_syncobj_create create{};
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_CREATE, &create))
            throw std::runtime_error("DrmSyncTimeline: drm_syncobj create failed");
        m_handle = create.handle;
    }

    void DrmSyncTimeline::destroy() noexcept {
        if (m_handle && m_drmFd >= 0) {
            struct drm_syncobj_destroy destroy{};
            destroy.handle = m_handle;
            drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
        }
        if (m_drmFd >= 0) {
            close(m_drmFd);
            m_drmFd = -1;
        }
        m_handle = 0;
    }

    void DrmSyncTimeline::signal(uint64_t point) const {
        struct drm_syncobj_timeline_array arr{};
        const uint32_t h = m_handle;
        const uint64_t p = point;
        arr.handles = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&h));
        arr.points = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&p));
        arr.count_handles = 1;
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_TIMELINE_SIGNAL, &arr))
            throw std::runtime_error("DrmSyncTimeline: timeline signal failed");
    }

    int DrmSyncTimeline::exportSyncFile(uint64_t point) const {
        struct drm_syncobj_handle h{};
        h.handle = m_handle;
        h.flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE
                | DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_TIMELINE;
        h.point = point;
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &h))
            throw std::runtime_error("DrmSyncTimeline: export sync_file failed");
        return h.fd;
    }

    void DrmSyncTimeline::importSyncFile(int fd, uint64_t point) const {
        struct drm_syncobj_handle h{};
        h.handle = m_handle;
        h.flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE
                | DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_TIMELINE;
        h.fd = fd;
        h.point = point;
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &h))
            throw std::runtime_error("DrmSyncTimeline: import sync_file failed");
    }

    void DrmSyncTimeline::wait(uint64_t point, int64_t timeoutNsec) const {
        struct drm_syncobj_timeline_wait w{};
        const uint32_t h = m_handle;
        const uint64_t p = point;
        w.handles = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&h));
        w.points = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&p));
        w.timeout_nsec = timeoutNsec;
        w.count_handles = 1;
        w.flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_TIMELINE_WAIT, &w))
            throw std::runtime_error("DrmSyncTimeline: timeline wait failed");
    }

    int DrmSyncTimeline::shareToFd() const {
        struct drm_syncobj_handle h{};
        h.handle = m_handle;
        h.flags = 0; // share the WHOLE container, not a timeline point
        if (drmIoctl(m_drmFd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &h))
            throw std::runtime_error("DrmSyncTimeline: shareToFd failed");
        return h.fd;
    }

    DrmSyncTimeline DrmSyncTimeline::fromFd(const char* drmPath, int sharedFd) {
        int drmFd = open(drmPath, O_RDWR);
        if (drmFd < 0)
            throw std::runtime_error(
                std::string("DrmSyncTimeline: cannot open ") + (drmPath ? drmPath : "nullptr"));
        struct drm_syncobj_handle h{};
        h.fd = sharedFd;
        h.flags = 0; // import the WHOLE container
        if (drmIoctl(drmFd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &h)) {
            close(drmFd);
            throw std::runtime_error("DrmSyncTimeline: fromFd failed");
        }
        return DrmSyncTimeline(drmFd, h.handle);
    }

}
