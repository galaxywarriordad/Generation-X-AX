// SPDX-FileCopyrightText: 2026 The Silicon Preservation Society (galaxywarriordad)
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CDVDRingSlab.hpp"
#include "IHardwareModeAdapter.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <thread>

#if __has_include(<ax/topology/pinning.hpp>)
#include <ax/topology/pinning.hpp>
#define PCSX2_AX_HAS_AX_TOPOLOGY 1
#else
#define PCSX2_AX_HAS_AX_TOPOLOGY 0
#endif

namespace pcsx2_ax::cdvd {

class ZeroCopyIsoReader;

class CdvdStreamController {
public:
	CdvdStreamController() noexcept = default;
	~CdvdStreamController() noexcept { stop(); }

	CdvdStreamController(const CdvdStreamController&)            = delete;
	CdvdStreamController& operator=(const CdvdStreamController&) = delete;

	[[nodiscard]] bool start(ZeroCopyIsoReader* reader) noexcept {
		if (reader == nullptr)
			return false;
		stop();
		reader_ = reader;
		running_.store(true, std::memory_order_release);
		worker_quiesced_.store(false, std::memory_order_release);
		worker_ = std::thread([this]() noexcept { worker_loop(); });
		return true;
	}

	void stop() noexcept {
		running_.store(false, std::memory_order_release);
		if (reader_ != nullptr)
			reader_->ring().wake_signal().notify_all();
		if (worker_.joinable())
			worker_.join();
		reader_ = nullptr;
	}

	[[nodiscard]] bool read_sectors_to_iop(uint32_t start_lba,
	                                       uint32_t sector_count,
	                                       uint8_t* iop_dst_ptr,
	                                       std::size_t stride_bytes) noexcept {
		if (reader_ == nullptr || iop_dst_ptr == nullptr || sector_count == 0u || stride_bytes == 0u)
			return false;

		auto& wake = reader_->ring().wake_signal();
		for (uint32_t i = 0u; i < sector_count; ++i) {
			const uint32_t target_lba = start_lba + i;

			const SectorSlot* slot = reader_->peek_sector();
			if (slot == nullptr || slot->lba != target_lba) {
				reader_->request_seek(target_lba, worker_quiesced_);
				while ((slot = reader_->peek_sector()) == nullptr) {
					if (!running_.load(std::memory_order_acquire))
						return false;
					wake.set_sleeping(true);
					slot = reader_->peek_sector();
					if (slot == nullptr && running_.load(std::memory_order_acquire))
						wake.wait_once();
					wake.set_sleeping(false);
				}
			}

			if (slot->status != ReadStatus::Ok) {
				reader_->consume_sector();
				return false;
			}

			const std::span<const uint8_t> src = slot->readable_span();
			const std::size_t copy_bytes = (src.size() < stride_bytes) ? src.size() : stride_bytes;
			std::memcpy(iop_dst_ptr + (static_cast<std::size_t>(i) * stride_bytes),
			            src.data(),
			            copy_bytes);

			reader_->consume_sector();
		}
		return true;
	}

private:
	void worker_loop() noexcept {
#if PCSX2_AX_HAS_AX_TOPOLOGY
		ax::topology::pin_to_housekeeping_core5();
#endif
		auto& wake = reader_->ring().wake_signal();

		while (running_.load(std::memory_order_acquire)) {
			if (reader_->seek_pending()) {
				worker_quiesced_.store(true, std::memory_order_release);
				while (reader_->seek_pending() && running_.load(std::memory_order_acquire)) {
					wake.set_sleeping(true);
					if (reader_->seek_pending() && running_.load(std::memory_order_acquire))
						wake.wait_once();
					wake.set_sleeping(false);
				}
				worker_quiesced_.store(false, std::memory_order_release);
				continue;
			}

			if (reader_->needs_readahead()) {
				const std::size_t produced = reader_->prefetch_burst();
				if (produced == 0u) {
					wake.set_sleeping(true);
					if (reader_->needs_readahead() && running_.load(std::memory_order_acquire))
						wake.wait_once();
					wake.set_sleeping(false);
				}
			} else {
				wake.set_sleeping(true);
				if (!reader_->needs_readahead() && !reader_->seek_pending() &&
				    running_.load(std::memory_order_acquire)) {
					wake.wait_once();
				}
				wake.set_sleeping(false);
			}
		}

		worker_quiesced_.store(true, std::memory_order_release);
	}

	ZeroCopyIsoReader* reader_{nullptr};
	std::thread        worker_{};
	alignas(kCacheLineBytes) std::atomic<bool> running_{false};
	alignas(kCacheLineBytes) std::atomic<bool> worker_quiesced_{true};
};

} // namespace pcsx2_ax::cdvd
