#pragma once

#include "xuyan/domain/scenario.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

struct DemoWorldStage {
    std::string id;
    std::string title;
    std::string detail;
    bool ready{false};
};

struct DemoWorldProgress {
    std::vector<DemoWorldStage> stages;
    std::string source_id;
    std::string world_version_id;
    std::string snapshot_id;
    std::string character_instance_id;
    int completed{0};
    bool ready{false};
};

class DemoWorldService {
public:
    explicit DemoWorldService(std::filesystem::path database_path);

    xuyan::domain::Result<DemoWorldProgress> inspect();
    xuyan::domain::Result<DemoWorldProgress> install();

    static std::string sourceText();

private:
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
