#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::windows
{

struct OmtSourceDescriptor
{
  std::string address;
  std::string host;
  std::string label;

  bool operator==(const OmtSourceDescriptor&) const = default;
};

struct OmtAudioFrame
{
  std::uint32_t sample_rate = 0;
  std::uint32_t channels = 0;
  std::uint32_t samples_per_channel = 0;
  std::vector<float> planar_samples;
};

std::optional<OmtSourceDescriptor> parse_omt_source(std::string_view address);
std::string local_omt_hostname();
std::optional<OmtSourceDescriptor>
select_vmix_omt_headphones_source(const std::vector<std::string>& addresses,
                                  std::string_view configured_host,
                                  std::string_view local_hostname);
std::optional<OmtSourceDescriptor>
select_vmix_omt_master_source(const std::vector<std::string>& addresses,
                              std::string_view configured_host,
                              std::string_view local_hostname);
std::optional<std::vector<float>> normalize_omt_audio_stereo(const OmtAudioFrame& frame);

class IOmtAudioReceiver
{
public:
  virtual ~IOmtAudioReceiver() = default;
  virtual std::optional<OmtAudioFrame> receive(std::uint32_t timeout_ms) = 0;
};

class OmtAudioReceiver final : public IOmtAudioReceiver
{
public:
  explicit OmtAudioReceiver(std::string address);
  ~OmtAudioReceiver();

  OmtAudioReceiver(const OmtAudioReceiver&) = delete;
  OmtAudioReceiver& operator=(const OmtAudioReceiver&) = delete;

  std::optional<OmtAudioFrame> receive(std::uint32_t timeout_ms) override;

private:
  struct State;
  std::unique_ptr<State> state_;
};

class IOmtRuntime
{
public:
  virtual ~IOmtRuntime() = default;
  virtual std::vector<std::string> discover_sources() const = 0;
  virtual std::unique_ptr<IOmtAudioReceiver> create_audio_receiver(std::string address) const = 0;
};

class OmtRuntime final : public IOmtRuntime
{
public:
  OmtRuntime();
  ~OmtRuntime();

  OmtRuntime(const OmtRuntime&) = delete;
  OmtRuntime& operator=(const OmtRuntime&) = delete;

  std::vector<std::string> discover_sources() const override;
  std::unique_ptr<IOmtAudioReceiver> create_audio_receiver(std::string address) const override;
};

class OmtRuntimeProvider
{
public:
  std::shared_ptr<OmtRuntime> acquire();

private:
  std::mutex mutex_;
  std::shared_ptr<OmtRuntime> runtime_;
};

} // namespace studio_remote::windows
