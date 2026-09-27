#pragma once

#include "xuyan/domain/extraction_candidate.h"

#include <QObject>
#include <QVariantList>

#include <cstdint>
#include <filesystem>
#include <vector>

class CandidateReviewViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QString selectedType READ selectedType NOTIFY changed)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
    Q_PROPERTY(QString selectedFields READ selectedFields NOTIFY changed)
    Q_PROPERTY(QString selectedQuote READ selectedQuote NOTIFY changed)
    Q_PROPERTY(QString selectedProvenance READ selectedProvenance NOTIFY changed)
    Q_PROPERTY(QString selectedSource READ selectedSource NOTIFY changed)
    Q_PROPERTY(QString selectedRange READ selectedRange NOTIFY changed)
    Q_PROPERTY(int selectedRevision READ selectedRevision NOTIFY changed)
    Q_PROPERTY(QString filter READ filter NOTIFY changed)
    Q_PROPERTY(QString worldId READ worldId NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(qlonglong pageIndex READ pageIndex NOTIFY changed)
    Q_PROPERTY(qlonglong pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(qlonglong totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(bool canPreviousPage READ canPreviousPage NOTIFY changed)
    Q_PROPERTY(bool canNextPage READ canNextPage NOTIFY changed)
public:
    /** @brief 创建候选校对视图模型；未选定世界前不读取候选。 */
    explicit CandidateReviewViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /** @brief 返回当前世界且符合审核筛选的候选摘要。 */
    QVariantList candidates() const { return items_; }
    /** @brief 返回当前选中的候选索引；无选择时为 -1。 */
    int selectedIndex() const noexcept { return selected_index_; }
    /** @brief 返回当前候选的稳定标识。 */
    QString selectedId() const;
    /** @brief 返回当前候选的资料类型。 */
    QString selectedType() const;
    /** @brief 返回当前候选的可编辑名称。 */
    QString selectedName() const;
    /** @brief 返回当前候选的结构化字段。 */
    QString selectedFields() const;
    /** @brief 返回当前候选的原文引文。 */
    QString selectedQuote() const;
    /** @brief 返回当前候选的来源性质。 */
    QString selectedProvenance() const;
    /** @brief 返回当前候选关联的小说来源标识。 */
    QString selectedSource() const;
    /** @brief 返回当前候选在来源中的码点范围。 */
    QString selectedRange() const;
    /** @brief 返回当前候选的审核修订号。 */
    int selectedRevision() const noexcept;
    /** @brief 返回当前审核状态筛选条件。 */
    QString filter() const { return filter_; }
    /** @brief 返回当前候选列表所属的世界标识。 */
    QString worldId() const { return world_id_; }
    /** @brief 返回是否正在读取候选或提交审核。 */
    bool busy() const noexcept { return busy_; }
    /** @brief 返回最近一次操作的错误说明。 */
    QString errorText() const { return error_text_; }
    /** @brief 返回最近一次操作的状态说明。 */
    QString statusText() const { return status_text_; }
    /** @brief 返回从零开始的当前候选页索引。 */
    qlonglong pageIndex() const noexcept { return page_index_; }
    /** @brief 返回匹配筛选条件的候选总页数。 */
    qlonglong pageCount() const noexcept { return total_count_ == 0 ? 0 : (total_count_ - 1) / page_size_ + 1; }
    /** @brief 返回当前世界符合筛选条件的候选总数。 */
    qlonglong totalCount() const noexcept { return total_count_; }
    /** @brief 返回是否可以读取上一页。 */
    bool canPreviousPage() const noexcept { return !busy_ && page_index_ > 0; }
    /** @brief 返回是否可以读取下一页。 */
    bool canNextPage() const noexcept { return !busy_ && page_index_ + 1 < pageCount(); }

    /** @brief 重新读取当前世界的候选并尽量保留选择。 */
    Q_INVOKABLE void refresh();
    /** @brief 切换世界并立即清空旧候选，随后异步读取新世界。 */
    Q_INVOKABLE void setWorldId(QString world_id);
    /** @brief 改变审核状态筛选条件并读取当前世界的队列。 */
    Q_INVOKABLE void setFilter(QString status);
    /** @brief 选择当前列表中的候选。 */
    Q_INVOKABLE void selectCandidate(int index);
    /** @brief 读取当前筛选条件下的上一页候选。 */
    Q_INVOKABLE void previousPage();
    /** @brief 读取当前筛选条件下的下一页候选。 */
    Q_INVOKABLE void nextPage();
    /** @brief 校验来源归属后提交当前候选的人工审核。 */
    Q_INVOKABLE void reviewSelected(QString status, QString name, QString fields_json, QString provenance_type);

signals:
    void changed();
    void candidateAccepted();

private:
    /** @brief 返回当前候选；索引无效时返回空指针。 */
    const xuyan::domain::ExtractionCandidate* selected() const noexcept;
    /** @brief 将领域候选转换为界面列表所需的字段。 */
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionCandidate>& candidates);
    /** @brief 后台按世界与状态读取一页候选，丢弃过期回调。 */
    void load(QString keep_id = {});

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::ExtractionCandidate> candidates_;
    QVariantList items_;
    int selected_index_{-1};
    QString filter_{QStringLiteral("candidate")};
    QString world_id_;
    std::uint64_t request_generation_{0};
    static constexpr int page_size_ = 100;
    qlonglong page_index_{0};
    qlonglong total_count_{0};
    bool busy_{false};
    bool reviewing_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("请选择世界以查看待校对候选")};
};
