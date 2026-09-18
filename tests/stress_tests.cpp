#include "xuyan/storage/workspace_repository.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void removeDatabase(const std::filesystem::path& path) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + "-wal", ignored);
    std::filesystem::remove(path.string() + "-shm", ignored);
}

} // namespace

int main() {
    try {
        const auto path = std::filesystem::temp_directory_path() / "xuyanforge-tests" / "stress.sqlite";
        std::filesystem::create_directories(path.parent_path()); removeDatabase(path);
        const auto started = std::chrono::steady_clock::now();
        xuyan::storage::WorkspaceRepository repository(path);
        require(repository.ensureDemo().ok(), "stress workspace must initialize");
        std::vector<xuyan::domain::WorldEntity> entities;
        entities.reserve(10000);
        for (int index = 0; index < 10000; ++index) {
            xuyan::domain::WorldEntity entity;
            entity.id = "stress-entity-" + std::to_string(index);
            entity.world_id = "world-stress"; entity.kind = index % 2 == 0 ? "event" : "character";
            entity.name = "长篇条目" + std::to_string(index);
            entity.description = std::string(256, static_cast<char>('a' + index % 26));
            entity.aliases = {"别名" + std::to_string(index)}; entity.tags = {"压力回归", "中文"};
            entity.revision = 1;
            entities.push_back(std::move(entity));
        }
        auto imported = repository.importEntities("stress-import", "stress-package-hash", std::move(entities));
        require(imported.ok() && *imported.value == 10000, "ten thousand entities must import in one transaction");
        auto first = repository.searchEntities("长篇条目", "", 0, 25);
        auto middle = repository.searchEntities("长篇条目", "", 4975, 25);
        auto last = repository.searchEntities("长篇条目", "", 9975, 25);
        require(first.ok() && middle.ok() && last.ok() && first.value->total == 10000
                    && first.value->items.size() == 25 && middle.value->items.size() == 25 && last.value->items.size() == 25,
                "large-world search must stay paged instead of loading the full result set");
        const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - started).count();
        require(elapsed < 30, "local 10k-entity import and three paged searches exceeded 30 seconds");
        removeDatabase(path);
        std::cout << "XuyanForge 10k-entity stress smoke passed in " << elapsed << " s.\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "Stress test failure: " << exception.what() << '\n';
        return 1;
    }
}
