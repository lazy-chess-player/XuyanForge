#pragma once

#include "xuyan/domain/world_entity.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

#include <filesystem>
#include <functional>

class WorkspaceViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QVariantList entityItems READ entityItems NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QString selectedName READ selectedName NOTIFY changed)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY changed)
    Q_PROPERTY(QString selectedDescription READ selectedDescription NOTIFY changed)
    Q_PROPERTY(QString selectedAliases READ selectedAliases NOTIFY changed)
    Q_PROPERTY(QString selectedTags READ selectedTags NOTIFY changed)
    Q_PROPERTY(QString selectedAttributes READ selectedAttributes NOTIFY changed)
    Q_PROPERTY(int selectedRevision READ selectedRevision NOTIFY changed)
    Q_PROPERTY(bool hasPreviousPage READ hasPreviousPage NOTIFY changed)
    Q_PROPERTY(bool hasNextPage READ hasNextPage NOTIFY changed)
    Q_PROPERTY(QString pageText READ pageText NOTIFY changed)
    Q_PROPERTY(bool draftAvailable READ draftAvailable NOTIFY changed)
    Q_PROPERTY(QString statusText READ statusText NOTIFY changed)

public:
    explicit WorkspaceViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QVariantList entityItems() const { return items_; }
    int total() const noexcept { return total_; }
    int selectedIndex() const noexcept { return selected_index_; }
    QString selectedId() const;
    QString selectedName() const;
    QString selectedKind() const;
    QString selectedDescription() const;
    QString selectedAliases() const;
    QString selectedTags() const;
    QString selectedAttributes() const;
    int selectedRevision() const noexcept;
    bool hasPreviousPage() const noexcept { return offset_ > 0; }
    bool hasNextPage() const noexcept { return offset_ + static_cast<int>(entities_.size()) < total_; }
    QString pageText() const;
    bool draftAvailable() const noexcept { return !active_draft_.isEmpty(); }
    QString statusText() const { return status_text_; }

    Q_INVOKABLE void refresh(QString query = {}, QString kind = {});
    Q_INVOKABLE void selectEntity(int index);
    Q_INVOKABLE void createEntity(QString name, QString kind, QString description,
                                  QString aliases, QString tags, QString attributes);
    Q_INVOKABLE void saveSelected(QString name, QString kind, QString description,
                                  QString aliases, QString tags, QString attributes);
    Q_INVOKABLE void deleteSelected();
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void previousPage();
    Q_INVOKABLE void nextPage();
    Q_INVOKABLE void saveDraft(QString name, QString kind, QString description,
                               QString aliases, QString tags, QString attributes);
    Q_INVOKABLE void discardDraft();
    Q_INVOKABLE void mergeSelectedInto(QString target_id);
    Q_INVOKABLE void splitMerge(QString merge_id);

signals:
    void changed();

private:
    using PageResult = xuyan::domain::Result<xuyan::domain::EntityPage>;
    using EntityResult = xuyan::domain::Result<xuyan::domain::WorldEntity>;

    static QString commandId();
    static std::vector<std::string> parseList(QString value);
    static QVariantMap toMap(const xuyan::domain::WorldEntity& entity);
    static xuyan::domain::WorldEntity fromForm(QString name, QString kind, QString description,
                                                QString aliases, QString tags, QString attributes);
    void loadPage(QString query, QString kind, QString keep_id = {});
    void applyPage(PageResult result, const QString& keep_id);
    void runEntity(std::function<EntityResult(const std::filesystem::path&)> work, QString success_message);
    QString draftKey() const;
    void loadActiveDraft();
    void clearActiveDraft();

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::WorldEntity> entities_;
    QVariantList items_;
    bool busy_{false};
    QString error_text_;
    QString status_text_;
    QString last_query_;
    QString last_kind_;
    int total_{0};
    int offset_{0};
    int page_size_{25};
    int selected_index_{-1};
    QVariantMap active_draft_;
};
