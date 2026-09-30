#pragma once

#include "xuyan/domain/extraction_candidate.h"
#include "xuyan/domain/extraction_job.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace xuyan::application {

/* 候选提交与校对入口；拥有路径，调用线程读取不可变原文，候选/证据/投影的最终原子性由仓储保障。 */
class CandidateService {
public:
    /*
     * 功能：绑定候选工作区。参数：database_path 为按值保存的路径。
     * 返回：完成初始化。失败：分配异常传播，不提前打开数据库。
     * 副作用：无存储访问；线程：同步构造，不拥有线程或常驻连接。
     */
    explicit CandidateService(std::filesystem::path database_path);
    /*
     * 功能：读取当前候选值，供审核方取得修订和来源身份；不自动校验调用方选中的世界。
     * 参数：candidate_id 为候选稳定标识，仅在本次调用借用。
     * 返回：含来源标识、当前修订、字段及证据的候选值对象，调用方须核对来源所属世界后审核。
     * 失败：候选不存在或仓储读取错误经 Result 传播，打开异常转中文存储错误，不暴露原文/异常详情。
     * 副作用：只读候选，打开数据库可能初始化结构；线程：调用线程同步完成，不保存输入引用或连接。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> load(const std::string& candidate_id);
    /*
     * 功能：校验冻结协议、字段、完整片段哈希及逐字证据后提交步骤候选。
     * 参数：command_id 为幂等提交标识；job_id 为任务；step_ordinal 为从 1 起的步骤序号；
     * expected_attempt 为当前已领取尝试次数；output_json 为内部候选封装，最多 8 MiB，借用至返回。
     * 返回：完整任务快照，合法空候选仍可完成步骤。失败：协议/字段/原文/保留映射/尝试冲突或存储错误返回 Result。
     * 副作用：候选、证据、原始输出、淘汰统计及步骤在仓储同事务提交；不会自动采纳为事实。
     * 线程：调用线程同步校验及提交，不联网，不长期保存参数引用。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJob> ingestStepOutput(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    /*
     * 功能：与完整提交执行相同校验，仅返回轻量检查点。
     * 参数：command_id 为幂等标识；job_id 为任务；step_ordinal 为从 1 起的步骤；expected_attempt 为已领取尝试；
     * output_json 为最多 8 MiB 的内部封装，含合法候选及版本相应淘汰统计。
     * 返回：同提交事务的任务检查点，不复制历史输出。失败：协议、证据、尝试或存储错误返回 Result。
     * 副作用：原子提交候选/证据/统计/步骤，不采纳事实；线程：调用线程同步，无网络等待。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionJobState> ingestStepOutputState(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    /*
     * 功能：兼容查询工作区全部匹配候选。参数：review_status 为内部审核状态，默认 candidate 表示待校对。
     * 返回：候选列表，无匹配成功为空。失败：状态/数据库错误返回 Result。
     * 副作用：只读，不进行世界隔离或有界分页，新界面应使用 listPage；线程：同步，结果为拥有值。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::ExtractionCandidate>> list(
        const std::string& review_status = "candidate");
    /*
     * 功能：世界隔离的分页查询。参数：world_id 为非空世界；source_id 为空表示该世界全部来源，否则限定来源；
     * review_status 为内部审核状态；limit 为页容量，合法范围由仓储校验；offset 为从零起的非负条目偏移。
     * 返回：本页与匹配总数，空页成功。失败：范围/分页/状态或存储错误返回 Result。
     * 副作用：只读候选；线程：调用线程同步，结果不借用连接。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidatePage> listPage(
        const std::string& world_id, const std::string& source_id,
        const std::string& review_status, int limit, std::int64_t offset);
    /*
     * 功能：提供关系端点逐字名称/别名建议，不替作者选择。
     * 参数：world_id 为世界；mention 为原文端点标识；limit 默认 20，范围由仓储校验；offset 默认 0，非负条目偏移。
     * 返回：已确认条目的稳定标识及修订建议页，无匹配成功为空。
     * 失败：身份、文本或分页/存储错误返回 Result；副作用：只读，不绑定或合并；线程：同步。
     */
    xuyan::domain::Result<xuyan::domain::RelationEndpointMatchPage> matchRelationEndpoints(
        const std::string& world_id, const std::string& mention, int limit = 20, std::int64_t offset = 0);
    /*
     * 功能：按候选完整逐字身份建议已有同世界同类型实体。
     * 参数：candidate_id 为候选；expected_candidate_revision 为已读取候选修订；limit 默认 20；offset 默认 0，非负条目偏移。
     * 返回：可供明确选择的匹配页，无匹配成功为空。失败：候选性质、修订/分页或数据库错误返回 Result。
     * 副作用：只读候选及确认条目，不自动选择/合并；线程：同步，结果为自有值。
     */
    xuyan::domain::Result<xuyan::domain::CandidateEntityMatchPage> matchCandidateEntities(
        const std::string& candidate_id, int expected_candidate_revision,
        int limit = 20, std::int64_t offset = 0);
    /*
     * 功能：将作者明确确认的实体候选关联已有条目，累计证据及逐字别名。
     * 参数：command_id 为幂等命令；candidate_id 为候选；expected_candidate_revision 为候选修订；
     * selection 为目标稳定标识及预期修订；provenance_type 为明确来源性质内部值。
     * 返回：审核后候选。失败：不是可关联实体、世界/类型/身份/来源性质不符、两侧修订或存储冲突返回 Result。
     * 副作用：仓储原子写关联、证据、别名及审核，不覆盖目标作者字段、不按同名自动关联；线程：同步，无网络。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> acceptIntoEntity(
        const std::string& command_id, const std::string& candidate_id, int expected_candidate_revision,
        const xuyan::domain::CandidateEntitySelection& selection, const std::string& provenance_type);
    /*
     * 功能：按作者提交字段审核候选，接受时构造相应条目及图投影。
     * 参数：command_id 为幂等命令；candidate_id 为候选；expected_revision 为当前候选修订；
     * review_status 为审核内部值；name 为编辑标题；fields_json 为封闭字段对象；provenance_type 为明确来源性质；
     * endpoint_selection 默认无值，仅接受类型化关系时必须传入两个端点标识及当前修订，其他审核不得携带。
     * 返回：新修订候选。失败：字段、引文标识、端点选择、修订或存储错误返回 Result。
     * 副作用：候选、证据、接受映射及条目/图投影由仓储同事务提交；人物说法/模型推断仍保留为说法/假设。
     * 线程：调用线程同步，输入借用至返回，不调用模型，不虚构日期、强度或坐标。
     */
    xuyan::domain::Result<xuyan::domain::ExtractionCandidate> review(
        const std::string& command_id, const std::string& candidate_id, int expected_revision,
        const std::string& review_status, const std::string& name,
        const std::string& fields_json, const std::string& provenance_type,
        std::optional<xuyan::domain::RelationEndpointSelection> endpoint_selection = std::nullopt);
private:
    /*
     * 功能：共享两种返回形态的协议/字段/原文校验与原子提交。
     * 参数：command_id 为幂等命令；job_id 为任务；step_ordinal 为从 1 起步骤；expected_attempt 为领取尝试；
     * output_json 为内部封装，最多 8 MiB；模板 JobResult 为完整任务或轻量检查点。
     * 返回：对应事务结果。失败：全部字段、版本、尝试、原文哈希与主干连续映射必须成立，否则返回错误。
     * 副作用：校验完成后交仓储同事务写候选/证据/统计/步骤；线程：同步，不保存外部视图或跨网络持有事务。
     */
    template<class JobResult> xuyan::domain::Result<JobResult> ingestStepOutputImpl(
        const std::string& command_id, const std::string& job_id, int step_ordinal,
        int expected_attempt, const std::string& output_json);
    /* 候选工作区路径；构造后只读，与服务同寿命，每次调用使用局部仓储/来源服务。 */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
