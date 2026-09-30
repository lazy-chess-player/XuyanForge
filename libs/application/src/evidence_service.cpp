#include "xuyan/application/evidence_service.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/domain/hash.h"
#include "xuyan/storage/workspace_repository.h"

namespace xuyan::application {

EvidenceService::EvidenceService(std::filesystem::path database_path) : database_path_(std::move(database_path)) {}

xuyan::domain::Result<xuyan::domain::EvidenceReference> EvidenceService::create(
    const std::string& command_id, const std::string& entity_id, const std::string& field_path,
    const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint,
    const std::string& provenance_type) {
    try {
        xuyan::storage::WorkspaceRepository repository(database_path_);
        auto entity = repository.loadEntity(entity_id);
        if (!entity.ok() || entity.value->deleted) return xuyan::domain::Result<xuyan::domain::EvidenceReference>::failure(
            {xuyan::domain::ErrorCode::missing_context, "证据关联的世界条目不存在", false, "输入有效的稳定条目 ID"});
        auto source = repository.loadSource(source_id);
        if (!source.ok()) return xuyan::domain::Result<xuyan::domain::EvidenceReference>::failure(*source.error);
        SourceImportService sources(database_path_);
        /* 位置针对标准化原文而非导入文件字节；来源服务从章节锚点扫描并换算码点。
         * 即使命令可能重放，也先重新读原文并校验；缺失资产不能绕过这一步直接返回历史证据。
         */
        auto quote = sources.evidenceText(source_id, start_codepoint, end_codepoint);
        if (!quote.ok()) return xuyan::domain::Result<xuyan::domain::EvidenceReference>::failure(*quote.error);
        /* 本调用独占的待保存值；ID 由命令派生，引文及摘要必须来自刚读取的同一段原文，
         * 修订由仓储创建时设为 1，不能用调用方任意字符串代替原文。
         */
        xuyan::domain::EvidenceReference evidence;
        evidence.id = "evidence-" + xuyan::domain::sha256(command_id).substr(0, 20);
        evidence.entity_id = entity_id; evidence.field_path = field_path; evidence.source_id = source_id;
        evidence.start_codepoint = start_codepoint; evidence.end_codepoint = end_codepoint;
        evidence.quote = std::move(*quote.value); evidence.quote_hash = xuyan::domain::sha256(evidence.quote);
        evidence.provenance_type = provenance_type;
        auto valid = xuyan::domain::validateEvidence(std::move(evidence));
        if (!valid.ok()) return valid;
        return repository.createEvidence(command_id, std::move(*valid.value));
    } catch (const std::exception& exception) { return xuyan::domain::Result<xuyan::domain::EvidenceReference>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> EvidenceService::listForSource(const std::string& source_id) {
    try { return xuyan::storage::WorkspaceRepository(database_path_).listEvidenceForSource(source_id); }
    catch (const std::exception& exception) { return xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>>::failure(
        {xuyan::domain::ErrorCode::storage_error, exception.what(), true, "检查工作区后重试"}); }
}

} // namespace xuyan::application
