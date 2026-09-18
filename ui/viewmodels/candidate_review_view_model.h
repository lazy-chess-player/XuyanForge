#pragma once

#include "xuyan/domain/extraction_candidate.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>

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
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)
public:
    explicit CandidateReviewViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    QVariantList candidates() const { return items_; }
    int selectedIndex() const noexcept { return selected_index_; }
    QString selectedId() const;
    QString selectedType() const;
    QString selectedName() const;
    QString selectedFields() const;
    QString selectedQuote() const;
    QString selectedProvenance() const;
    QString selectedSource() const;
    QString selectedRange() const;
    int selectedRevision() const noexcept;
    QString filter() const { return filter_; }
    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QString statusText() const { return status_text_; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void setFilter(QString status);
    Q_INVOKABLE void selectCandidate(int index);
    Q_INVOKABLE void reviewSelected(QString status, QString name, QString fields_json, QString provenance_type);

signals:
    void changed();
    void candidateAccepted();

private:
    const xuyan::domain::ExtractionCandidate* selected() const noexcept;
    static QVariantList maps(const std::vector<xuyan::domain::ExtractionCandidate>& candidates);
    void load(QString keep_id = {});

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::ExtractionCandidate> candidates_;
    QVariantList items_;
    int selected_index_{-1};
    QString filter_{QStringLiteral("candidate")};
    bool busy_{false};
    QString error_text_;
    QString status_text_{QStringLiteral("候选仅在人工接受后写入世界资料")};
};
