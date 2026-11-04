#include "Archive.h"
#include "Pipeline.h"
#include "RelativeTable.h"
#include "Workspace.h"
#include "FutureEditor.h"
#include "FutureWork.h"
#include <QDateTime>
#include <QApplication>
#include <QCheckBox>
#include <QCalendarWidget>
#include <QDesktopServices>
#include <QDirIterator>
#include <QFileInfo>
#include <QGroupBox>
#include <QDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QScrollArea>
#include <QTableWidget>
#include <QTabWidget>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QRegularExpression>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QIcon>
#include <QUrl>
#include <QVBoxLayout>
#include <QFont>
#include <QUuid>
#include <QFileDialog>
#include <QInputDialog>
#include <stdexcept>
#include <algorithm>

class Window : public QMainWindow {
  public:
    Window(const QString &workspace) : root(workspace) {
        QDir().mkpath(root);
        QDir::setCurrent(root);
        QDir().mkpath(".youngkly");
        const auto defaults = QDir(QCoreApplication::applicationDirPath()).filePath("defaults");
        QDirIterator it(defaults, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            auto src = it.next();
            auto dst = QDir(root).filePath(QDir(defaults).relativeFilePath(src));
            if (!QFileInfo::exists(dst)) {
                QDir().mkpath(QFileInfo(dst).path());
                if (!QFile::copy(src, dst))
                    throw std::runtime_error("Cannot initialize workspace: " + dst.toStdString());
            }
        }
        config = youngkly::loadConfig("config/youngkly.json");
        setWindowTitle("Youngkly");
        resize(1200, 800);
        setMinimumSize(900, 620);
        auto *center = new QWidget;
        auto *layout = new QVBoxLayout(center);
        setCentralWidget(center);
        auto *selection = new QHBoxLayout;
        auto *dateBox = new QGroupBox("보고 날짜");
        auto *dateLayout = new QVBoxLayout(dateBox);
        auto *caption = new QLabel("날먹하고싶다");
        caption->setAlignment(Qt::AlignCenter);
        dateLayout->addWidget(caption);
        date = new QCalendarWidget;
        date->setMinimumWidth(300);
        date->setSelectedDate(QDate::currentDate());
        date->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
        date->setFirstDayOfWeek(Qt::Monday);
        dateLayout->addWidget(date);
        dateLayout->addWidget(new QLabel("저장된 보고서 프로젝트"));
        projectList = new QListWidget;
        projectList->setObjectName("savedReportProjects");
        projectList->setWordWrap(true);
        projectList->setMinimumHeight(120);
        projectList->setMaximumHeight(210);
        dateLayout->addWidget(projectList, 1);
        connect(projectList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
            if (!item->data(Qt::UserRole).toString().isEmpty()) loadProjectPath(item->data(Qt::UserRole).toString(), false);
        });
        selection->addWidget(dateBox);
        auto *box = new QGroupBox("보고서에 포함할 연구");
        auto *choices = new QVBoxLayout(box);
        auto *selectionButtons = new QHBoxLayout;
        selectAll = new QPushButton("모두 선택");
        clearAll = new QPushButton("모두 해제");
        selectionButtons->addWidget(selectAll);
        selectionButtons->addWidget(clearAll);
        selectionButtons->addStretch();
        choices->addLayout(selectionButtons);
        connect(selectAll, &QPushButton::clicked, this, [this] {
            for (auto *c : checks)
                c->setChecked(true);
        });
        connect(clearAll, &QPushButton::clicked, this, [this] {
            for (auto *c : checks)
                c->setChecked(false);
        });
        for (auto v : config["sources"].toArray()) {
            auto s = v.toObject();
            auto *c = new QCheckBox(youngkly::displayHeading(s["project_heading"].toString()));
            c->setChecked(true);
            checks << c;
            choices->addWidget(c);
        }
        dispersion = new QCheckBox("Dispersion 포함"); dispersion->setChecked(config["include_dispersion"].toBool(false));
        complexation = new QCheckBox("Complexation 포함"); complexation->setChecked(config["include_complexation"].toBool(false));
        choices->addWidget(dispersion); choices->addWidget(complexation);
        choices->addStretch();
        selection->addWidget(box, 1);
        layout->addLayout(selection);
        auto *buttons = new QHBoxLayout;
        inspect = new QPushButton("시트 날짜 확인");
        generate = new QPushButton("Word 보고서 작성");
        open = new QPushButton("결과 폴더 열기");
        preview = new QPushButton("SI 형식 표 미리보기");
        preview->setEnabled(false);
        buttons->addWidget(inspect);
        buttons->addWidget(generate);
        buttons->addWidget(open);
        openWord = new QPushButton("생성된 Word 열기"); openWord->setEnabled(false); buttons->addWidget(openWord);
        connect(openWord, &QPushButton::clicked, this, [this] {
            if (QFileInfo::exists(output)) QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(output).absoluteFilePath()));
        });
        buttons->addWidget(preview);
        plans = new QPushButton("Future work 편집"); buttons->addWidget(plans);
        cancel = new QPushButton("작업 취소"); cancel->setEnabled(false); buttons->addWidget(cancel);
        connect(plans, &QPushButton::clicked, this, [this] {
            try { editFutureWork(this, config, date->selectedDate()); }
            catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
        });
        connect(cancel, &QPushButton::clicked, this, [this] { cancelled = true; process.kill(); status->setText("작업을 취소했습니다."); });
        layout->addLayout(buttons);
        auto *storage = new QHBoxLayout;
        createPreview = new QPushButton("프리뷰 생성·저장");
        loadSaved = new QPushButton("저장 프로젝트 불러오기");
        exportSaved = new QPushButton("현재 프리뷰로 Word 작성"); exportSaved->setEnabled(false);
        storage->addWidget(createPreview); storage->addWidget(loadSaved); storage->addWidget(exportSaved);
        layout->addLayout(storage);
        connect(createPreview, &QPushButton::clicked, this, [this] { previewOnly = true; begin(true); });
        connect(loadSaved, &QPushButton::clicked, this, [this] { loadProject(); });
        connect(exportSaved, &QPushButton::clicked, this, [this] {
            try {
                runDirectory = QFileInfo(modelPath).absolutePath();
                QDir().mkpath(runDirectory);
                youngkly::writeFile(runDirectory + "/config.json", QJsonDocument(session).toJson());
                sessionConfigPath = runDirectory + "/config.json";
                stage = 1; previewOnly = false; cancelled = false; busy(true); advance();
            } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
        });
        status = new QLabel("보고 날짜와 연구 항목을 선택하세요.");
        status->setWordWrap(true);
        layout->addWidget(status);
        log = new QPlainTextEdit;
        log->setReadOnly(true);
        layout->addWidget(log, 1);
        process.setWorkingDirectory(root);
        process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process, &QProcess::readyReadStandardOutput, this, [this] {
            const auto progress = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
            log->appendPlainText(progress);
            if (!progress.isEmpty()) status->setText(progress.section('\n', -1));
        });
        connect(&process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart)
                fail("실행 파일을 시작하지 못했습니다: " + process.errorString());
        });
        connect(&process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus exit) {
                    if (cancelled) { status->setText("작업을 취소했습니다."); busy(false); return; }
                    if (code != 0 || exit != QProcess::NormalExit) {
                        fail("작업이 완료되지 않았습니다. 아래 실행 기록을 확인하세요.");
                        return;
                    }
                    try {
                        advance();
                    } catch (const std::exception &e) {
                        fail(QString::fromUtf8(e.what()));
                    }
                });
        connect(preview, &QPushButton::clicked, this, [this] { previewReport(); });
        connect(inspect, &QPushButton::clicked, this, [this] { begin(false); });
        connect(generate, &QPushButton::clicked, this, [this] { previewOnly = false; begin(true); });
        connect(open, &QPushButton::clicked, this, [this] {
            QDir().mkpath("outputs");
            QDesktopServices::openUrl(QUrl::fromLocalFile(runDirectory.isEmpty() ? QDir(root).filePath("프로젝트 모음") : QFileInfo(runDirectory).absoluteFilePath()));
        });
        refreshProjects();
    }
    void refreshProjects() {
        const auto selected = projectList->currentItem() ? projectList->currentItem()->data(Qt::UserRole).toString() : QString();
        projectList->clear();
        QDirIterator files("프로젝트 모음", {"*.youngkly_project.json"}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            if (QDir::fromNativeSeparators(path).contains("/자료/")) continue;
            try {
                const auto model = youngkly::loadConfig(path);
                const auto label = model["project_name"].toString(QFileInfo(path).completeBaseName().replace(".youngkly_project", ""));
                auto *item = new QListWidgetItem(label);
                item->setData(Qt::UserRole, path); item->setToolTip(path);
                // Insert by report date, newest first.
                int index = 0;
                while (index < projectList->count() && projectList->item(index)->data(Qt::UserRole + 1).toString() > model["report_date"].toString()) ++index;
                item->setData(Qt::UserRole + 1, model["report_date"].toString());
                projectList->insertItem(index, item);
                if (path == selected || QFileInfo(path).absoluteFilePath() == QFileInfo(modelPath).absoluteFilePath()) projectList->setCurrentItem(item);
            } catch (const std::exception &) { /* Ignore incomplete saves in the startup list. */ }
        }
        if (!projectList->count()) {
            auto *empty = new QListWidgetItem("저장된 보고서가 없습니다.", projectList);
            empty->setFlags(Qt::NoItemFlags);
        }
    }
    void loadProject() {
        const auto *item = projectList->currentItem();
        if (!item || item->data(Qt::UserRole).toString().isEmpty()) {
            projectList->setFocus(); status->setText("달력 아래에서 보고서 프로젝트를 선택하세요."); return;
        }
        loadProjectPath(item->data(Qt::UserRole).toString(), true);
    }
    void loadProjectPath(const QString &path, bool showPreview) {
        try {
            const auto model = youngkly::loadConfig(path);
            const auto saved = youngkly::loadConfig(model["snapshot_config"].toString());
            modelPath = path; building = true; session = saved; sessionConfigPath = model["snapshot_config"].toString();
            output = model["word_output"].toString(); runDirectory = QFileInfo(path).absolutePath();
            date->setSelectedDate(QDate::fromString(model["report_date"].toString(), Qt::ISODate));
            const auto sources = config["sources"].toArray();
            for (int i = 0; i < checks.size(); ++i) {
                bool included = false;
                for (const auto &source : saved["sources"].toArray()) included |= source.toObject()["id"] == sources[i].toObject()["id"];
                checks[i]->setChecked(included);
            }
            dispersion->setChecked(saved["include_dispersion"].toBool(false));
            complexation->setChecked(saved["include_complexation"].toBool(false));
            busy(false); status->setText("보고서를 불러왔습니다. 프리뷰 확인·Word 열기·갱신을 사용할 수 있습니다.");
            if (showPreview) previewReport();
        } catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
    }
    void saveProjects() {
        auto model = youngkly::loadConfig(modelPath);
        model["word_output"] = output;
        const auto paths = youngkly::saveProjectSnapshots(model, session, "프로젝트 모음");
        if (!paths.isEmpty()) {
            modelPath = paths.first(); runDirectory = QFileInfo(modelPath).absolutePath();
            sessionConfigPath = youngkly::loadConfig(modelPath)["snapshot_config"].toString();
        }
        refreshProjects();
        for (const auto &path : paths) log->appendPlainText("프로젝트 저장: " + path);
    }
    bool configureDirections(const QJsonObject &model) {
        QDialog dialog(this);
        dialog.setWindowTitle("반응 방향과 에너지 기준 구조 확인");
        dialog.resize(900, 580);
        auto *layout = new QVBoxLayout(&dialog);
        auto *explanation = new QLabel("IRC forward/reverse는 반응물·생성물을 자동 확정하지 않습니다. 각 경로의 기준 방향을 지정하세요. 미확정 경로는 활성화 장벽 해석을 보류합니다.", &dialog);
        explanation->setWordWrap(true);
        layout->addWidget(explanation);
        auto *scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        auto *page = new QWidget(scroll);
        auto *column = new QVBoxLayout(page);
        struct DirectionSelection { QComboBox *combo; QString key; int first; int last; };
        QVector<DirectionSelection> selections;
        const auto rules = session["reaction_rules"].toObject();
        for (const auto &sectionValue : youngkly::reportSections(model, session)) {
            const auto section = sectionValue.toObject();
            QMap<int, QString> names;
            for (const auto &rowValue : section["rows"].toArray()) {
                const auto row = rowValue.toObject();
                names[row["sheet_row"].toInt()] = row["filename"].toString();
            }
            for (const auto &groupValue : section["triplets"].toArray()) {
                const auto group = groupValue.toObject();
                const auto ids = group["rows"].toArray();
                if (ids.size() != 3) continue;
                const int first = ids.at(0).toInt(), last = ids.at(2).toInt();
                const QString key = group["rule_key"].toString(group["section"].toString());
                auto *label = new QLabel(youngkly::displayHeading(section["project_heading"].toString()) +
                    " / " + group["section"].toString() + "\n" + names[first] + "  |  TS  |  " + names[last], page);
                label->setWordWrap(true);
                column->addWidget(label);
                auto *combo = new QComboBox(page);
                combo->addItem("방향 미확정 — 반응 장벽 문장 보류", 0);
                combo->addItem("첫 번째 구조 → 세 번째 구조", 1);
                combo->addItem("세 번째 구조 → 첫 번째 구조", 2);
                const auto existing = rules.contains(key) ? rules[key].toObject() :
                                      rules[group["section"].toString()].toObject();
                const int reactant = existing["reactant_row_id"].toInt();
                const int product = existing["product_row_id"].toInt();
                combo->setCurrentIndex(reactant == first && product == last ? 1 :
                                       reactant == last && product == first ? 2 : 0);
                column->addWidget(combo);
                selections << DirectionSelection{combo, key, first, last};
            }
        }
        if (selections.isEmpty()) return false;
        column->addStretch();
        scroll->setWidget(page);
        layout->addWidget(scroll);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
        buttons->button(QDialogButtonBox::Save)->setText("반응 기준점 저장");
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return false;
        auto updated = rules;
        for (const auto &item : selections) {
            auto rule = updated[item.key].toObject();
            const int choice = item.combo->currentIndex();
            if (choice == 0) {
                rule.remove("reactant_row_id");
                rule.remove("product_row_id");
                rule.remove("reaction_direction_confirmed");
            } else {
                rule["reactant_row_id"] = choice == 1 ? item.first : item.last;
                rule["product_row_id"] = choice == 1 ? item.last : item.first;
                rule["reaction_direction_confirmed"] = true;
            }
            updated[item.key] = rule;
        }
        session["reaction_rules"] = updated;
        return true;
    }
    void previewReport() {
        if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) return;
        try {
            const auto model = youngkly::loadConfig(modelPath);
            QDialog dialog(this);
            dialog.setWindowTitle("Youngkly — SI Generator 형식 보고서 미리보기");
            dialog.resize(1140, 760);
            auto *layout = new QVBoxLayout(&dialog);
            auto *tabs = new QTabWidget(&dialog);
            const auto sections = youngkly::reportSections(model, session);
            for (const auto &sectionValue : sections) {
                const auto section = sectionValue.toObject();
                if (section["analyses"].toArray().isEmpty()) continue;
                auto *area = new QScrollArea(tabs);
                area->setWidgetResizable(true);
                auto *contents = new QWidget(area);
                auto *column = new QVBoxLayout(contents);
                auto *title = new QLabel(youngkly::displayHeading(section["project_heading"].toString()), contents);
                QFont heading = title->font(); heading.setBold(true); heading.setPointSize(12); title->setFont(heading);
                column->addWidget(title);
                for (const auto &groupValue : section["analyses"].toArray()) {
                    const auto group = groupValue.toObject();
                    column->addWidget(new QLabel("반응 단계: " + group["section"].toString(), contents));
                    const auto captures = group["captures"].toArray();
                    if (!captures.isEmpty()) {
                        auto *figures = new QHBoxLayout;
                        for (int i = 0; i < captures.size(); ++i) {
                            auto *panel = new QVBoxLayout;
                            auto *image = new QLabel(contents);
                            image->setPixmap(QPixmap(captures[i].toObject()["png"].toString()).scaled(280, 220, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                            image->setAlignment(Qt::AlignCenter); panel->addWidget(image);
                            const auto labels = group["structure_labels"].toArray();
                            auto *label = new QLabel(i < labels.size() ? labels[i].toString() : QString(), contents);
                            label->setAlignment(Qt::AlignCenter);
                            QFont font("Times New Roman", 10); font.setItalic(label->text().startsWith("S = "));
                            label->setFont(font); panel->addWidget(label); figures->addLayout(panel);
                        }
                        column->addLayout(figures);
                    }
                    QStringList issues;
                    for (const auto &issue : group["issues"].toArray()) issues << issue.toString();
                    if (!issues.isEmpty()) {
                        auto *warning = new QLabel("검증 필요: " + issues.join(" | "), contents);
                        warning->setWordWrap(true);
                        column->addWidget(warning);
                    }
                    const auto rmsd = group["group_view"].toObject()["alignment_anchor_rmsd_angstrom"].toArray();
                    if (!rmsd.isEmpty()) {
                        QStringList values;
                        for (const auto &value : rmsd) values << QString::number(value.toDouble(), 'f', 3);
                        column->addWidget(new QLabel("Anchor alignment RMSD (Å): " + values.join(" / "), contents));
                    }
                    for (const auto &entry : {qMakePair("Relative energy", "energy_table"),
                                              qMakePair("Mulliken spin density", "spin_table"),
                                              qMakePair("Selected geometry", "geometry_table")}) {
                        auto t = group[entry.second].toObject();
                        if (entry.second != QStringLiteral("energy_table"))
                            t = youngkly::siDocumentOrientation(t);
                        const auto headers = t["headers"].toArray(), rows = t["rows"].toArray();
                        if (headers.isEmpty()) continue;
                        auto *label = new QLabel(entry.first, contents);
                        QFont font = label->font(); font.setBold(true); label->setFont(font);
                        column->addWidget(label);
                        auto *table = new QTableWidget(rows.size(), headers.size(), contents);
                        QStringList headerLabels;
                        for (const auto &h : headers) headerLabels << h.toString();
                        table->setFont(QFont("Times New Roman", 10));
                        table->horizontalHeader()->setFont(QFont("Times New Roman", 10, QFont::Bold));
                        table->setHorizontalHeaderLabels(headerLabels);
                        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
                        table->setSelectionBehavior(QAbstractItemView::SelectRows);
                        for (int i = 0; i < rows.size(); ++i) {
                            const auto values = rows[i].toArray();
                            for (int j = 0; j < headers.size(); ++j) {
                                const auto value = j < values.size() ? values[j] : QJsonValue();
                                const QString shown = value.isDouble() ?
                                    QString::number(value.toDouble(), 'f', entry.second == QStringLiteral("energy_table") ? 2 : 3) :
                                    value.isNull() || value.isUndefined() ? "N/A" : value.toString();
                                auto *cell = new QTableWidgetItem(shown);
                                cell->setTextAlignment(Qt::AlignCenter);
                                if (shown.startsWith("S = ")) { auto font = table->font(); font.setItalic(true); cell->setFont(font); }
                                table->setItem(i, j, cell);
                            }
                        }
                        table->verticalHeader()->setDefaultSectionSize(25);
                        table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
                        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
                        table->setMinimumHeight(qMin(420, 70 + 32 * (rows.size() + 1)));
                        column->addWidget(table);
                    }
                }
                column->addStretch();
                area->setWidget(contents);
                tabs->addTab(area, section["sheet"].toString());
            }
            if (tabs->count() == 0) {
                auto *empty = new QLabel("해당 날짜의 분석된 구조가 없습니다.", tabs);
                tabs->addTab(empty, "미리보기");
            }
            layout->addWidget(tabs);
            auto *close = new QPushButton("닫기", &dialog);
            connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
            layout->addWidget(close);
            dialog.exec();
        } catch (const std::exception &e) {
            fail(QString::fromUtf8(e.what()));
        }
    }
    void begin(bool report) {
        try {
            const auto previousRules = session["reaction_rules"];
            session = config;
            if (!modelPath.isEmpty() && QFileInfo::exists(modelPath) &&
                youngkly::loadConfig(modelPath)["report_date"].toString() == date->selectedDate().toString(Qt::ISODate) && previousRules.isObject())
                session["reaction_rules"] = previousRules;
            cancelled = false; directionsPrompted = false; output.clear();
            runDirectory = ".youngkly/work/" + QUuid::createUuid().toString(QUuid::Id128);
            QDir().mkpath(runDirectory); modelPath = runDirectory + "/report-model.json";
            QJsonArray chosen;
            auto sources = config["sources"].toArray();
            for (int i = 0; i < checks.size(); ++i)
                if (checks[i]->isChecked())
                    chosen << sources[i];
            if (chosen.isEmpty()) {
                status->setText("연구 항목을 하나 이상 선택하세요.");
                return;
            }
            session["sources"] = chosen;
            session["include_dispersion"] = dispersion->isChecked();
            session["include_complexation"] = complexation->isChecked();
            sessionConfigPath = runDirectory + "/config.json";
            QDir().mkpath(".youngkly");
            youngkly::writeFile(sessionConfigPath, QJsonDocument(session).toJson());
            building = report;
            stage = 0;
            log->clear();
            busy(true);
            status->setText(report ? "시트와 계산 결과를 읽고 있습니다."
                                   : "시트의 보고 날짜를 확인하고 있습니다.");
            QStringList args{"--config",
                             sessionConfigPath,
                             "--date",
                             date->selectedDate().toString(Qt::ISODate),
                             "--fetch",
                             "--out",
                             modelPath};
            if (report)
                args << "--analyze";
            start("Youngkly-cli.exe", args);
        } catch (const std::exception &e) {
            fail(QString::fromUtf8(e.what()));
        }
    }
    void advance() {
        if (stage == 0) {
            auto model = youngkly::loadConfig(modelPath);
            for (auto v : youngkly::reportSections(model, session)) {
                auto s = v.toObject();
                QStringList dates;
                for (auto d : s["available_dates"].toArray())
                    dates << d.toString();
                if (!dates.isEmpty())
                    log->appendPlainText(youngkly::displayHeading(s["project_heading"].toString()) + " / " +
                                         s["sheet"].toString() + ": " + dates.join(", "));
            }
            if (!building) {
                status->setText("시트 확인 완료. 아래 기록에서 보고 날짜를 확인하세요.");
                busy(false);
                return;
            }
            bool hasPlan = false;
            for (auto v : session["sources"].toArray())
                if (!youngkly::confirmedFuture(session, date->selectedDate().toString(Qt::ISODate), v.toObject()["id"].toString()).isEmpty()) hasPlan = true;
            if (youngkly::selectedRowCount(model) == 0 && !hasPlan) {
                fail("선택한 날짜에 표시된 행이 없습니다. 시트 날짜 확인 후 보고 날짜를 선택하세요.");
                return;
            }
            if (!directionsPrompted) {
                directionsPrompted = true;
                if (configureDirections(model)) {
                    youngkly::writeFile(sessionConfigPath, QJsonDocument(session).toJson());
                    status->setText("확인한 반응 방향으로 에너지 및 문장을 다시 분석합니다.");
                    start("Youngkly-cli.exe", {"--config", sessionConfigPath,
                          "--date", date->selectedDate().toString(Qt::ISODate),
                          "--analyze", "--out", modelPath});
                    return;
                }
            }
            stage = 1;
            status->setText("분자 뷰 선택 창에서 방향과 수소 표시를 정한 뒤 저장해 주세요.");
            start("youngkly_render.exe",
                  {modelPath, sessionConfigPath, runDirectory + "/assets"});
            return;
        }
        if (stage == 1) {
            saveProjects();
            if (previewOnly) {
                previewOnly = false; busy(false);
                status->setText("보고서 프로젝트를 저장했습니다."); previewReport(); return;
            }
            stage = 2;
            const auto d = QDate::fromString(youngkly::loadConfig(modelPath)["report_date"].toString(), Qt::ISODate);
            const int week = (d.day() + QDate(d.year(), d.month(), 1).dayOfWeek() - 2) / 7 + 1;
            QString author = session["report_author"].toString("이영섭");
            author.remove(QRegularExpression("[\\\\/:*?\\\"<>|]"));
            output = runDirectory + QString("/주간_연구_보고서_%1_%2월_%3주차_%4.docx")
                         .arg(author).arg(d.month())
                         .arg(week)
                         .arg(d.toString("yyMMdd"));
            status->setText("Word 보고서를 작성하고 있습니다.");
            start("Youngkly-cli.exe", {"--config", sessionConfigPath, "--model",
                                       modelPath, "--docx", output, "--force-output"});
            return;
        }
        saveProjects();
        status->setText("완료: " + QDir(root).filePath(output));
        busy(false);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(root).filePath(output)));
    }

  private:
    void busy(bool value) {
        projectList->setEnabled(!value);
        openWord->setEnabled(!value && QFileInfo::exists(output));
        createPreview->setEnabled(!value); loadSaved->setEnabled(!value);
        bool rendered = false;
        if (!value && !modelPath.isEmpty() && QFileInfo::exists(modelPath)) {
            rendered = true;
            for (const auto &section : youngkly::reportSections(youngkly::loadConfig(modelPath), session))
                for (const auto &g : section.toObject()["analyses"].toArray()) {
                    const auto group = g.toObject();
                    if (group["status"] != "missing_outputs" && group["captures"].toArray().size() != group["rows"].toArray().size()) rendered = false;
                }
        }
        exportSaved->setEnabled(!value && rendered && building);
        dispersion->setEnabled(!value); complexation->setEnabled(!value);
        inspect->setEnabled(!value);
        generate->setEnabled(!value);
        date->setEnabled(!value);
        selectAll->setEnabled(!value);
        clearAll->setEnabled(!value);
        plans->setEnabled(!value); cancel->setEnabled(value);
        preview->setEnabled(!value && !modelPath.isEmpty() && QFileInfo::exists(modelPath));
        for (auto *c : checks)
            c->setEnabled(!value);
    }
    void fail(const QString &message) {
        status->setText(message);
        log->appendPlainText(message);
        busy(false);
    }
    void start(const QString &exe, const QStringList &args) {
        QString executable = QDir(QCoreApplication::applicationDirPath()).filePath(exe);
        if (exe == "Youngkly-cli.exe" && !QFileInfo::exists(executable)) executable = QDir(QCoreApplication::applicationDirPath()).filePath("Youngkly.exe");
        process.start(executable, args);
    }
    QString root, output, modelPath, runDirectory, sessionConfigPath;
    bool previewOnly = false;
    QCheckBox *dispersion, *complexation;
    QPushButton *openWord;
    QPushButton *createPreview, *loadSaved, *exportSaved;
    QJsonObject config, session;
    QVector<QCheckBox *> checks;
    QCalendarWidget *date;
    QListWidget *projectList;
    QPlainTextEdit *log;
    QPushButton *inspect, *generate, *open, *preview, *plans, *cancel, *selectAll, *clearAll;
    QLabel *status;
    QProcess process;
    bool building = false, cancelled = false, directionsPrompted = false;
    int stage = 0;
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setFont(QFont("맑은 고딕", 10));
    app.setApplicationName("Youngkly");
    app.setWindowIcon(QIcon(":/youngkly/icon.png"));
    try {
        auto args = app.arguments();
        int i = args.indexOf("--workspace");
        QString root = i >= 0 && i + 1 < args.size() ? args[i + 1] : QDir::currentPath();
        Window window(root);
        window.show();
        if (args.contains("--smoke-test")) {
            QJsonObject result{
                {"status", "ok"},
                {"qt", qVersion()},
                {"workspace", root},
                {"projects", youngkly::loadConfig("config/youngkly.json")["sources"].toArray().size()}};
            youngkly::writeFile(".youngkly/app-smoke.json", QJsonDocument(result).toJson());
            QTimer::singleShot(500, &app, [&] {
                window.grab().save(".youngkly/app-smoke.png");
                app.quit();
            });
        }
        return app.exec();
    } catch (const std::exception &e) {
        QMessageBox::critical(nullptr, "Youngkly", QString::fromUtf8(e.what()));
        return 1;
    }
}
