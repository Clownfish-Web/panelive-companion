#include "macos_application_services.hpp"
#include "../../platform/shared/teleprompter/session_device_source.hpp"

#include "../../platform/macos/credentials/macos_startup_password.hpp"
#include "../../platform/macos/platform_services/macos_lan_address.hpp"
#include "../../platform/macos/platform_services/macos_production_installations.hpp"

#include <stdexcept>
#include <studio_remote/discovery/lan_address_selection.hpp>
#include <syslog.h>
#include <unistd.h>
#include <utility>

namespace studio_remote::macos
{
namespace
{
GatewayConfig startup_configuration(ConfigStore& store,
                                    const CommandLine& command_line,
                                    std::stop_token cancellation)
{
  if (cancellation.stop_requested())
    throw std::runtime_error("Companion startup cancelled");
  auto config = store.load();
  if (command_line.providers)
    config.providers = *command_line.providers;
  if (command_line.name)
    config.name = *command_line.name;
  if (config.name.empty())
    config.name = "PaneLive Companion";
  if (command_line.streamlabs_port)
    config.streamlabs_port = *command_line.streamlabs_port;
  if (command_line.gateway_port)
    config.port = *command_line.gateway_port;
  if (command_line.obs_host)
    config.obs_host = *command_line.obs_host;
  if (command_line.obs_port)
    config.obs_port = *command_line.obs_port;
  if (command_line.obs_host || command_line.obs_port)
    config.obs_connection_configured = true;
  config.pairing_address =
      select_configured_lan_ipv4(available_lan_ipv4_addresses(), config.pairing_address)
          .value_or("");
  return config;
}
} // namespace

MacosApplicationServices::MacosApplicationServices(const CommandLine& command_line,
                                                   MacosShutdownSignal& shutdown,
                                                   std::stop_token cancellation,
                                                   EnduranceProcessLog& process_log)
    : store_(command_line.data_directory),
      config_(startup_configuration(store_, command_line, cancellation)),
      credentials_(config_.instance_id)
{
  if (cancellation.stop_requested())
    throw std::runtime_error("Companion startup cancelled");
  if (command_line.read_obs_password)
  {
    import_startup_obs_password(
        [this](std::string_view password)
        {
          auto updated = config_;
          updated.obs_connection_configured = true;
          auto saved =
              store_.save_obs_connection(std::move(updated), credentials_, std::string(password));
          config_ = std::move(saved.config);
          if (!saved.warning.empty())
            ::syslog(LOG_WARNING, "%s", saved.warning.c_str());
        },
        STDIN_FILENO,
        cancellation);
  }
  else
    store_.save(config_);
  // The instance identity and selected OBS credential are persisted before creating TLS keys.
  if (cancellation.stop_requested())
    throw std::runtime_error("Companion startup cancelled");
  log_ = &process_log.start(store_.directory() / "endurance-logs", PROJECT_VERSION);
  audit_ = std::make_unique<AuditStore>(
      store_.directory() / "gateway.db", AuditStore::UnixTimeProvider{}, log_);
  scripts_ = std::make_unique<teleprompter::SqliteRepository>(
      store_.directory() / "gateway.db",
      studio_remote::teleprompter::session_device_source(*audit_),
      [this] { return credentials_.uuid_v4(); },
      []
      {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::system_clock::now().time_since_epoch())
                                              .count());
      },
      []
      {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                              std::chrono::steady_clock::now().time_since_epoch())
                                              .count());
      });
  if (config_.enforce_single_operator_client_mode)
    audit_->enforce_single_operator();
  auto tls = TlsIdentity::load_or_create(store_.directory() / "tls", config_.instance_id, "localhost");
  auto obs_password = [&]
  {
    const auto interaction =
        command_line.open_management ? KeychainInteraction::allow : KeychainInteraction::disallow;
    MacosKeychainInteraction authorization(interaction);
    MacosCredentialStore startup_credentials(config_.instance_id, interaction);
    if (command_line.read_streamlabs_token)
      import_startup_obs_password(
          [&](std::string_view token)
          {
            if (token.empty())
              throw std::invalid_argument("Streamlabs API token must not be empty");
            startup_credentials.put(config_.streamlabs_credential_reference, token);
          },
          STDIN_FILENO,
          cancellation);
    // Unlock encrypted backend storage during explicit startup recovery if necessary.
    // Runtime credential access remains noninteractive.
    const auto providers = selected_providers(config_.providers);
    if (providers.contains("streamlabs"))
      static_cast<void>(startup_credentials.get(config_.streamlabs_credential_reference));
    auto password = providers.contains("obs")
                        ? startup_credentials.get(config_.obs_credential_reference)
                        : std::string{};
    return password;
  }();
  // Startup authorization has ended. Runtime credential access remains noninteractive.
  GatewayCompositionInputs inputs;
  inputs.scripts = scripts_.get();
  gateway_ = std::make_unique<MacosGatewayComposition>(config_,
                                                       store_.directory(),
                                                       std::move(tls),
                                                       std::move(obs_password),
                                                       credentials_,
                                                       *audit_,
                                                       hotkeys_,
                                                       *log_,
                                                       std::move(inputs));
  bool network_recovery = false;
  // Adapter startup failures are fatal. Listener failures leave the adapter running for recovery.
  gateway_->start_adapters();
  try
  {
    gateway_->reconfigure_network(config_.port, config_.allow_non_private_interfaces);
  }
  catch (const std::exception&)
  {
    log_->record("http", "startupListenerUnavailable");
    network_recovery = true;
  }
  create_management(command_line, shutdown, network_recovery);
}

void MacosApplicationServices::create_management(const CommandLine& command_line,
                                                 MacosShutdownSignal& shutdown,
                                                 bool network_recovery)
{
  show_management_ =
      command_line.open_management || !config_.obs_connection_configured || network_recovery;
  MacosManagementServices services{config_,
                                   store_,
                                   credentials_,
                                   *audit_,
                                   scripts_.get(),
                                   *gateway_,
                                   *log_,
                                   shutdown,
                                   status_snapshot_builder_,
                                   [this] { return sign_in_.enabled(); },
                                   [this](bool enabled) { sign_in_.set_enabled(enabled); },
                                   PROJECT_VERSION,
                                   [] { return installed_obs_directory().string(); }};
  management_ = std::make_unique<ManagementWorker>(
      config_.management_interface_mode,
      initial_management_page(
          config_.management_interface_mode, config_.obs_connection_configured, network_recovery),
      [this](ManagementInterfaceMode mode)
      {
        auto updated = config_;
        updated.management_interface_mode = mode;
        try
        {
          store_.save(updated);
        }
        catch (const ConfigurationCommitError&)
        {
          // Return normally so the shared controller also publishes the mode selected on disk.
          ::syslog(LOG_WARNING,
                   "Interface mode saved, but configuration durability is unconfirmed");
        }
        config_ = std::move(updated);
      },
      make_management_callbacks(services));
  if (network_recovery)
    management_->post(
        [warning = gateway_->network_status().warning](ManagementUiController& controller)
        { controller.report_error(warning); });
  connect_client_approval_requests({});
}

void MacosApplicationServices::prepare_normal_shutdown()
{
  disconnect_client_approval_requests();
  management_.reset();
  log_->record("process", "componentStopping", {{"component", "gatewayRuntime"}});
}

MacosApplicationServices::~MacosApplicationServices()
{
  disconnect_client_approval_requests();
  management_.reset();
  gateway_.reset();
}

ManagementWorker& MacosApplicationServices::management() noexcept
{
  return *management_;
}

bool MacosApplicationServices::show_management_on_start() const noexcept
{
  return show_management_;
}

ClientApprovalCallbacks
MacosApplicationServices::client_approval_callbacks(ManagementApprovalRequesters requesters)
{
  return make_macos_client_approval_callbacks(config_,
                                              store_,
                                              *audit_,
                                              *gateway_,
                                              std::move(requesters.role),
                                              std::move(requesters.global_setting),
                                              std::move(requesters.cancel_device));
}

void MacosApplicationServices::connect_client_approval_requests(
    ManagementApprovalRequesters requesters)
{
  gateway_->http().on_client_removed(requesters.cancel_device);
  auto callbacks = client_approval_callbacks(std::move(requesters));
  gateway_->http().on_role_change_requested(std::move(callbacks.request_role));
  gateway_->http().on_global_setting_change_requested(std::move(callbacks.request_global_setting));
}

void MacosApplicationServices::disconnect_client_approval_requests()
{
  gateway_->http().on_client_removed({});
  gateway_->http().on_role_change_requested({});
  gateway_->http().on_global_setting_change_requested({});
}

void MacosApplicationServices::on_pairing_closed(std::function<void()> handler)
{
  gateway_->pairing().on_closed(std::move(handler));
}
} // namespace studio_remote::macos
