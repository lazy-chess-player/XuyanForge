#include "xuyan/application/provider_connection_service.h"
#include "xuyan/application/provider_generation_service.h"
#include "xuyan/domain/provider_connection.h"
#include "xuyan/platform/credential_store.h"
#include "xuyan/providers/qt_provider_transport.h"

#include <QCoreApplication>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace {

void clearSecret(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

int configureDeepSeek(const std::filesystem::path& database) {
    std::string secret;
    if (!std::getline(std::cin, secret) || secret.empty()) {
        std::cerr << "No credential received on stdin.\n";
        return 2;
    }
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::application::ProviderConnectionService service(database, credentials);
    int revision = 0;
    auto connections = service.list();
    if (!connections.ok()) {
        clearSecret(secret);
        std::cerr << "Unable to open provider catalog.\n";
        return 3;
    }
    for (const auto& item : *connections.value)
        if (item.id == "provider-deepseek") revision = item.revision;
    xuyan::domain::ProviderConnection connection;
    connection.id = "provider-deepseek";
    connection.name = "DeepSeek";
    connection.kind = "deepseek";
    connection.endpoint = "https://api.deepseek.com";
    connection.default_model = "deepseek-flash";
    connection.data_policy = "remote_allowed";
    connection.enabled = true;
    auto saved = service.save("configure-deepseek-v1", std::move(connection), revision, secret);
    clearSecret(secret);
    if (!saved.ok()) {
        std::cerr << "DeepSeek configuration failed: " << saved.error->message << '\n';
        return 4;
    }
    std::cout << "DeepSeek connection configured; credential stored outside the workspace.\n";
    return 0;
}

int testProvider(const std::filesystem::path& database, const std::string& connection_id) {
    xuyan::platform::SystemCredentialStore credentials;
    xuyan::providers::QtProviderTransport transport;
    xuyan::application::ProviderGenerationService service(database, credentials, transport);
    auto report = service.testStructuredGeneration(connection_id, 45000);
    if (!report.ok()) {
        std::cerr << "Provider test failed before completion: " << report.error->message << '\n';
        return 5;
    }
    std::cout << "status=" << report.value->status
              << " provider=" << report.value->provider_kind
              << " model=" << report.value->model_id
              << " json_valid=" << (report.value->json_valid ? "true" : "false")
              << " input_tokens=" << report.value->input_tokens
              << " output_tokens=" << report.value->output_tokens
              << " elapsed_ms=" << report.value->elapsed_ms;
    if (!report.value->failure_kind.empty()) std::cout << " failure=" << report.value->failure_kind;
    std::cout << '\n';
    return report.value->status == "completed" && report.value->json_valid ? 0 : 6;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto arguments = app.arguments();
    if (arguments.size() < 3) {
        std::cerr << "Usage: xuyanforge_provider_cli <configure-deepseek|test> <workspace> [connection-id]\n";
        return 1;
    }
    const auto database = std::filesystem::path(arguments.at(2).toStdWString());
    if (arguments.at(1) == QStringLiteral("configure-deepseek")) return configureDeepSeek(database);
    if (arguments.at(1) == QStringLiteral("test")) {
        const auto id = arguments.size() >= 4 ? arguments.at(3).toStdString() : std::string{"provider-deepseek"};
        return testProvider(database, id);
    }
    std::cerr << "Unknown provider command.\n";
    return 1;
}
