#pragma once

#include "xuyan/domain/source_document.h"
#include "xuyan/domain/evidence.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <filesystem>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

class SourceViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QVariantList sourceItems READ sourceItems NOTIFY changed)
    Q_PROPERTY(QVariantList chapterItems READ chapterItems NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(QString previewText READ previewText NOTIFY changed)
    Q_PROPERTY(QString selectedHash READ selectedHash NOTIFY changed)
    Q_PROPERTY(QVariantList evidenceItems READ evidenceItems NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
    Q_PROPERTY(int highlightStart READ highlightStart NOTIFY changed)
    Q_PROPERTY(int highlightEnd READ highlightEnd NOTIFY changed)
    Q_PROPERTY(int selectedChapterIndex READ selectedChapterIndex NOTIFY changed)
    Q_PROPERTY(QString selectedSourceId READ selectedSourceId NOTIFY changed)
    Q_PROPERTY(QString worldId READ worldId NOTIFY changed)
    Q_PROPERTY(bool previewLoading READ previewLoading NOTIFY changed)
    Q_PROPERTY(qlonglong previewStart READ previewStart NOTIFY changed)
    Q_PROPERTY(qlonglong previewEnd READ previewEnd NOTIFY changed)
    Q_PROPERTY(bool canPreviousWindow READ canPreviousWindow NOTIFY changed)
    Q_PROPERTY(bool canNextWindow READ canNextWindow NOTIFY changed)

public:
    /** @brief 保存数据库路径并等待当前世界确定后再读取来源。 */
    explicit SourceViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    /** @brief 返回来源操作是否正在执行。 */
    bool busy() const noexcept { return busy_; }
    /** @brief 返回最近一次来源操作的中文错误。 */
    QString errorText() const { return error_text_; }
    /** @brief 返回当前世界的小说列表。 */
    QVariantList sourceItems() const { return source_items_; }
    /** @brief 返回当前小说的章节目录。 */
    QVariantList chapterItems() const { return chapter_items_; }
    /** @brief 返回当前小说在当前世界列表中的下标。 */
    int selectedIndex() const noexcept { return selected_index_; }
    /** @brief 返回当前章节按需加载的原文片段。 */
    QString previewText() const { return preview_text_; }
    /** @brief 返回所选小说在来源记录中的内容哈希。 */
    QString selectedHash() const;
    /** @brief 返回当前小说的证据引用列表。 */
    QVariantList evidenceItems() const { return evidence_items_; }
    /** @brief 返回最近一次来源操作的状态说明。 */
    QString statusText() const { return status_text_; }
    /** @brief 返回已选证据在预览文本中的 UTF-16 起点。 */
    int highlightStart() const noexcept { return highlight_start_; }
    /** @brief 返回已选证据在预览文本中的 UTF-16 终点。 */
    int highlightEnd() const noexcept { return highlight_end_; }
    /** @brief 返回当前章节的零基下标。 */
    int selectedChapterIndex() const noexcept { return selected_chapter_index_; }
    /** @brief 返回当前小说的稳定来源标识。 */
    QString selectedSourceId() const { return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].id) : QString{}; }
    /** @brief 返回当前世界的稳定标识。 */
    QString worldId() const { return world_id_; }
    /** @brief 返回当前原文窗口是否仍在后台读取。 */
    bool previewLoading() const noexcept { return preview_loading_; }
    /** @brief 返回当前窗口在整部来源中的码点起点。 */
    qlonglong previewStart() const noexcept { return static_cast<qlonglong>(preview_start_codepoint_); }
    /** @brief 返回当前窗口在整部来源中的码点终点。 */
    qlonglong previewEnd() const noexcept { return static_cast<qlonglong>(preview_end_codepoint_); }
    /** @brief 返回当前章节是否还有上一段原文窗口。 */
    bool canPreviousWindow() const noexcept;
    /** @brief 返回当前章节是否还有下一段原文窗口。 */
    bool canNextWindow() const noexcept;

    /** @brief 切换当前世界，立即清除上一世界的来源与预览，并加载新世界资料。 */
    void setWorldId(QString world_id);
    /** @brief 重新读取工作区来源，并尽量保留当前或待选中的小说。 */
    Q_INVOKABLE void refresh();
    /** @brief 将选定的本地小说导入当前世界，不向远程发送正文。 */
    Q_INVOKABLE void importFile(const QUrl& file_url, QString world_id);
    /** @brief 按当前世界来源列表的下标选中小说。 */
    Q_INVOKABLE void selectSource(int index);
    /** @brief 按稳定标识选择小说；若列表尚未加载则在刷新后选中。 */
    Q_INVOKABLE void selectSourceId(QString source_id);
    /** @brief 将原文选区校验后关联到指定世界条目字段。 */
    Q_INVOKABLE void createEvidence(QString entity_id, QString field_path, int selection_start,
                                    int selection_end, QString provenance_type);
    /** @brief 定位证据所属章节和窗口，并在原文载入后高亮其 UTF-16 范围。 */
    Q_INVOKABLE void selectEvidence(int index);
    /** @brief 按下标选择章节并按需读取原文预览。 */
    Q_INVOKABLE void selectChapter(int index);
    /** @brief 读取当前章节的上一段原文窗口。 */
    Q_INVOKABLE void previousWindow();
    /** @brief 读取当前章节的下一段原文窗口。 */
    Q_INVOKABLE void nextWindow();
    /** @brief 校验并保存章节标题及 Unicode 码点区间。 */
    Q_INVOKABLE void saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint);

signals:
    void changed();
    void sourceImported();

private:
    /** @brief 将后台查询结果映射为来源列表并恢复选中项。 */
    void applyDocuments(xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result,
                        QString keep_id = {});
    /** @brief 选择来源，并在稳定章节仍存在时恢复阅读窗口而不短暂加载第一章。 */
    void selectSourceAt(int index, const std::string& chapter_id = {},
                        std::optional<std::size_t> preferred_window_start = std::nullopt);
    /** @brief 若世界切换发生在异步操作期间，则在操作结束后补一次来源刷新。 */
    void refreshAfterWorldChange();
    /** @brief 后台读取当前章节原文和对应的证据引用。 */
    void loadPreview(const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint);
    /** @brief 切换当前原文窗口并使旧请求失效。 */
    void showWindow(std::size_t start_codepoint, std::size_t end_codepoint);
    /** @brief 把绝对码点区间映射为已加载窗口中的 UTF-16 高亮范围。 */
    void highlightEvidenceRange(std::size_t start_codepoint, std::size_t end_codepoint);

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::SourceDocument> documents_;
    QVariantList source_items_;
    QString world_id_;
    std::uint64_t world_generation_{0};
    std::uint64_t preview_generation_{0};
    QString desired_source_id_;
    bool refresh_after_world_change_{false};
    QVariantList chapter_items_;
    std::vector<xuyan::domain::EvidenceReference> evidence_;
    QVariantList evidence_items_;
    int selected_index_{-1};
    bool busy_{false};
    QString error_text_;
    QString preview_text_;
    QString status_text_;
    int highlight_start_{0};
    int highlight_end_{0};
    int selected_chapter_index_{-1};
    std::size_t preview_start_codepoint_{0};
    std::size_t preview_end_codepoint_{0};
    bool preview_loading_{false};
    std::optional<std::pair<std::size_t, std::size_t>> pending_evidence_range_;
    static constexpr std::size_t preview_window_size_{50000};
};
