/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_
#define MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_

#include <stdint.h>

#include "optional"
#include "api/network_types.h"
#include "api/webrtc_key_value_config.h"
#include "rtc_base/data_rate.h"
#include "rtc_base/timestamp.h"
#include "congestion_controller/link_capacity_estimator.h"
#include "remote_bitrate_estimator/bwe_defines.h"
#include "rtc_base/field_trial_parser.h"

namespace webrtc {
// A rate control implementation based on additive increases of
// bitrate when no over-use is detected and multiplicative decreases when
// over-uses are detected. When we think the available bandwidth has changes or
// is unknown, we will switch to a "slow-start mode" where we increase
// multiplicatively.
class AimdRateControl {
 public:
  explicit AimdRateControl(const WebRtcKeyValueConfig* key_value_config);
  AimdRateControl(const WebRtcKeyValueConfig* key_value_config, bool send_side);
  ~AimdRateControl();

  // Returns true if the target bitrate has been initialized. This happens
  // either if it has been explicitly set via SetStartBitrate/SetEstimate, or if
  // we have measured a throughput.
  bool ValidEstimate() const;
  void SetStartBitrate(DataRate start_bitrate);
  void SetMinBitrate(DataRate min_bitrate);
  TimeDelta GetFeedbackInterval() const;

  // Returns true if the bitrate estimate hasn't been changed for more than
  // an RTT, or if the estimated_throughput is less than half of the current
  // estimate. Should be used to decide if we should reduce the rate further
  // when over-using.
  bool TimeToReduceFurther(Timestamp at_time,
                           DataRate estimated_throughput) const;
  // As above. To be used if overusing before we have measured a throughput.
  bool InitialTimeToReduceFurther(Timestamp at_time) const;

  DataRate LatestEstimate() const;
  void SetRtt(TimeDelta rtt);
  DataRate Update(const RateControlInput* input, Timestamp at_time);
  void SetInApplicationLimitedRegion(bool in_alr);
  void SetEstimate(DataRate bitrate, Timestamp at_time);
  void SetNetworkStateEstimate(
      const std::optional<NetworkStateEstimate>& estimate);

  // Returns the increase rate when used bandwidth is near the link capacity.
  double GetNearMaxIncreaseRateBpsPerSecond() const;
  // Returns the expected time between overuse signals (assuming steady state).
  TimeDelta GetExpectedBandwidthPeriod() const;

 private:
  friend class GoogCcStatePrinter;
  // Update the target bitrate based on, among other things, the current rate
  // control state, the current target bitrate and the estimated throughput.
  // When in the "increase" state the bitrate will be increased either
  // additively or multiplicatively depending on the rate control region. When
  // in the "decrease" state the bitrate will be decreased to slightly below the
  // current throughput. When in the "hold" state the bitrate will be kept
  // constant to allow built up queues to drain.
  DataRate ChangeBitrate(DataRate current_bitrate,
                         const RateControlInput& input,
                         Timestamp at_time);
  // Clamps new_bitrate to within the configured min bitrate and a linear
  // function of the throughput, so that the new bitrate can't grow too
  // large compared to the bitrate actually being received by the other end.
  DataRate ClampBitrate(DataRate new_bitrate,
                        DataRate estimated_throughput) const;
  DataRate MultiplicativeRateIncrease(Timestamp at_time,
                                      Timestamp last_ms,
                                      DataRate current_bitrate) const;
  DataRate AdditiveRateIncrease(Timestamp at_time, Timestamp last_time) const;
  void UpdateChangePeriod(Timestamp at_time);
  void ChangeState(const RateControlInput& input, Timestamp at_time);

  DataRate min_configured_bitrate_;
  DataRate max_configured_bitrate_;
  DataRate current_bitrate_;
  DataRate latest_estimated_throughput_;
  LinkCapacityEstimator link_capacity_;
  std::optional<NetworkStateEstimate> network_estimate_;
  RateControlState rate_control_state_;
  Timestamp time_last_bitrate_change_;
  Timestamp time_last_bitrate_decrease_;
  Timestamp time_first_throughput_estimate_;
  bool bitrate_is_initialized_;
  double beta_;
  bool in_alr_;
  TimeDelta rtt_;
  const bool send_side_;
  const bool in_experiment_;
  // Allow the delay based estimate to only increase as long as application
  // limited region (alr) is not detected.
  const bool no_bitrate_increase_in_alr_;
  const bool smoothing_experiment_;
  // Use estimated link capacity lower bound if it is higher than the
  // acknowledged rate when backing off due to overuse.
  const bool estimate_bounded_backoff_;
  // Use estimated link capacity upper bound as upper limit for increasing
  // bitrate over the acknowledged rate.
  const bool estimate_bounded_increase_;
  std::optional<DataRate> last_decrease_;
  FieldTrialOptional<TimeDelta> initial_backoff_interval_;
  FieldTrialParameter<DataRate> low_throughput_threshold_;
  // Deprecated, enable |estimate_bounded_backoff_| instead.
  FieldTrialOptional<double> capacity_deviation_ratio_threshold_;
  // Deprecated, enable |estimate_bounded_increase_| instead.
  FieldTrialOptional<double> capacity_limit_deviation_factor_;
};
}  // namespace webrtc

#endif  // MODULES_REMOTE_BITRATE_ESTIMATOR_AIMD_RATE_CONTROL_H_
