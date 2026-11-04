#include "Archive.h"
#include "Pipeline.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QIcon>
#include <QUrl>
#include <QVBoxLayout>
#include <QFont>
#include <stdexcept>

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
        resize(800, 700);
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
        dateLayout->addStretch();
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
        choices->addStretch();
        selection->addWidget(box, 1);
        layout->addLayout(selection);
        auto *buttons = new QHBoxLayout;
        inspect = new QPushButton("시트 날짜 확인");
        generate = new QPushButton("Word 보고서 작성");
        open = new QPushButton("결과 폴더 열기");
        buttons->addWidget(inspect);
        buttons->addWidget(generate);
        buttons->addWidget(open);
        plans = new QPushButton("Future work 편집"); buttons->addWidget(plans);
        cancel = new QPushButton("작업 취소"); cancel->setEnabled(false); buttons->addWidget(cancel);
        connect(plans, &QPushButton::clicked, this, [this] {
            try { editFutureWork(this, config, date->selectedDate()); }
            catch (const std::exception &e) { fail(QString::fromUtf8(e.what())); }
        });
        connect(cancel, &QPushButton::clicked, this, [this] { cancelled = true; process.kill(); status->setText("작업을 취소했습니다."); });
        layout->addLayout(buttons);
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
        connect(inspect, &QPushButton::clicked, this, [this] { begin(false); });
        connect(generate, &QPushButton::clicked, this, [this] { begin(true); });
        connect(open, &QPushButton::clicked, this, [this] {
            QDir().mkpath("outputs");
            QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(root).filePath("outputs")));
        });
    }
    void begin(bool report) {
        try {
            session = config; cancelled = false;
            runDirectory = "outputs/run-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz");
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
            QDir().mkpath(".youngkly");
            youngkly::writeFile(".youngkly/gui-config.json", QJsonDocument(session).toJson());
            building = report;
            stage = 0;
            log->clear();
            busy(true);
            status->setText(report ? "시트와 계산 결과를 읽고 있습니다."
                                   : "시트의 보고 날짜를 확인하고 있습니다.");
            QStringList args{"--config",
                             ".youngkly/gui-config.json",
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
            stage = 1;
            status->setText("분자 뷰 선택 창에서 방향과 수소 표시를 정한 뒤 저장해 주세요.");
            start("youngkly_render.exe",
                  {modelPath, ".youngkly/gui-config.json", runDirectory + "/assets"});
            return;
        }
        if (stage == 1) {
            stage = 2;
            const auto d = date->selectedDate();
            const int week = (d.day() + QDate(d.year(), d.month(), 1).dayOfWeek() - 2) / 7 + 1;
            output = runDirectory + QString("/주간_연구_보고서_이영섭_%1월_%2주차_%3.docx")
                         .arg(d.month())
                         .arg(week)
                         .arg(d.toString("yyMMdd"));
            status->setText("Word 보고서를 작성하고 있습니다.");
            start("Youngkly-cli.exe", {"--config", ".youngkly/gui-config.json", "--model",
                                       modelPath, "--docx", output});
            return;
        }
        status->setText("완료: " + QDir(root).filePath(output));
        busy(false);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(root).filePath(output)));
    }

  private:
    void busy(bool value) {
        inspect->setEnabled(!value);
        generate->setEnabled(!value);
        date->setEnabled(!value);
        selectAll->setEnabled(!value);
        clearAll->setEnabled(!value);
        plans->setEnabled(!value); cancel->setEnabled(value);
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
    QString root, output, modelPath, runDirectory;
    QJsonObject config, session;
    QVector<QCheckBox *> checks;
    QCalendarWidget *date;
    QPlainTextEdit *log;
    QPushButton *inspect, *generate, *open, *plans, *cancel, *selectAll, *clearAll;
    QLabel *status;
    QProcess process;
    bool building = false, cancelled = false;
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
