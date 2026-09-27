#include "gateway_command_processor.hpp"

#include <algorithm>
#include <chrono>

namespace studio_remote
{
namespace
{
json collection_rejection(const ProductionCollectionSelection& selection, std::string reason)
{
  return {
      {"status", "rejected"}, {"requestId", selection.request_id}, {"error", std::move(reason)}};
}
} // namespace

json GatewayCommandProcessor::select_collection(const Device& device,
                                                const ProductionCollectionSelection& selection,
                                                IObsAdapter& provider,
                                                CriticalLiveControlService& controls)
{
  if (!authorized(device))
    return collection_rejection(selection, "permission-required");
  if (device.production_provider != provider.backend_info().id)
    return collection_rejection(selection, "invalid_production_provider");

  std::lock_guard admission(collection_admission_mutex_);
  if (!authorized(device))
    return collection_rejection(selection, "permission-required");
  const auto key = "collection:" + device.production_provider + ":" + device.id + ":" +
                   std::to_string(device.pairing_generation) + ":" + selection.request_id;
  if (const auto cached = cached_response(key))
    return *cached;
  const auto snapshot = state_.production_catalogs();
  if (!provider.connected() || !snapshot.collections)
    return collection_rejection(selection, "production-backend-unavailable");
  if (selection.state_sequence != snapshot.state_sequence)
    return collection_rejection(selection, "state-revision-mismatch");
  const auto& catalog = *snapshot.collections;
  if (selection.expected_current_collection_id != catalog.selected_id)
    return collection_rejection(selection, "collection-changed");
  if (!std::ranges::any_of(catalog.choices,
                           [&](const auto& item) { return item.id == selection.collection_id; }))
    return collection_rejection(selection, "collection-not-found");
  const auto state = state_.command_state_snapshot();
  if (state.record_active() || state.stream_active())
    return collection_rejection(selection, "outputs-active");
  auto reservation = controls.reserve_configuration();
  if (!reservation)
    return collection_rejection(selection, "collection-load-in-progress");

  const auto operation_id = std::to_string(monotonic_ns());
  json accepted = {{"status", "accepted"},
                   {"requestId", selection.request_id},
                   {"commandId", selection.request_id},
                   {"operationId", operation_id},
                   {"stateSequence", snapshot.state_sequence}};
  {
    std::lock_guard lock(operation_mutex_);
    if (stopping_ || operations_.size() >= 128)
      return collection_rejection(selection, "production-backend-unavailable");
    auto insertion = cache_accepted_response(key, std::move(accepted));
    accepted = std::move(insertion.response);
    if (!insertion.inserted)
      return accepted;
    operations_.push_back(QueuedCollectionSelection{
        device, selection, catalog.revision, operation_id, &provider, std::move(reservation)});
  }
  operation_cv_.notify_one();
  return accepted;
}

void GatewayCommandProcessor::run_collection_selection(const QueuedCollectionSelection& operation)
{
  const auto& selection = operation.selection;
  const auto fail = [&](std::string reason)
  {
    audit_.command(operation.device.id,
                   "select-scene-collection",
                   selection.collection_id,
                   "failed",
                   selection.request_id);
    emit({{"type", "commandFailed"},
          {"commandId", selection.request_id},
          {"requestId", selection.request_id},
          {"operationId", operation.operation_id},
          {"reason", std::move(reason)},
          {"_deviceId", operation.device.id}});
  };
  try
  {
    if (stopping_ || !operation.provider->connected())
      return fail("production-backend-unavailable");
    if (!authorized(operation.device))
      return fail("permission-required");
    const auto catalog = operation.provider->scene_collection_catalog();
    // Native catalog reads can block while the owning pairing or its authority changes.
    if (!authorized(operation.device))
      return fail("permission-required");
    if (stopping_)
      return fail("production-backend-unavailable");
    if (!catalog || catalog->selected_id != selection.expected_current_collection_id)
      return fail("collection-changed");
    operation.provider->select_scene_collection(selection.collection_id,
                                                operation.catalog_revision);
    request_reconcile_();
    const auto deadline = Clock::now() + std::chrono::seconds(12);
    while (!stopping_ && Clock::now() < deadline)
    {
      const auto snapshot = state_.production_catalogs();
      if (snapshot.collections && snapshot.collections->selected_id == selection.collection_id &&
          snapshot.baseline_sequence > selection.state_sequence)
      {
        audit_.command(operation.device.id,
                       "select-scene-collection",
                       selection.collection_id,
                       "completed",
                       selection.request_id);
        emit({{"type", "commandCompleted"},
              {"commandId", selection.request_id},
              {"requestId", selection.request_id},
              {"operationId", operation.operation_id},
              {"resultingRevision", snapshot.state_sequence},
              {"_deviceId", operation.device.id}});
        return;
      }
      std::unique_lock lock(operation_mutex_);
      operation_cv_.wait_for(
          lock, std::chrono::milliseconds(50), [this] { return stopping_.load(); });
    }
    fail("collection-load-failed");
  }
  catch (const ProductionConfigurationError& error)
  {
    request_reconcile_();
    fail(error.what());
  }
  catch (const std::exception&)
  {
    request_reconcile_();
    fail("collection-load-failed");
  }
}
} // namespace studio_remote
