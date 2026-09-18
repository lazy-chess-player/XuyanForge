#include "xuyan/application/package_service.h"

#include "xuyan/domain/hash.h"
#include "xuyan/domain/world_entity.h"
#include "xuyan/package/json.h"
#include "xuyan/package/zip_archive.h"
#include "xuyan/storage/workspace_repository.h"

#include <map>
#include <limits>
#include <sstream>

namespace xuyan::application {
namespace {

using xuyan::domain::Error;
using xuyan::domain::ErrorCode;
using xuyan::domain::Result;
using xuyan::domain::WorldEntity;
using xuyan::package::JsonValue;

Error packageError(std::string message) {
    return Error{ErrorCode::validation_failed, std::move(message), false, "检查包版本、摘要和内容后重试"};
}

JsonValue strings(const std::vector<std::string>& values) {
    JsonValue::Array array;
    for (const auto& value : values) array.emplace_back(value);
    return JsonValue(std::move(array));
}

JsonValue encodeEntity(const WorldEntity& entity) {
    return JsonValue::Object{
        {"aliases", strings(entity.aliases)}, {"attributes_json", entity.attributes_json},
        {"deleted", entity.deleted}, {"description", entity.description}, {"id", entity.id},
        {"kind", entity.kind}, {"name", entity.name}, {"review_status", entity.review_status},
        {"revision", entity.revision}, {"tags", strings(entity.tags)}, {"world_id", entity.world_id},
    };
}

JsonValue encodeBlueprint(const xuyan::domain::CharacterBlueprint& card, bool include_private) {
    return JsonValue::Object{
        {"abilities_json", card.abilities_json}, {"background", card.background},
        {"deleted", card.deleted}, {"equipment", strings(card.equipment)},
        {"extensions_json", card.extensions_json}, {"id", card.id},
        {"long_term_goal", card.long_term_goal}, {"name", card.name},
        {"private_notes", include_private ? card.private_notes : std::string{}},
        {"short_term_goal", card.short_term_goal}, {"speech_style", card.speech_style},
        {"summary", card.summary}, {"traits", strings(card.traits)},
        {"values", strings(card.values)}, {"version", card.version},
    };
}

Result<std::string> requiredString(const JsonValue& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->isString()) return Result<std::string>::failure(packageError("包字段缺失或类型错误：" + std::string(key)));
    return Result<std::string>::success(value->string());
}

Result<std::vector<std::string>> stringArray(const JsonValue& object, std::string_view key) {
    const auto* value = object.find(key);
    if (value == nullptr || !value->isArray()) return Result<std::vector<std::string>>::failure(packageError("包数组字段无效：" + std::string(key)));
    std::vector<std::string> result;
    for (const auto& item : value->array()) {
        if (!item.isString()) return Result<std::vector<std::string>>::failure(packageError("包数组包含非字符串"));
        result.push_back(item.string());
    }
    return Result<std::vector<std::string>>::success(std::move(result));
}

Result<WorldEntity> decodeEntity(const JsonValue& value) {
    if (!value.isObject()) return Result<WorldEntity>::failure(packageError("实体行必须是 JSON 对象"));
    WorldEntity entity;
    auto id = requiredString(value, "id"); auto world = requiredString(value, "world_id");
    auto kind = requiredString(value, "kind"); auto name = requiredString(value, "name");
    auto description = requiredString(value, "description"); auto attributes = requiredString(value, "attributes_json");
    auto review = requiredString(value, "review_status"); auto aliases = stringArray(value, "aliases");
    auto tags = stringArray(value, "tags");
    if (!id.ok() || !world.ok() || !kind.ok() || !name.ok() || !description.ok() || !attributes.ok()
        || !review.ok() || !aliases.ok() || !tags.ok()) {
        return Result<WorldEntity>::failure(packageError("实体缺少必需字段或字段类型错误"));
    }
    const auto* revision = value.find("revision");
    const auto* deleted = value.find("deleted");
    if (revision == nullptr || !revision->isInteger() || revision->integer() < 1
        || revision->integer() > std::numeric_limits<int>::max() || deleted == nullptr || !deleted->isBool()) {
        return Result<WorldEntity>::failure(packageError("实体修订或删除状态无效"));
    }
    auto extension_json = xuyan::package::parseJson(*attributes.value);
    if (!extension_json.ok() || !extension_json.value->isObject())
        return Result<WorldEntity>::failure(packageError("实体扩展属性不是合法 JSON 对象"));
    entity.id = std::move(*id.value); entity.world_id = std::move(*world.value);
    entity.kind = std::move(*kind.value); entity.name = std::move(*name.value);
    entity.description = std::move(*description.value); entity.attributes_json = std::move(*attributes.value);
    entity.review_status = std::move(*review.value); entity.aliases = std::move(*aliases.value);
    entity.tags = std::move(*tags.value); entity.revision = static_cast<int>(revision->integer());
    entity.deleted = deleted->boolean();
    return xuyan::domain::validateEntity(std::move(entity));
}

Result<xuyan::domain::CharacterBlueprint> decodeBlueprint(const JsonValue& value) {
    if (!value.isObject()) return Result<xuyan::domain::CharacterBlueprint>::failure(packageError("人物卡版本必须是 JSON 对象"));
    xuyan::domain::CharacterBlueprint card;
    auto id = requiredString(value, "id"); auto name = requiredString(value, "name");
    auto summary = requiredString(value, "summary"); auto long_goal = requiredString(value, "long_term_goal");
    auto short_goal = requiredString(value, "short_term_goal"); auto speech = requiredString(value, "speech_style");
    auto abilities = requiredString(value, "abilities_json"); auto background = requiredString(value, "background");
    auto private_notes = requiredString(value, "private_notes"); auto extensions = requiredString(value, "extensions_json");
    auto values = stringArray(value, "values"); auto traits = stringArray(value, "traits");
    auto equipment = stringArray(value, "equipment");
    const auto* version = value.find("version"); const auto* deleted = value.find("deleted");
    if (!id.ok() || !name.ok() || !summary.ok() || !long_goal.ok() || !short_goal.ok() || !speech.ok()
        || !abilities.ok() || !background.ok() || !private_notes.ok() || !extensions.ok()
        || !values.ok() || !traits.ok() || !equipment.ok() || version == nullptr || !version->isInteger()
        || version->integer() < 1 || version->integer() > std::numeric_limits<int>::max()
        || deleted == nullptr || !deleted->isBool()) {
        return Result<xuyan::domain::CharacterBlueprint>::failure(packageError("人物卡版本缺少字段或字段类型错误"));
    }
    auto abilities_value = xuyan::package::parseJson(*abilities.value);
    auto extensions_value = xuyan::package::parseJson(*extensions.value);
    if (!abilities_value.ok() || !abilities_value.value->isArray()
        || !extensions_value.ok() || !extensions_value.value->isObject()) {
        return Result<xuyan::domain::CharacterBlueprint>::failure(packageError("人物卡 JSON 扩展字段无效"));
    }
    card.id = std::move(*id.value); card.name = std::move(*name.value); card.summary = std::move(*summary.value);
    card.long_term_goal = std::move(*long_goal.value); card.short_term_goal = std::move(*short_goal.value);
    card.speech_style = std::move(*speech.value); card.abilities_json = std::move(*abilities.value);
    card.background = std::move(*background.value); card.private_notes = std::move(*private_notes.value);
    card.extensions_json = std::move(*extensions.value); card.values = std::move(*values.value);
    card.traits = std::move(*traits.value); card.equipment = std::move(*equipment.value);
    card.version = static_cast<int>(version->integer()); card.deleted = deleted->boolean();
    return xuyan::domain::validateBlueprint(std::move(card));
}

std::string entitiesJsonl(const std::vector<WorldEntity>& entities) {
    std::string output;
    for (const auto& entity : entities) { output += xuyan::package::writeJson(encodeEntity(entity)); output.push_back('\n'); }
    return output;
}

Result<std::vector<WorldEntity>> parseEntities(std::string_view jsonl) {
    std::vector<WorldEntity> result;
    std::size_t begin = 0;
    while (begin < jsonl.size()) {
        const auto newline = jsonl.find('\n', begin);
        const auto end = newline == std::string_view::npos ? jsonl.size() : newline;
        auto line = jsonl.substr(begin, end - begin);
        if (!line.empty()) {
            auto parsed = xuyan::package::parseJson(line);
            if (!parsed.ok()) return Result<std::vector<WorldEntity>>::failure(*parsed.error);
            auto entity = decodeEntity(*parsed.value);
            if (!entity.ok()) return Result<std::vector<WorldEntity>>::failure(*entity.error);
            result.push_back(std::move(*entity.value));
        }
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
    if (result.size() > 100000) return Result<std::vector<WorldEntity>>::failure(packageError("实体数量超过上限"));
    return Result<std::vector<WorldEntity>>::success(std::move(result));
}

Result<std::vector<xuyan::domain::CharacterBlueprint>> parseBlueprints(std::string_view jsonl) {
    std::vector<xuyan::domain::CharacterBlueprint> result;
    std::size_t begin = 0;
    while (begin < jsonl.size()) {
        const auto newline = jsonl.find('\n', begin);
        const auto end = newline == std::string_view::npos ? jsonl.size() : newline;
        const auto line = jsonl.substr(begin, end - begin);
        if (!line.empty()) {
            auto parsed = xuyan::package::parseJson(line);
            if (!parsed.ok()) return Result<std::vector<xuyan::domain::CharacterBlueprint>>::failure(*parsed.error);
            auto card = decodeBlueprint(*parsed.value);
            if (!card.ok()) return Result<std::vector<xuyan::domain::CharacterBlueprint>>::failure(*card.error);
            result.push_back(std::move(*card.value));
        }
        if (newline == std::string_view::npos) break;
        begin = newline + 1;
    }
    if (result.size() > 10000) return Result<std::vector<xuyan::domain::CharacterBlueprint>>::failure(packageError("人物卡版本数量超过上限"));
    return Result<std::vector<xuyan::domain::CharacterBlueprint>>::success(std::move(result));
}

} // namespace

PackageService::PackageService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

Result<PackageReport> PackageService::exportWorld(const std::filesystem::path& destination,
                                                  const std::string& title, const std::string& author) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    std::vector<WorldEntity> entities;
    for (int offset = 0;;) {
        auto page = repository.searchEntities({}, {}, offset, 200);
        if (!page.ok()) return Result<PackageReport>::failure(*page.error);
        entities.insert(entities.end(), page.value->items.begin(), page.value->items.end());
        if (!page.value->has_more) break;
        offset += static_cast<int>(page.value->items.size());
    }
    const auto entity_data = entitiesJsonl(entities);
    const auto world_data = xuyan::package::writeJson(JsonValue::Object{
        {"schema_version", "0.1.0"}, {"title", title}, {"world_id", "world-grey-harbor"},
    });
    const auto package_id = "package-" + xuyan::domain::sha256(world_data + entity_data).substr(0, 24);
    JsonValue::Array files;
    files.emplace_back(JsonValue::Object{{"bytes", static_cast<std::int64_t>(world_data.size())},
                                         {"path", "world.json"}, {"sha256", xuyan::domain::sha256(world_data)}});
    files.emplace_back(JsonValue::Object{{"bytes", static_cast<std::int64_t>(entity_data.size())},
                                         {"path", "entities.jsonl"}, {"sha256", xuyan::domain::sha256(entity_data)}});
    const auto manifest = xuyan::package::writeJson(JsonValue::Object{
        {"author", author}, {"base_world_version_id", "world-grey-harbor-v1"},
        {"contains_private_notes", false}, {"contains_source_text", false},
        {"content_version", "1.0.0"}, {"extensions", JsonValue::Object{}},
        {"files", std::move(files)}, {"format", "xuyanforge-package"}, {"format_version", "0.1.0"},
        {"kind", "world"}, {"optional_features", JsonValue::Array{}}, {"package_id", package_id},
        {"required_features", JsonValue::Array{JsonValue("world-v1")}}, {"title", title},
    });
    auto written = xuyan::package::writeZip(destination,
        {{"manifest.json", manifest}, {"world.json", world_data}, {"entities.jsonl", entity_data}});
    if (!written.ok()) return Result<PackageReport>::failure(*written.error);
    return Result<PackageReport>::success({package_id, "world", destination, static_cast<int>(entities.size())});
}

Result<PackageReport> PackageService::importWorld(const std::string& command_id,
                                                  const std::filesystem::path& source) {
    auto archive = xuyan::package::readZip(source);
    if (!archive.ok()) return Result<PackageReport>::failure(*archive.error);
    std::map<std::string, std::string, std::less<>> entries;
    for (auto& entry : *archive.value) entries.emplace(std::move(entry.path), std::move(entry.data));
    const auto manifest_entry = entries.find("manifest.json");
    if (manifest_entry == entries.end()) return Result<PackageReport>::failure(packageError("包缺少 manifest.json"));
    auto manifest = xuyan::package::parseJson(manifest_entry->second);
    if (!manifest.ok() || !manifest.value->isObject()) return Result<PackageReport>::failure(packageError("manifest.json 无效"));
    auto format = requiredString(*manifest.value, "format"); auto version = requiredString(*manifest.value, "format_version");
    auto kind = requiredString(*manifest.value, "kind"); auto package_id = requiredString(*manifest.value, "package_id");
    if (!format.ok() || !version.ok() || !kind.ok() || !package_id.ok() || *format.value != "xuyanforge-package"
        || *version.value != "0.1.0" || *kind.value != "world") {
        return Result<PackageReport>::failure(packageError("包格式、版本或类型不兼容"));
    }
    const auto* files = manifest.value->find("files");
    if (files == nullptr || !files->isArray()) return Result<PackageReport>::failure(packageError("manifest 缺少文件清单"));
    for (const auto& file : files->array()) {
        if (!file.isObject()) return Result<PackageReport>::failure(packageError("文件清单项无效"));
        auto path = requiredString(file, "path"); auto hash = requiredString(file, "sha256");
        const auto* bytes = file.find("bytes");
        if (!path.ok() || !hash.ok() || bytes == nullptr || !bytes->isInteger() || bytes->integer() < 0)
            return Result<PackageReport>::failure(packageError("文件清单字段无效"));
        const auto found = entries.find(*path.value);
        if (found == entries.end() || static_cast<std::int64_t>(found->second.size()) != bytes->integer()
            || xuyan::domain::sha256(found->second) != *hash.value) {
            return Result<PackageReport>::failure(packageError("包文件大小或 SHA-256 不匹配：" + *path.value));
        }
    }
    const auto entity_entry = entries.find("entities.jsonl");
    if (entity_entry == entries.end()) return Result<PackageReport>::failure(packageError("世界包缺少 entities.jsonl"));
    auto entities = parseEntities(entity_entry->second);
    if (!entities.ok()) return Result<PackageReport>::failure(*entities.error);
    const auto package_hash = xuyan::domain::sha256(manifest_entry->second + entity_entry->second);
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto imported = repository.importEntities(command_id, package_hash, std::move(*entities.value));
    if (!imported.ok()) return Result<PackageReport>::failure(*imported.error);
    return Result<PackageReport>::success({*package_id.value, "world", source, *imported.value});
}

Result<PackageReport> PackageService::exportCharacter(const std::string& blueprint_id,
                                                      const std::filesystem::path& destination,
                                                      const std::string& author,
                                                      bool include_private_notes) {
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto head = repository.loadBlueprint(blueprint_id);
    if (!head.ok()) return Result<PackageReport>::failure(*head.error);
    std::vector<xuyan::domain::CharacterBlueprint> versions;
    std::string data;
    for (int version = 1; version <= head.value->version; ++version) {
        auto card = repository.loadBlueprint(blueprint_id, version);
        if (!card.ok()) return Result<PackageReport>::failure(*card.error);
        versions.push_back(*card.value);
        data += xuyan::package::writeJson(encodeBlueprint(*card.value, include_private_notes));
        data.push_back('\n');
    }
    const auto package_id = "package-" + xuyan::domain::sha256(data).substr(0, 24);
    JsonValue::Array files;
    files.emplace_back(JsonValue::Object{{"bytes", static_cast<std::int64_t>(data.size())},
                                         {"path", "character-versions.jsonl"},
                                         {"sha256", xuyan::domain::sha256(data)}});
    const auto manifest = xuyan::package::writeJson(JsonValue::Object{
        {"author", author}, {"base_world_version_id", JsonValue(nullptr)},
        {"contains_private_notes", include_private_notes}, {"contains_source_text", false},
        {"content_version", std::to_string(head.value->version) + ".0.0"},
        {"extensions", JsonValue::Object{}}, {"files", std::move(files)},
        {"format", "xuyanforge-package"}, {"format_version", "0.1.0"}, {"kind", "character"},
        {"optional_features", JsonValue::Array{}}, {"package_id", package_id},
        {"required_features", JsonValue::Array{JsonValue("character-v1")}}, {"title", head.value->name},
    });
    auto written = xuyan::package::writeZip(destination,
        {{"manifest.json", manifest}, {"character-versions.jsonl", data}});
    if (!written.ok()) return Result<PackageReport>::failure(*written.error);
    return Result<PackageReport>::success({package_id, "character", destination, static_cast<int>(versions.size())});
}

Result<PackageReport> PackageService::importCharacter(const std::string& command_id,
                                                      const std::filesystem::path& source) {
    auto archive = xuyan::package::readZip(source);
    if (!archive.ok()) return Result<PackageReport>::failure(*archive.error);
    std::map<std::string, std::string, std::less<>> entries;
    for (auto& entry : *archive.value) entries.emplace(std::move(entry.path), std::move(entry.data));
    const auto manifest_entry = entries.find("manifest.json");
    if (manifest_entry == entries.end()) return Result<PackageReport>::failure(packageError("人物包缺少 manifest.json"));
    auto manifest = xuyan::package::parseJson(manifest_entry->second);
    if (!manifest.ok() || !manifest.value->isObject()) return Result<PackageReport>::failure(packageError("人物包 manifest 无效"));
    auto format = requiredString(*manifest.value, "format"); auto version = requiredString(*manifest.value, "format_version");
    auto kind = requiredString(*manifest.value, "kind"); auto package_id = requiredString(*manifest.value, "package_id");
    if (!format.ok() || !version.ok() || !kind.ok() || !package_id.ok() || *format.value != "xuyanforge-package"
        || *version.value != "0.1.0" || *kind.value != "character") {
        return Result<PackageReport>::failure(packageError("人物包格式、版本或类型不兼容"));
    }
    const auto* files = manifest.value->find("files");
    if (files == nullptr || !files->isArray()) return Result<PackageReport>::failure(packageError("人物包缺少文件清单"));
    for (const auto& file : files->array()) {
        auto path = requiredString(file, "path"); auto hash = requiredString(file, "sha256");
        const auto* bytes = file.find("bytes");
        if (!path.ok() || !hash.ok() || bytes == nullptr || !bytes->isInteger() || bytes->integer() < 0)
            return Result<PackageReport>::failure(packageError("人物包文件清单无效"));
        const auto found = entries.find(*path.value);
        if (found == entries.end() || static_cast<std::int64_t>(found->second.size()) != bytes->integer()
            || xuyan::domain::sha256(found->second) != *hash.value)
            return Result<PackageReport>::failure(packageError("人物包文件摘要不匹配"));
    }
    const auto versions_entry = entries.find("character-versions.jsonl");
    if (versions_entry == entries.end()) return Result<PackageReport>::failure(packageError("人物包缺少版本数据"));
    auto versions = parseBlueprints(versions_entry->second);
    if (!versions.ok()) return Result<PackageReport>::failure(*versions.error);
    const auto package_hash = xuyan::domain::sha256(manifest_entry->second + versions_entry->second);
    xuyan::storage::WorkspaceRepository repository(database_path_);
    auto imported = repository.importBlueprintVersions(command_id, package_hash, std::move(*versions.value));
    if (!imported.ok()) return Result<PackageReport>::failure(*imported.error);
    return Result<PackageReport>::success({*package_id.value, "character", source, *imported.value});
}

} // namespace xuyan::application
