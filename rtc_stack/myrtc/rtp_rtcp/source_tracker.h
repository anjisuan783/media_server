/*
 *  Copyright (c) 2019 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_RTP_RTCP_SOURCE_SOURCE_TRACKER_H_
#define MODULES_RTP_RTCP_SOURCE_SOURCE_TRACKER_H_

#include <cstdint>
#include <list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "optional"
#include "api/rtp_packet_infos.h"
#include "api/rtp_source.h"
#include "rtc_base/critical_section.h"
#include "rtc_base/time_utils.h"
#include "rtc_base/clock.h"

namespace webrtc {

//
// Tracker for `RTCRtpContributingSource` and `RTCRtpSynchronizationSource`:
//   - https://w3c.github.io/webrtc-pc/#dom-rtcrtpcontributingsource
//   - https://w3c.github.io/webrtc-pc/#dom-rtcrtpsynchronizationsource
//
class SourceTracker {
 public:
  // Amount of time before the entry associated with an update is removed. See:
  // https://w3c.github.io/webrtc-pc/#dom-rtcrtpreceiver-getcontributingsources
  static constexpr int64_t kTimeoutMs = 10000;  // 10 seconds

  explicit SourceTracker(Clock* clock);

  SourceTracker(const SourceTracker& other) = delete;
  SourceTracker(SourceTracker&& other) = delete;
  SourceTracker& operator=(const SourceTracker& other) = delete;
  SourceTracker& operator=(SourceTracker&& other) = delete;

  // Updates the source entries when a frame is delivered to the
  // RTCRtpReceiver's MediaStreamTrack.
  void OnFrameDelivered(const RtpPacketInfos& packet_infos);

  // Returns an |RtpSource| for each unique SSRC and CSRC identifier updated in
  // the last |kTimeoutMs| milliseconds. Entries appear in reverse chronological
  // order (i.e. with the most recently updated entries appearing first).
  std::vector<RtpSource> GetSources() const;

 private:
  struct SourceKey {
    SourceKey(RtpSourceType source_type, uint32_t source)
        : source_type(source_type), source(source) {}

    // Type of |source|.
    RtpSourceType source_type;

    // CSRC or SSRC identifier of the contributing or synchronization source.
    uint32_t source;
  };

  struct SourceKeyComparator {
    bool operator()(const SourceKey& lhs, const SourceKey& rhs) const {
      return (lhs.source_type == rhs.source_type) && (lhs.source == rhs.source);
    }
  };

  struct SourceKeyHasher {
    size_t operator()(const SourceKey& value) const {
      return static_cast<size_t>(value.source_type) +
             static_cast<size_t>(value.source) * 11076425802534262905ULL;
    }
  };

  struct SourceEntry {
    // Timestamp indicating the most recent time a frame from an RTP packet,
    // originating from this source, was delivered to the RTCRtpReceiver's
    // MediaStreamTrack. Its reference clock is the outer class's |clock_|.
    int64_t timestamp_ms;

    // Audio level from an RFC 6464 or RFC 6465 header extension received with
    // the most recent packet used to assemble the frame associated with
    // |timestamp_ms|. May be absent. Only relevant for audio receivers. See the
    // specs for `RTCRtpContributingSource` for more info.
    std::optional<uint8_t> audio_level;

    // RTP timestamp of the most recent packet used to assemble the frame
    // associated with |timestamp_ms|.
    uint32_t rtp_timestamp;
  };

  using SourceList = std::list<std::pair<const SourceKey, SourceEntry>>;
  using SourceMap = std::unordered_map<SourceKey,
                                       SourceList::iterator,
                                       SourceKeyHasher,
                                       SourceKeyComparator>;

  // Updates an entry by creating it (if it didn't previously exist) and moving
  // it to the front of the list. Returns a reference to the entry.
  SourceEntry& UpdateEntry(const SourceKey& key)
      RTC_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  // Removes entries that have timed out. Marked as "const" so that we can do
  // pruning in getters.
  void PruneEntries(int64_t now_ms) const RTC_EXCLUSIVE_LOCKS_REQUIRED(lock_);

  Clock* const clock_;
  rtc::CriticalSection lock_;

  // Entries are stored in reverse chronological order (i.e. with the most
  // recently updated entries appearing first). Mutability is needed for timeout
  // pruning in const functions.
  mutable SourceList list_ RTC_GUARDED_BY(lock_);
  mutable SourceMap map_ RTC_GUARDED_BY(lock_);
};

}  // namespace webrtc

#endif  // MODULES_RTP_RTCP_SOURCE_SOURCE_TRACKER_H_