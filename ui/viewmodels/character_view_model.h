#pragma once

#include "xuyan/domain/character_blueprint.h"

#include <QObject>
#include <QVariantList>

#include <filesystem>

class CharacterViewModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QVariantList cardItems READ cardItems NOTIFY changed)
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(int selectedVersion READ selectedVersion NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString values READ values NOTIFY changed)
    Q_PROPERTY(QString traits READ traits NOTIFY changed)
    Q_PROPERTY(QString longGoal READ longGoal NOTIFY changed)
    Q_PROPERTY(QString shortGoal READ shortGoal NOTIFY changed)
    Q_PROPERTY(QString speechStyle READ speechStyle NOTIFY changed)
    Q_PROPERTY(QString abilities READ abilities NOTIFY changed)
    Q_PROPERTY(QString equipment READ equipment NOTIFY changed)
    Q_PROPERTY(QString background READ background NOTIFY changed)
    Q_PROPERTY(QString privateNotes READ privateNotes NOTIFY changed)

public:
    explicit CharacterViewModel(std::filesystem::path database_path, QObject* parent = nullptr);

    bool busy() const noexcept { return busy_; }
    QString errorText() const { return error_text_; }
    QVariantList cardItems() const { return items_; }
    int selectedIndex() const noexcept { return selected_index_; }
    QString selectedId() const;
    int selectedVersion() const noexcept;
    QString name() const;
    QString summary() const;
    QString values() const;
    QString traits() const;
    QString longGoal() const;
    QString shortGoal() const;
    QString speechStyle() const;
    QString abilities() const;
    QString equipment() const;
    QString background() const;
    QString privateNotes() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectCard(int index);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void saveCard(QString name, QString summary, QString values, QString traits,
                              QString long_goal, QString short_goal, QString speech_style,
                              QString abilities, QString equipment, QString background,
                              QString private_notes);

signals:
    void changed();

private:
    static QString join(const std::vector<std::string>& values);
    static std::vector<std::string> split(QString value);
    void loadCards(QString keep_id = {});

    std::filesystem::path database_path_;
    std::vector<xuyan::domain::CharacterBlueprint> cards_;
    QVariantList items_;
    int selected_index_{-1};
    bool busy_{false};
    QString error_text_;
};

