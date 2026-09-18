#include "character_view_model.h"

#include "xuyan/application/character_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>

CharacterViewModel::CharacterViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) { refresh(); }

QString CharacterViewModel::join(const std::vector<std::string>& values) {
    QStringList result;
    for (const auto& value : values) result << QString::fromStdString(value);
    return result.join(QStringLiteral("，"));
}

std::vector<std::string> CharacterViewModel::split(QString value) {
    value.replace(QChar(0xff0c), QChar(','));
    std::vector<std::string> result;
    for (const auto& part : value.split(',', Qt::SkipEmptyParts)) result.push_back(part.trimmed().toStdString());
    return result;
}

QString CharacterViewModel::selectedId() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].id) : QString{}; }
int CharacterViewModel::selectedVersion() const noexcept { return selected_index_ >= 0 ? cards_[selected_index_].version : 0; }
QString CharacterViewModel::name() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].name) : QString{}; }
QString CharacterViewModel::summary() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].summary) : QString{}; }
QString CharacterViewModel::values() const { return selected_index_ >= 0 ? join(cards_[selected_index_].values) : QString{}; }
QString CharacterViewModel::traits() const { return selected_index_ >= 0 ? join(cards_[selected_index_].traits) : QString{}; }
QString CharacterViewModel::longGoal() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].long_term_goal) : QString{}; }
QString CharacterViewModel::shortGoal() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].short_term_goal) : QString{}; }
QString CharacterViewModel::speechStyle() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].speech_style) : QString{}; }
QString CharacterViewModel::abilities() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].abilities_json) : QStringLiteral("[]"); }
QString CharacterViewModel::equipment() const { return selected_index_ >= 0 ? join(cards_[selected_index_].equipment) : QString{}; }
QString CharacterViewModel::background() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].background) : QString{}; }
QString CharacterViewModel::privateNotes() const { return selected_index_ >= 0 ? QString::fromStdString(cards_[selected_index_].private_notes) : QString{}; }

void CharacterViewModel::refresh() { loadCards(selectedId()); }

void CharacterViewModel::loadCards(QString keep_id) {
    if (busy_) return;
    busy_ = true; error_text_.clear(); emit changed();
    const auto path = database_path_;
    QPointer<CharacterViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, keep_id] {
        xuyan::domain::Result<std::vector<xuyan::domain::CharacterBlueprint>> result;
        try {
            xuyan::application::CharacterService service(path);
            result = service.openAndList();
        } catch (const std::exception& exception) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                "检查人物卡工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result), keep_id]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) {
                self->error_text_ = QString::fromStdString(result.error->message);
                emit self->changed(); return;
            }
            self->cards_ = std::move(*result.value);
            self->items_.clear(); self->selected_index_ = -1;
            for (std::size_t index = 0; index < self->cards_.size(); ++index) {
                const auto& card = self->cards_[index];
                self->items_.push_back(QVariantMap{{"name", QString::fromStdString(card.name)},
                                                   {"summary", QString::fromStdString(card.summary)},
                                                   {"version", card.version}, {"id", QString::fromStdString(card.id)}});
                if (QString::fromStdString(card.id) == keep_id) self->selected_index_ = static_cast<int>(index);
            }
            if (self->selected_index_ < 0 && !self->cards_.empty()) self->selected_index_ = 0;
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void CharacterViewModel::selectCard(int index) {
    if (index < 0 || index >= static_cast<int>(cards_.size())) return;
    selected_index_ = index; error_text_.clear(); emit changed();
}

void CharacterViewModel::clearSelection() { selected_index_ = -1; error_text_.clear(); emit changed(); }

void CharacterViewModel::saveCard(QString card_name, QString card_summary, QString card_values, QString card_traits,
                                  QString long_goal, QString short_goal, QString speech_style, QString card_abilities,
                                  QString card_equipment, QString card_background, QString private_notes) {
    if (busy_) return;
    xuyan::domain::CharacterBlueprint card;
    if (selected_index_ >= 0) card = cards_[selected_index_];
    card.name = card_name.trimmed().toStdString();
    card.summary = card_summary.toStdString();
    card.values = split(std::move(card_values));
    card.traits = split(std::move(card_traits));
    card.long_term_goal = long_goal.toStdString();
    card.short_term_goal = short_goal.toStdString();
    card.speech_style = speech_style.toStdString();
    card.abilities_json = card_abilities.trimmed().isEmpty() ? "[]" : card_abilities.trimmed().toStdString();
    card.equipment = split(std::move(card_equipment));
    card.background = card_background.toStdString();
    card.private_notes = private_notes.toStdString();
    const int expected = selected_index_ >= 0 ? cards_[selected_index_].version : 0;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); emit changed();
    const auto path = database_path_;
    QPointer<CharacterViewModel> self(this);
    QThreadPool::globalInstance()->start([self, path, card = std::move(card), expected, command]() mutable {
        xuyan::domain::Result<xuyan::domain::CharacterBlueprint> result;
        try {
            xuyan::application::CharacterService service(path);
            result = expected == 0 ? service.create(command, std::move(card))
                                   : service.save(command, std::move(card), expected);
        } catch (const std::exception& exception) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, exception.what(), true,
                                                "检查人物卡工作区后重试"});
        }
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, result = std::move(result)]() mutable {
            if (!self) return;
            self->busy_ = false;
            if (!result.ok()) { self->error_text_ = QString::fromStdString(result.error->message); emit self->changed(); return; }
            self->loadCards(QString::fromStdString(result.value->id));
        }, Qt::QueuedConnection);
    });
}

