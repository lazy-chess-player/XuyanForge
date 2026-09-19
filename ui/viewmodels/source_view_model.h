#pragma once

#include "xuyan/domain/source_document.h"
#include "xuyan/domain/evidence.h"

#include <QObject>
#include <QUrl>
#include <QVariantList>

#include <filesystem>

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

public:
    explicit SourceViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QVariantList sourceItems() const { return source_items_; }
    QVariantList chapterItems() const { return chapter_items_; }
    int selectedIndex() const noexcept { return selected_index_; }
    QString previewText() const { return preview_text_; }
    QString selectedHash() const;
    QVariantList evidenceItems() const { return evidence_items_; }
    QString statusText() const { return status_text_; }
    int highlightStart() const noexcept { return highlight_start_; }
    int highlightEnd() const noexcept { return highlight_end_; }
    int selectedChapterIndex() const noexcept { return selected_chapter_index_; }
    QString selectedSourceId() const { return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].id) : QString{}; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void importFile(const QUrl& file_url, QString world_id);
    Q_INVOKABLE void selectSource(int index);
    Q_INVOKABLE void selectSourceId(QString source_id);
    Q_INVOKABLE void createEvidence(QString entity_id, QString field_path, int selection_start,
                                    int selection_end, QString provenance_type);
    Q_INVOKABLE void selectEvidence(int index);
    Q_INVOKABLE void selectChapter(int index);
    Q_INVOKABLE void saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint);

signals:
    void changed();
    void sourceImported();

private:
    void applyDocuments(xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result,
                        QString keep_id = {});
    void loadPreview(const std::string& source_id, std::size_t start_codepoint, std::size_t end_codepoint);

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::SourceDocument> documents_;
    QVariantList source_items_;
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
};
