/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <utility>

namespace vk {
    /// Kernel-side drm_syncobj timeline bridge for cross-vendor GPU sync.
    ///
    /// Vulkan has no cross-vendor timeline-semaphore handle type (radv rejects
    /// VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT timeline export). The Linux
    /// kernel drm_syncobj timeline, however, is understood by both the NVIDIA
    /// and RADV drivers. We bridge it to Vulkan via a sync_file fd:
    ///
    ///   signal point N  ->  export sync_file  ->  import into Vulkan binary
    ///   semaphore (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT)
    ///
    /// A sync_file is a cross-device primitive, so the imported binary semaphore
    /// works on either GPU. This preserves frame_index ordering with zero CPU
    /// overhead (the GPU waits on the sync_file directly).
    class DrmSyncTimeline {
    public:
        /// open the drm device at @p path (e.g. "/dev/dri/card2") and create a
        /// timeline syncobj. throws std::runtime_error on failure.
        explicit DrmSyncTimeline(const char* drmPath);

        DrmSyncTimeline(const DrmSyncTimeline&) = delete;
        DrmSyncTimeline& operator=(const DrmSyncTimeline&) = delete;
        DrmSyncTimeline(DrmSyncTimeline&& other) noexcept
            : m_drmFd(other.m_drmFd), m_handle(other.m_handle) {
            other.m_drmFd = -1;
            other.m_handle = 0;
        }
        DrmSyncTimeline& operator=(DrmSyncTimeline&& other) noexcept {
            if (this != &other) {
                destroy();
                m_drmFd = other.m_drmFd;
                m_handle = other.m_handle;
                other.m_drmFd = -1;
                other.m_handle = 0;
            }
            return *this;
        }

        ~DrmSyncTimeline() { destroy(); }

        /// signal a timeline point (makes it available / submitted)
        void signal(uint64_t point) const;

        /// export a timeline point as a sync_file fd.
        /// the returned fd is owned by the caller and must be close()d.
        [[nodiscard]] int exportSyncFile(uint64_t point) const;

        /// import a sync_file fd as a timeline point on this syncobj
        void importSyncFile(int fd, uint64_t point) const;

        /// block the calling thread (kernel deep-sleep, no CPU busy-wait) until
        /// @p point is signalled on this syncobj. Used to let the backend
        /// (RADV) wait for a frame point produced by the app (NVIDIA)
        /// without importing the foreign sync_file into a Vulkan semaphore
        /// (which RADV rejects cross-drm). Wakes on the hardware interrupt.
        /// @param point the timeline point to wait for
        /// @param timeoutNsec absolute timeout in ns, or INT64_MAX to wait forever
        void wait(uint64_t point, int64_t timeoutNsec = INT64_MAX) const;

        /// export the WHOLE syncobj container as a single fd. This shares the
        /// kernel object itself (not a timeline point), so a peer that imports it
        /// sees every signal()/importSyncFile() done on the original. This is the
        /// "share the whole container" trick: pass this fd ONCE at init and the
        /// peer can drmWait() on any future point with ZERO per-frame IPC.
        /// @return an fd owned by the caller (close() it)
        [[nodiscard]] int shareToFd() const;

        /// import a shared syncobj container fd (from shareToFd()) into this
        /// drm fd. After this, signal() on the original object is visible here.
        static DrmSyncTimeline fromFd(const char* drmPath, int sharedFd);

        /// get the raw syncobj handle (used internally / for debugging)
        [[nodiscard]] uint32_t handle() const { return m_handle; }

    private:
        DrmSyncTimeline(int drmFd, uint32_t handle) : m_drmFd(drmFd), m_handle(handle) {}

        int m_drmFd{ -1 };
        uint32_t m_handle{ 0 };

        void destroy() noexcept;
    };
}
