#include "source_view_model.h"
#include "view_model_text.h"

#include "xuyan/application/source_import_service.h"
#include "xuyan/application/evidence_service.h"
#include "xuyan/application/world_catalog_service.h"

#include <QMetaObject>
#include <QPointer>
#include <QThreadPool>
#include <QUuid>

#include <exception>
#include <algorithm>
#include <iterator>

SourceViewModel::SourceViewModel(std::filesystem::path database_path, QObject* parent)
    : QObject(parent), database_path_(std::move(database_path)) {}

void SourceViewModel::setWorldId(QString world_id) {
    if (world_id_ == world_id) return;
    world_id_ = std::move(world_id);
    ++world_generation_;
    ++preview_generation_;
    desired_source_id_.clear();
    documents_.clear(); source_items_.clear(); chapter_items_.clear();
    evidence_.clear(); evidence_items_.clear();
    selected_index_ = -1; selected_chapter_index_ = -1;
    preview_text_.clear(); preview_loading_ = false;
    pending_evidence_range_.reset();
    preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
    highlight_start_ = 0; highlight_end_ = 0;
    status_text_.clear(); error_text_.clear();
    if (busy_) refresh_after_world_change_ = true;
    else if (!world_id_.isEmpty()) refresh();
    emit changed();
}

QString SourceViewModel::selectedHash() const {
    return selected_index_ >= 0 ? QString::fromStdString(documents_[selected_index_].sha256) : QString{};
}

bool SourceViewModel::canPreviousWindow() const noexcept {
    if (selected_index_ < 0 || selected_chapter_index_ < 0) return false;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    return static_cast<std::size_t>(selected_chapter_index_) < chapters.size()
        && preview_start_codepoint_ > chapters[static_cast<std::size_t>(selected_chapter_index_)].start_codepoint;
}

bool SourceViewModel::canNextWindow() const noexcept {
    if (selected_index_ < 0 || selected_chapter_index_ < 0) return false;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    return static_cast<std::size_t>(selected_chapter_index_) < chapters.size()
        && preview_end_codepoint_ < chapters[static_cast<std::size_t>(selected_chapter_index_)].end_codepoint;
}

void SourceViewModel::refresh() {
    if (world_id_.isEmpty()) {
        ++preview_generation_;
        documents_.clear(); source_items_.clear(); chapter_items_.clear();
        selected_index_ = -1; selected_chapter_index_ = -1;
        preview_text_.clear(); evidence_.clear(); evidence_items_.clear();
        preview_loading_ = false; pending_evidence_range_.reset();
        preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
        emit changed();
        return;
    }
    if (busy_) { refresh_after_world_change_ = true; return; }
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    const auto requested_world = world_id_;
    const auto world_generation = world_generation_;
    const auto keep_id = desired_source_id_.isEmpty() ? selectedSourceId() : desired_source_id_;
    QPointer<SourceViewModel> self(this);
    /*
     * 功能：在工作线程读取指定世界的来源元数据，不把整本正文带到界面线程。
     * 参数：闭包按值持有路径、待保留来源 ID、世界 ID 和代次；self 为非拥有式弱引用。
     * 返回：无；经 GUI 排队回调交付 Result。
     * 失败：服务错误或异常转为安全中文错误；对象销毁时跳过回调。
     * 副作用：只读工作区来源目录，不发网络请求。
     * 线程与生命周期：全局线程池运行，闭包不借用视图模型成员。
     */
    QThreadPool::globalInstance()->start([self, path, keep_id, requested_world, world_generation] {
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.listForWorld(requested_world.toStdString());
        } catch (...) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, "工作区操作发生内部错误，请检查资料后重试", true,
                                                "检查工作区后重试"});
        }
        if (!self) return;
        /*
         * 功能：仅将当前世界代次的来源目录应用到 GUI，并恢复合适的选中来源。
         * 参数：捕获弱引用、拥有结果、保留 ID、请求世界及代次。
         * 返回：无；代次或世界不匹配时丢弃旧结果并安排刷新。
         * 失败：Result 错误由 applyDocuments 写入界面，不作为空目录处理。
         * 副作用：可能替换目录、章节和预览状态，发 changed 或再发起异步读取。
         * 线程与生命周期：Qt 排队到 GUI 线程，执行前再次检查 QPointer。
         */
        QMetaObject::invokeMethod(self, [self, result = std::move(result), keep_id, requested_world, world_generation]() mutable {
            if (!self) return;
            if (self->world_id_ != requested_world || self->world_generation_ != world_generation) {
                // 切换世界后的旧查询不能覆盖当前列表，转而读取新世界。
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            const auto selected_now = self->selectedSourceId();
            self->applyDocuments(std::move(result), self->desired_source_id_.isEmpty()
                ? (selected_now.isEmpty() ? keep_id : selected_now) : self->desired_source_id_);
        }, Qt::QueuedConnection);
    });
}

/*
 * 功能：接收当前世界的来源查询结果，重建列表并恢复同一来源的章节与阅读窗口。
 * 参数：result 为拥有来源元数据的查询结果，失败时含错误；keep_id 为希望恢复的稳定来源 ID，空值回退首项。
 * 返回：无；无来源时保留空选择。
 * 失败：Result 错误转中文提示并保持旧目录，不伪造成功；值分配异常可能传播。
 * 副作用：更新 GUI 来源、章节、证据和选中状态；可能调用 showWindow 异步读取窗口并发 changed。
 * 线程与生命周期：只在 GUI 线程执行；传入值在调用期间由本函数消费。
 */
void SourceViewModel::applyDocuments(
    xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> result, QString keep_id) {
    busy_ = false;
    if (!result.ok()) {
        error_text_ = view_model_text::errorText(*result.error);
        emit changed();
        refreshAfterWorldChange();
        return;
    }
    const auto previous_source_id = selectedSourceId();
    std::string previous_chapter_id;
    std::optional<std::size_t> previous_window_start;
    if (selected_index_ >= 0 && selected_chapter_index_ >= 0) {
        const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
        if (static_cast<std::size_t>(selected_chapter_index_) < chapters.size()) {
            previous_chapter_id = chapters[static_cast<std::size_t>(selected_chapter_index_)].id;
            previous_window_start = preview_start_codepoint_;
        }
    }
    documents_ = std::move(*result.value);
    // 存储层可列出整个工作区，这里只把当前世界的来源交给界面。
    /*
     * 功能：把不属于当前世界的来源从界面快照中剔除，作为服务过滤之外的显示边界。
     * 参数：document 为擦除算法临时借用的来源元数据；闭包只借用当前 GUI 对象。
     * 返回：true 表示该来源需移出当前世界列表；false 表示保留。
     * 失败：字符串转换分配异常可能传播。
     * 副作用：谓词本身只读；外围 erase_if 修改 documents_。
     * 线程与生命周期：同步运行于 GUI 线程，闭包不逃逸本次调用。
     */
    std::erase_if(documents_, [this](const auto& document) {
        return world_id_.isEmpty() || QString::fromStdString(document.world_id) != world_id_;
    });
    desired_source_id_.clear();
    source_items_.clear();
    selected_index_ = -1;
    for (std::size_t index = 0; index < documents_.size(); ++index) {
        const auto& document = documents_[index];
        source_items_.push_back(QVariantMap{
            {QStringLiteral("name"), QString::fromStdString(document.name)},
            {QStringLiteral("id"), QString::fromStdString(document.id)},
            {QStringLiteral("chapters"), static_cast<int>(document.chapters.size())},
            {QStringLiteral("encoding"), QString::fromStdString(document.detected_encoding)},
        });
        if (QString::fromStdString(document.id) == keep_id) selected_index_ = static_cast<int>(index);
    }
    if (selected_index_ < 0 && !documents_.empty()) selected_index_ = 0;
    if (selected_index_ >= 0) {
        // 导入新来源不继承旧章节；刷新同一来源才恢复稳定章节 ID 与窗口。
        if (previous_source_id == keep_id)
            selectSourceAt(selected_index_, previous_chapter_id, previous_window_start);
        else selectSourceAt(selected_index_);
    }
    else emit changed();
    refreshAfterWorldChange();
}

/*
 * 功能：消费一次延迟刷新标记，重新查询操作期间切换到的当前世界。
 * 参数：无。
 * 返回：无；无挂起刷新时直接返回。
 * 失败：刷新失败通过界面错误状态报告。
 * 副作用：清除标记并可能投递一次来源目录读取。
 * 线程与生命周期：仅 GUI 线程调用；不持有后台连接。
 */
void SourceViewModel::refreshAfterWorldChange() {
    if (!refresh_after_world_change_) return;
    refresh_after_world_change_ = false;
    refresh();
}

void SourceViewModel::importFile(const QUrl& file_url, QString world_id) {
    if (busy_ || !file_url.isLocalFile()) return;
    if (world_id.isEmpty()) { error_text_ = tr("请先在首页创建世界"); emit changed(); return; }
    if (world_id != world_id_) { error_text_ = tr("当前世界已切换，请重新选择导入目标"); emit changed(); return; }
    busy_ = true;
    error_text_.clear();
    emit changed();
    const auto path = database_path_;
    const auto file = file_url.toLocalFile().toStdWString();
    const auto world_generation = world_generation_;
    const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    QPointer<SourceViewModel> self(this);
    /*
     * 功能：在工作线程导入本地文件、关联目标世界并重读该世界来源目录。
     * 参数：闭包按值持有数据库路径、文件路径、命令 ID、世界 ID 和代次；self 为非拥有式弱引用。
     * 返回：无；导入结果和目录经排队回调交付。
     * 失败：导入、关联或查询失败返回 Result；异常转为中文存储错误。
     * 副作用：成功时写入来源资产、来源元数据和世界关联；不调用模型。
     * 线程与生命周期：全局线程池执行，文件路径和值与 GUI 对象分离；销毁不回滚已完成导入。
     */
    QThreadPool::globalInstance()->start([self, path, file, command, world_id, world_generation] {
        xuyan::domain::Result<xuyan::domain::SourceDocument> imported;
        xuyan::domain::Result<std::vector<xuyan::domain::SourceDocument>> documents;
        try {
            xuyan::application::SourceImportService service(path);
            imported = service.importTextFile(command, std::filesystem::path(file), "1", world_id.toStdString());
            if (imported.ok()) {
                auto attached = xuyan::application::WorldCatalogService(path).attachSource(
                    world_id.toStdString(), imported.value->id);
                if (!attached.ok()) imported = decltype(imported)::failure(*attached.error);
                else documents = service.listForWorld(world_id.toStdString());
            }
        } catch (...) {
            imported = decltype(imported)::failure({xuyan::domain::ErrorCode::storage_error, "工作区操作发生内部错误，请检查资料后重试", true,
                                                    "检查文件与工作区后重试"});
        }
        if (!self) return;
        /*
         * 功能：仅在导入目标仍是当前世界时显示导入结果并选中新增来源。
         * 参数：捕获弱引用、拥有导入和目录结果、操作世界及代次。
         * 返回：无；对象消失或世界变化时放弃回填。
         * 失败：导入错误写入中文提示；目录错误交由 applyDocuments 处理。
         * 副作用：复位忙碌态，更新目录并在成功时发 sourceImported。
         * 线程与生命周期：Qt 排队到 GUI 线程；后台落盘不依赖回调仍然存活。
         */
        QMetaObject::invokeMethod(self, [self, imported = std::move(imported), documents = std::move(documents), world_id, world_generation]() mutable {
            if (!self) return;
            if (self->world_id_ != world_id || self->world_generation_ != world_generation) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            if (!imported.ok()) {
                self->busy_ = false;
                self->error_text_ = view_model_text::errorText(*imported.error);
                emit self->changed();
                self->refreshAfterWorldChange();
                return;
            }
            const auto keep = QString::fromStdString(imported.value->id);
            self->applyDocuments(std::move(documents), keep);
            emit self->sourceImported();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectSource(int index) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) return;
    // 用户明确选中来源后，旧的按 ID 延迟跳转意图不应在下次刷新时重新生效。
    desired_source_id_.clear();
    selectSourceAt(index);
}

/*
 * 功能：选中当前目录的一项来源，并尽量恢复稳定章节 ID 或原文阅读位置。
 * 参数：index 为零基来源下标；chapter_id 为可空稳定章节 ID；preferred_window_start 为可空绝对 Unicode 码点起点。
 * 返回：无；越界时忽略。
 * 失败：预览读库失败由异步回调提示；章节 ID 不再存在时回退位置或首章。
 * 副作用：清空旧章节与证据，更新选中项并可能调用 showWindow 读取新预览。
 * 线程与生命周期：只在 GUI 线程运行，传入章节 ID 只在本次调用期间借用。
 */
void SourceViewModel::selectSourceAt(int index, const std::string& chapter_id,
                                     std::optional<std::size_t> preferred_window_start) {
    if (index < 0 || index >= static_cast<int>(documents_.size())) return;
    ++preview_generation_;
    pending_evidence_range_.reset();
    selected_index_ = index;
    chapter_items_.clear();
    evidence_.clear(); evidence_items_.clear(); selected_chapter_index_ = -1;
    for (const auto& chapter : documents_[index].chapters) {
        chapter_items_.push_back(QVariantMap{
            {QStringLiteral("title"), QString::fromStdString(chapter.title)},
            {QStringLiteral("ordinal"), chapter.ordinal},
            {QStringLiteral("start"), static_cast<qlonglong>(chapter.start_codepoint)},
            {QStringLiteral("end"), static_cast<qlonglong>(chapter.end_codepoint)},
        });
    }
    const auto& chapters = documents_[static_cast<std::size_t>(index)].chapters;
    if (!chapters.empty()) {
        int chapter_index = 0;
        bool chapter_matched = false;
        if (!chapter_id.empty()) {
            /*
             * 功能：判断当前章节是否与保存的稳定章节 ID 相同。
             * 参数：chapter 为本次查找借用的章节；chapter_id 为仅在 selectSourceAt 调用期间有效的引用。
             * 返回：ID 相等为 true，否则为 false。
             * 失败：不执行校验，比较本身不访问外部资源。
             * 副作用：只读章节；谓词不会逃逸同步 find_if。
             */
            const auto found = std::find_if(chapters.begin(), chapters.end(), [&chapter_id](const auto& chapter) {
                return chapter.id == chapter_id;
            });
            if (found != chapters.end()) {
                chapter_index = static_cast<int>(std::distance(chapters.begin(), found));
                chapter_matched = true;
            }
        }
        if (!chapter_matched && preferred_window_start) {
            // 外部重划章节可能更换 ID，退化为按原文绝对坐标寻找仍覆盖阅读位置的章节。
            /*
             * 功能：章节 ID 改变后，查找仍包含原阅读位置的章节。
             * 参数：chapter 为本次查找借用的章节；preferred_window_start 按值持有绝对码点起点，此处已确认非空。
             * 返回：窗口起点位于章节半开范围内为 true；否则 false。
             * 失败：无数据库操作或额外异常。
             * 副作用：只读；谓词仅在同步 find_if 中有效。
             */
            const auto found = std::find_if(chapters.begin(), chapters.end(), [preferred_window_start](const auto& chapter) {
                return chapter.start_codepoint <= *preferred_window_start
                    && *preferred_window_start < chapter.end_codepoint;
            });
            if (found != chapters.end()) {
                chapter_index = static_cast<int>(std::distance(chapters.begin(), found));
                chapter_matched = true;
            }
        }
        if (!chapter_matched) preferred_window_start.reset();
        selected_chapter_index_ = chapter_index;
        const auto& chapter = chapters[static_cast<std::size_t>(chapter_index)];
        auto start = chapter.start_codepoint;
        if (preferred_window_start && *preferred_window_start >= chapter.start_codepoint) {
            // 边界校正后若旧窗口起点超出本章，退到仍可读的最后一个窗口。
            start = *preferred_window_start < chapter.end_codepoint ? *preferred_window_start
                : std::max(chapter.start_codepoint, chapter.end_codepoint -
                    std::min(preview_window_size_, chapter.end_codepoint - chapter.start_codepoint));
        }
        showWindow(start, std::min(chapter.end_codepoint, start + preview_window_size_));
    } else {
        preview_text_.clear(); preview_loading_ = false;
        preview_start_codepoint_ = 0; preview_end_codepoint_ = 0;
        highlight_start_ = 0; highlight_end_ = 0;
        emit changed();
    }
}

void SourceViewModel::selectSourceId(QString source_id) {
    if (source_id.isEmpty() || world_id_.isEmpty()) return;
    desired_source_id_ = source_id;
    for (int index = 0; index < static_cast<int>(documents_.size()); ++index) {
        if (QString::fromStdString(documents_[index].id) == source_id) {
            selectSource(index); return;
        }
    }
    if (!busy_) {
        status_text_ = tr("正在刷新来源，请稍后再试");
        refresh();
    }
}

/*
 * 功能：按来源与绝对码点半开区间读取原文窗口及证据，不将整书一次载入 QML。
 * 参数：source_id 为调用期间借用的稳定来源 ID；start_codepoint、end_codepoint 为从零起的 Unicode 码点边界。
 * 返回：无；异步回填 previewText 和证据列表。
 * 失败：服务或读取异常转中文错误；过期窗口结果直接丢弃。
 * 副作用：后台只读原文资产和证据；有效 GUI 回调更新预览、证据、高亮并发 changed。
 * 线程与生命周期：后台捕获路径、ID、边界及双代次值；对象销毁后不访问 GUI。
 */
void SourceViewModel::loadPreview(const std::string& source_id, std::size_t start_codepoint,
                                  std::size_t end_codepoint) {
    const auto path = database_path_;
    const auto world_generation = world_generation_;
    const auto preview_generation = preview_generation_;
    QPointer<SourceViewModel> self(this);
    /*
     * 功能：在工作线程读取窗口原文和同来源证据。
     * 参数：闭包按值持有路径、来源 ID、码点边界及世界/窗口代次；self 为弱引用。
     * 返回：无；把拥有的 Result 投递回 GUI。
     * 失败：任一服务异常转换为安全中文错误，不泄露原文或本机路径。
     * 副作用：只读来源资产与证据，不创建世界资料。
     * 线程与生命周期：全局线程池执行，所捕获值独立于视图模型生命周期。
     */
    QThreadPool::globalInstance()->start([self, path, source_id, start_codepoint, end_codepoint,
                                          world_generation, preview_generation] {
        xuyan::domain::Result<std::string> result;
        xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> evidence;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.evidenceText(source_id, start_codepoint, end_codepoint);
            xuyan::application::EvidenceService evidence_service(path);
            evidence = evidence_service.listForSource(source_id);
        } catch (...) {
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error, "工作区操作发生内部错误，请检查资料后重试", true,
                                                "检查来源与工作区后重试"});
        }
        if (!self) return;
        /*
         * 功能：确认世界、来源、窗口边界与代次仍匹配后更新预览并兑现待定位证据。
         * 参数：捕获弱引用、来源与码点边界、双代次及拥有的原文/证据结果。
         * 返回：无；任一身份不匹配时丢弃旧窗口结果。
         * 失败：原文读取失败显示中文错误并撤销待高亮；证据读取失败保持空证据列表。
         * 副作用：更新预览文字、加载标志、证据列表及高亮，发 changed。
         * 线程与生命周期：Qt 排队到 GUI 线程；不保存 Result 内部借用视图。
         */
        QMetaObject::invokeMethod(self, [self, source_id, start_codepoint, end_codepoint,
                                         world_generation, preview_generation,
                                         result = std::move(result), evidence = std::move(evidence)]() mutable {
            if (!self || self->world_generation_ != world_generation
                || self->preview_generation_ != preview_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id
                || self->preview_start_codepoint_ != start_codepoint
                || self->preview_end_codepoint_ != end_codepoint) return;
            self->preview_loading_ = false;
            if (!result.ok()) {
                self->error_text_ = view_model_text::errorText(*result.error);
                self->pending_evidence_range_.reset();
            }
            else self->preview_text_ = QString::fromStdString(*result.value);
            self->evidence_.clear(); self->evidence_items_.clear();
            if (evidence.ok()) {
                self->evidence_ = std::move(*evidence.value);
                for (const auto& item : self->evidence_) self->evidence_items_.push_back(QVariantMap{
                    {"entityId", QString::fromStdString(item.entity_id)}, {"field", QString::fromStdString(item.field_path)},
                    {"start", static_cast<qlonglong>(item.start_codepoint)}, {"end", static_cast<qlonglong>(item.end_codepoint)},
                    {"quote", QString::fromStdString(item.quote)}, {"provenance", QString::fromStdString(item.provenance_type)}});
            }
            if (result.ok() && self->pending_evidence_range_) {
                const auto [start, end] = *self->pending_evidence_range_;
                self->highlightEvidenceRange(start, end);
                self->pending_evidence_range_.reset();
            }
            emit self->changed();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::createEvidence(QString entity_id, QString field_path, int selection_start,
                                     int selection_end, QString provenance_type) {
    if (busy_ || preview_loading_ || selected_index_ < 0) return;
    if (selection_start < 0 || selection_end <= selection_start || selection_end > preview_text_.size()) {
        error_text_ = tr("请先在原文预览中选择一段非空文本"); emit changed(); return;
    }
    /* 选择下标是 Qt UTF-16 码元；落库证据必须换算为全文绝对 Unicode 码点，不能直接相加。 */
    const auto start_cp = preview_start_codepoint_ + static_cast<std::size_t>(preview_text_.left(selection_start).toUcs4().size());
    const auto end_cp = preview_start_codepoint_ + static_cast<std::size_t>(preview_text_.left(selection_end).toUcs4().size());
    const auto source_id = documents_[selected_index_].id;
    const auto world_generation = world_generation_;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = tr("正在校验证据区间…"); emit changed();
    QPointer<SourceViewModel> self(this);
    /*
     * 功能：在工作线程提交证据引用并重新读取该来源证据目录。
     * 参数：闭包按值持有路径、命令 ID、来源/实体/字段/性质及绝对码点区间和世界代次；self 为弱引用。
     * 返回：无；提交与目录结果经排队回调传递。
     * 失败：服务错误或异常形成 Result 失败，仍尝试保留可用的目录读取结果。
     * 副作用：成功时写证据引用及命令记录，不改原文，不发送模型请求。
     * 线程与生命周期：全局线程池执行，已落库写入不因界面销毁撤销。
     */
    QThreadPool::globalInstance()->start([self, path, command, source_id, entity_id, field_path,
                                          start_cp, end_cp, provenance_type, world_generation] {
        xuyan::domain::Result<xuyan::domain::EvidenceReference> created;
        xuyan::domain::Result<std::vector<xuyan::domain::EvidenceReference>> listed;
        try {
            xuyan::application::EvidenceService service(path);
            created = service.create(command, entity_id.toStdString(), field_path.toStdString(), source_id,
                                     start_cp, end_cp, provenance_type.toStdString());
            listed = service.listForSource(source_id);
        } catch (...) {
            created = decltype(created)::failure({xuyan::domain::ErrorCode::storage_error,
                "证据提交或刷新发生内部错误", true, "检查工作区后刷新证据"});
        }
        if (!self) return;
        /*
         * 功能：仅在原世界和原来源仍被选中时应用证据提交结果。
         * 参数：捕获弱引用、来源 ID、世界代次及拥有的提交/目录 Result。
         * 返回：无；过期结果不更新当前世界的证据列表。
         * 失败：提交失败显示中文错误；目录失败不冒充成功目录。
         * 副作用：复位忙碌态，更新证据列表与状态并发 changed，可能安排延迟刷新。
         * 线程与生命周期：Qt GUI 线程执行，对象消失则放弃通知而不回滚写入。
         */
        QMetaObject::invokeMethod(self, [self, source_id, world_generation,
                                         created = std::move(created), listed = std::move(listed)]() mutable {
            if (!self) return;
            if (self->world_generation_ != world_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            self->busy_ = false;
            if (!created.ok()) self->error_text_ = view_model_text::errorText(*created.error);
            else self->status_text_ = tr("证据已关联到 %1.%2").arg(
                QString::fromStdString(created.value->entity_id), QString::fromStdString(created.value->field_path));
            self->evidence_.clear(); self->evidence_items_.clear();
            if (listed.ok()) for (const auto& item : *listed.value) {
                self->evidence_.push_back(item);
                self->evidence_items_.push_back(QVariantMap{{"entityId", QString::fromStdString(item.entity_id)},
                    {"field", QString::fromStdString(item.field_path)}, {"start", static_cast<qlonglong>(item.start_codepoint)},
                    {"end", static_cast<qlonglong>(item.end_codepoint)}, {"quote", QString::fromStdString(item.quote)},
                    {"provenance", QString::fromStdString(item.provenance_type)}});
            }
            emit self->changed();
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}

void SourceViewModel::selectEvidence(int index) {
    if (index < 0 || index >= static_cast<int>(evidence_.size())) return;
    const auto& item = evidence_[static_cast<std::size_t>(index)];
    if (item.start_codepoint >= preview_start_codepoint_ && item.end_codepoint <= preview_end_codepoint_) {
        if (preview_loading_) pending_evidence_range_ = {{item.start_codepoint, item.end_codepoint}};
        else highlightEvidenceRange(item.start_codepoint, item.end_codepoint);
        emit changed();
        return;
    }
    if (selected_index_ < 0) return;
    const auto& chapters = documents_[static_cast<std::size_t>(selected_index_)].chapters;
    for (std::size_t chapter_index = 0; chapter_index < chapters.size(); ++chapter_index) {
        const auto& chapter = chapters[chapter_index];
        if (item.start_codepoint < chapter.start_codepoint || item.start_codepoint >= chapter.end_codepoint) continue;
        // 跨章证据仍归属起点章节；临时窗口可越过章节末尾以完整显示引文。
        const auto candidate_start = item.end_codepoint > preview_window_size_
            ? item.end_codepoint - preview_window_size_ : 0;
        const auto start = std::max(chapter.start_codepoint, candidate_start);
        if (start > item.start_codepoint) break;
        selected_chapter_index_ = static_cast<int>(chapter_index);
        pending_evidence_range_ = {{item.start_codepoint, item.end_codepoint}};
        showWindow(start, std::min(std::max(chapter.end_codepoint, item.end_codepoint),
                                   start + preview_window_size_));
        return;
    }
    status_text_ = tr("该证据位于其他章节，请先选择对应章节");
    emit changed();
}

void SourceViewModel::selectChapter(int index) {
    if (selected_index_ < 0 || index < 0 || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    selected_chapter_index_ = index;
    pending_evidence_range_.reset();
    const auto& chapter = documents_[selected_index_].chapters[static_cast<std::size_t>(index)];
    showWindow(chapter.start_codepoint, std::min(chapter.end_codepoint,
                                                chapter.start_codepoint + preview_window_size_));
}

void SourceViewModel::previousWindow() {
    if (!canPreviousWindow()) return;
    const auto& chapter = documents_[static_cast<std::size_t>(selected_index_)].chapters[
        static_cast<std::size_t>(selected_chapter_index_)];
    const auto end = preview_start_codepoint_;
    const auto start = end - chapter.start_codepoint > preview_window_size_
        ? end - preview_window_size_ : chapter.start_codepoint;
    pending_evidence_range_.reset();
    showWindow(start, end);
}

void SourceViewModel::nextWindow() {
    if (!canNextWindow()) return;
    const auto& chapter = documents_[static_cast<std::size_t>(selected_index_)].chapters[
        static_cast<std::size_t>(selected_chapter_index_)];
    const auto start = preview_end_codepoint_;
    pending_evidence_range_.reset();
    showWindow(start, std::min(chapter.end_codepoint, start + preview_window_size_));
}

/*
 * 功能：切换当前来源的原文预览窗口，并使上一个窗口的迟到回调失效。
 * 参数：start_codepoint、end_codepoint 为全文绝对 Unicode 码点半开边界；终点不得小于起点。
 * 返回：无；无选中来源或边界逆序时忽略。
 * 失败：窗口读取失败由 loadPreview 的异步回调报告。
 * 副作用：递增窗口代次、清高亮、设置加载态、发 changed 并投递只读窗口请求。
 * 线程与生命周期：只在 GUI 线程运行，后台按值捕获边界。
 */
void SourceViewModel::showWindow(std::size_t start_codepoint, std::size_t end_codepoint) {
    if (selected_index_ < 0 || end_codepoint < start_codepoint) return;
    // 每次切窗递增请求代次；旧窗口即使与新窗口边界相同也不能回填。
    ++preview_generation_;
    preview_start_codepoint_ = start_codepoint;
    preview_end_codepoint_ = end_codepoint;
    preview_text_ = tr("正在读取本章原文…");
    preview_loading_ = true;
    highlight_start_ = 0; highlight_end_ = 0;
    emit changed();
    loadPreview(documents_[selected_index_].id, preview_start_codepoint_, preview_end_codepoint_);
}

/*
 * 功能：把绝对原文码点范围换成当前 QString 预览中的 UTF-16 码元高亮范围。
 * 参数：start_codepoint、end_codepoint 为绝对 Unicode 码点半开范围，必须位于已加载窗口内。
 * 返回：无；窗口未就绪或范围越界时高亮保持为零长度。
 * 失败：不读取外部资源，非法范围静默保持无高亮。
 * 副作用：更新 GUI 高亮起止码元下标；不修改原文或证据。
 * 线程与生命周期：仅 GUI 线程同步执行。
 */
void SourceViewModel::highlightEvidenceRange(std::size_t start_codepoint, std::size_t end_codepoint) {
    highlight_start_ = 0; highlight_end_ = 0;
    if (preview_loading_ || start_codepoint < preview_start_codepoint_ || end_codepoint > preview_end_codepoint_
        || end_codepoint < start_codepoint) return;
    /*
     * 功能：从预览开头扫描指定数量的 Unicode 码点，计算对应 UTF-16 码元下标。
     * 参数：wanted 为相对当前窗口起点的非负码点偏移；闭包仅在当前函数内借用 this。
     * 返回：对应码元下标；超过预览长度时返回预览末尾。
     * 失败：不访问外部数据；非法代理对按单个码元前进。
     * 副作用：只读 preview_text_，不缓存转换表。
     * 线程与生命周期：GUI 线程同步执行，不逃逸本次调用。
     */
    const auto utf16ForCodepoint = [this](std::size_t wanted) {
        qsizetype offset = 0; std::size_t count = 0;
        while (offset < preview_text_.size() && count < wanted) {
            // Qt 字符串按 UTF-16 存储；补充平面字符跨两个码元但只占一个码点。
            if (preview_text_.at(offset).isHighSurrogate() && offset + 1 < preview_text_.size()
                && preview_text_.at(offset + 1).isLowSurrogate()) offset += 2;
            else ++offset;
            ++count;
        }
        return static_cast<int>(offset);
    };
    highlight_start_ = utf16ForCodepoint(start_codepoint - preview_start_codepoint_);
    highlight_end_ = utf16ForCodepoint(end_codepoint - preview_start_codepoint_);
}

void SourceViewModel::saveChapter(int index, QString title, qlonglong start_codepoint, qlonglong end_codepoint) {
    if (busy_ || selected_index_ < 0 || index < 0
        || index >= static_cast<int>(documents_[selected_index_].chapters.size())) return;
    auto chapters = documents_[selected_index_].chapters;
    auto& edited = chapters[static_cast<std::size_t>(index)];
    edited.title = title.trimmed().toStdString();
    edited.start_codepoint = start_codepoint < 0 ? 0 : static_cast<std::size_t>(start_codepoint);
    edited.end_codepoint = end_codepoint < 0 ? 0 : static_cast<std::size_t>(end_codepoint);
    // 表单只编辑一章；相邻章节共享的切片边界随之移动，避免留下未解析的原文空档。
    if (index > 0) chapters[static_cast<std::size_t>(index - 1)].end_codepoint = edited.start_codepoint;
    if (index + 1 < static_cast<int>(chapters.size()))
        chapters[static_cast<std::size_t>(index + 1)].start_codepoint = edited.end_codepoint;
    const auto source_id = documents_[selected_index_].id; const auto expected = documents_[selected_index_].chapter_revision;
    const auto world_generation = world_generation_;
    const auto path = database_path_; const auto command = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    busy_ = true; error_text_.clear(); status_text_ = tr("正在校验章节布局…"); emit changed();
    QPointer<SourceViewModel> self(this);
    /*
     * 功能：在工作线程按预期章节修订保存完整连续的章节布局。
     * 参数：闭包按值持有路径、命令 ID、来源 ID、预期修订、可移动章节值及世界代次；self 为弱引用。
     * 返回：无；保存结果经 GUI 排队回调交付。
     * 失败：修订冲突、范围不合法或服务异常转为 Result 失败。
     * 副作用：成功时写章节元数据及命令记录，原文资产保持不可变。
     * 线程与生命周期：全局线程池执行，不跨线程使用 GUI 对象或数据库连接。
     */
    QThreadPool::globalInstance()->start([self, path, command, source_id, expected,
                                          chapters = std::move(chapters), world_generation]() mutable {
        xuyan::domain::Result<xuyan::domain::SourceDocument> result;
        try {
            xuyan::application::SourceImportService service(path);
            result = service.saveChapters(command, source_id, expected, std::move(chapters));
        } catch (...) {
            // 双层防护：服务外的意外异常也必须让界面结束忙碌态并允许重试。
            result = decltype(result)::failure({xuyan::domain::ErrorCode::storage_error,
                                                "章节布局保存失败", true, "检查工作区文件与权限后重试"});
        }
        if (!self) return;
        /*
         * 功能：在原来源仍被选中时应用新章节修订并尽量恢复章节 ID 与阅读窗口。
         * 参数：捕获弱引用、来源 ID、世界代次和拥有的保存 Result。
         * 返回：无；已切世界或来源时不回填旧布局。
         * 失败：服务失败映射为中文错误，保留现有可见章节供刷新核对。
         * 副作用：复位忙碌态、替换来源元数据、重开窗口并可能投递延迟刷新。
         * 线程与生命周期：Qt 排队到 GUI 线程；后台提交完成后无法由回调取消。
         */
        QMetaObject::invokeMethod(self, [self, source_id, world_generation, result = std::move(result)]() mutable {
            if (!self) return;
            if (self->world_generation_ != world_generation || self->selected_index_ < 0
                || self->documents_[self->selected_index_].id != source_id) {
                self->busy_ = false;
                self->refreshAfterWorldChange();
                return;
            }
            self->busy_ = false;
            if (!result.ok()) {
                self->error_text_ = view_model_text::errorText(*result.error);
                emit self->changed();
                self->refreshAfterWorldChange();
                return;
            }
            std::string selected_chapter_id;
            std::optional<std::size_t> selected_window_start;
            if (self->selected_chapter_index_ >= 0) {
                const auto& old_chapters = self->documents_[static_cast<std::size_t>(self->selected_index_)].chapters;
                if (static_cast<std::size_t>(self->selected_chapter_index_) < old_chapters.size()) {
                    selected_chapter_id = old_chapters[static_cast<std::size_t>(self->selected_chapter_index_)].id;
                    selected_window_start = self->preview_start_codepoint_;
                }
            }
            self->documents_[self->selected_index_] = std::move(*result.value);
            self->status_text_ = tr("章节布局已保存为修订 %1").arg(self->documents_[self->selected_index_].chapter_revision);
            self->selectSourceAt(self->selected_index_, selected_chapter_id, selected_window_start);
            self->refreshAfterWorldChange();
        }, Qt::QueuedConnection);
    });
}
