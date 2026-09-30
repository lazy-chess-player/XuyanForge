#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <filesystem>
#include <string>
#include <vector>

namespace xuyan::application {

/*
 * 职责：创建、追加和读取工作区人物卡版本；实例记忆由实例服务独立维护。
 * 资源与生命周期：只拥有数据库路径值；每次业务调用在调用线程打开并销毁局部仓储，结果独立持有数据。
 * 线程：同步服务，不拥有线程或长期连接；调用方须保证调用期间服务存活，销毁不能与调用并发。
 * 存储边界：包括查询在内的业务调用可能创建数据库父目录/文件并迁移结构，不预置人物卡。
 */
class CharacterService {
public:
    /*
     * 功能：保存后续人物卡操作使用的数据库位置。
     * 参数：database_path：输入数据库文件路径，按值移入成员；相对路径在业务调用时按进程当前目录解析，空路径不在此校验。
     * 返回：完成路径初始化，不打开数据库。
     * 失败：路径值构造/分配异常直接传播。
     * 副作用：仅初始化本对象；调用方路径的引用不被保留。
     */
    explicit CharacterService(std::filesystem::path database_path);

    /*
     * 功能：委托 list 打开工作区并查询未删除人物卡的最新版本。
     * 参数：无。
     * 返回：按创建时间、卡片 ID 排序的自有值列表；无卡片为成功空列表。
     * 失败：仓储查询/版本读取失败返回 storage_error；仓储打开/迁移及未捕获的分配异常直接传播。
     * 副作用：只读卡片；打开数据库的目录创建和结构迁移边界见类说明。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> openAndList();
    /*
     * 功能：校验人物卡并创建首个版本；同一命令与相同规范化字段可重放。
     * 参数：
     *   command_id：输入幂等命令标识，调用期间借用；调用方应提供非空且未用于不同卡片操作的值。
     *   blueprint：输入完整卡片，按值转交；id 为空时分配新 ID，version/deleted 被设为 1/false。
     *     名称为 1—512 字节且不含制表符以外的低位控制字节（小于 0x20）；摘要最多 16 KiB，背景/私注各最多 256 KiB；
     *     values/traits 各最多 128 项、装备最多 256 项，列表删除空项并排序去重；能力/扩展仅检查数组/对象外形。
     * 返回：创建的版本 1，或命令日志固定的历史版本；包含最终卡片 ID（调用方指定或仓储生成）。
     * 失败：字段无效为 validation_failed，命令负载不同为 command_conflict，重复卡片 ID/存储失败为 storage_error；
     *   仓储打开/迁移及仓储捕获范围外的分配异常传播，当前不单独校验空 command_id。
     * 副作用：同事务写卡片头、版本正文及命令；只规范化传入副本，数据库打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> create(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint);
    /*
     * 功能：保留历史正文并追加人物卡版本，按预期版本防止覆盖并发编辑。
     * 参数：
     *   command_id：输入幂等标识，借用至返回；相同标识须复用相同编辑字段和 expected_version。
     *   blueprint：输入编辑后的完整卡片副本，id 必须对应已有卡片；字段限制及列表规范化同 create；
     *     version 由仓储重算，deleted 同时更新卡片头的删除标志。
     *   expected_version：输入已读取的最新正版本号；首次保存为 1，必须等于当前卡片头版本。
     * 返回：版本号加 1 的卡片，或该命令固定的历史版本。
     * 失败：字段无效、卡片缺失、头版本过期、命令负载冲突及存储失败分别返回 Result 错误；
     *   仓储打开/迁移及捕获范围外的分配异常传播。
     * 副作用：同事务追加版本、更新头/删除标志并记命令；已创建实例仍绑定旧版，打开可能创建/迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> save(
        const std::string& command_id, xuyan::domain::CharacterBlueprint blueprint, int expected_version);
    /*
     * 功能：按稳定标识读取卡片头或指定历史版本，允许读取带删除标志的版本。
     * 参数：
     *   blueprint_id：输入已有卡片 ID，调用期间借用；空值按查询处理，通常无匹配。
     *   version：输入版本号；默认 -1，实际所有负数均读取最新；正数读取指定版本，0 通常无匹配。
     * 返回：独立卡片值，含该版本 deleted 标志；不存在时不会成功返回空卡片。
     * 失败：卡片/版本缺失及读取错误均由仓储转为 storage_error；仓储打开/迁移异常直接传播。
     * 副作用：只读卡片正文；打开可能创建数据库或迁移结构。
     */
    xuyan::domain::Result<xuyan::domain::CharacterBlueprint> load(
        const std::string& blueprint_id, int version = -1);
    /*
     * 功能：查询当前工作区未删除卡片的最新版本，不分页。
     * 参数：无。
     * 返回：按创建时间、ID 排序的完整卡片列表；无记录为成功空列表，值对象不借用仓储。
     * 失败：查询或任一卡片版本读取失败为 storage_error；仓储打开/迁移异常直接传播。
     * 副作用：只读人物卡；打开可能创建数据库或迁移结构。
     */
    xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> list();

private:
    /* 人物卡数据库文件路径，无计量单位；初值为构造实参（无默认实参，空值仍保存）。
     * 构造时写入，各业务方法只读；拥有路径值直到服务销毁，不持有文件句柄或卡片缓存。
     */
    std::filesystem::path database_path_;
};

} // namespace xuyan::application
