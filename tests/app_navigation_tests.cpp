#define main youngkly_application_entry
#include "../src/app_main.cpp"
#undef main
#include <QTemporaryDir>
#include <QTextStream>

void checkUi(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto originalDirectory = QDir::currentPath();
    QTemporaryDir temporary;
    try {
        checkUi(temporary.isValid(), "Temporary workspace");
        QDir().mkpath(temporary.filePath("config"));
        youngkly::writeFile(temporary.filePath("config/youngkly.json"),
            QJsonDocument(QJsonObject{{"sources", QJsonArray{QJsonObject{{"id", "test"},
                {"project_heading", "Test research"}, {"tabs", QJsonArray()}}}}}).toJson());
        Window window(temporary.path()); window.resize(900, 620); window.show(); app.processEvents();
        auto *pages = window.findChild<QStackedWidget *>();
        auto *next = window.findChild<QPushButton *>("nextPage");
        auto *previous = window.findChild<QPushButton *>("previousPage");
        checkUi(pages && pages->count() == 3 && pages->currentIndex() == 0 && !previous->isEnabled(),
                "Project page is first and previous is disabled");
        next->click(); app.processEvents();
        checkUi(pages->currentIndex() == 1 && previous->isEnabled(), "Next creates project and opens research page");
        next->click(); app.processEvents();
        checkUi(pages->currentIndex() == 2 && !next->isEnabled(), "Next opens final report page");
        for (auto *option : window.findChildren<QCheckBox *>()) {
            if (option->text().contains("Dispersion") || option->text().contains("Complexation"))
                checkUi(pages->widget(2)->isAncestorOf(option), "Optional energy terms belong to final report page");
        }
        for (int page = 2; page >= 0; --page) {
            pages->setCurrentIndex(page); app.processEvents();
            checkUi(window.grab().save(originalDirectory + QString("/navigation-page-%1.png").arg(page + 1)), "Save page screenshot");
        }
        auto *projects = window.findChild<QListWidget *>("savedReportProjects");
        checkUi(projects->count() == 1, "Created project is listed");
        const auto projectPath = projects->item(0)->data(Qt::UserRole).toString();
        QTimer::singleShot(0, [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (auto *question = qobject_cast<QMessageBox *>(widget)) question->done(QMessageBox::No);
        });
        window.removeProject();
        checkUi(QFileInfo::exists(projectPath), "Cancelling deletion retains project");
        projects->item(0)->setData(Qt::UserRole, temporary.filePath("outside.json"));
        window.removeProject();
        checkUi(QFileInfo::exists(projectPath), "Deletion rejects a folder outside project root");
        QTextStream(stdout) << "Project navigation, report options, deletion cancellation and path guard passed\n";
        QDir::setCurrent(originalDirectory);
        return 0;
    } catch (const std::exception &error) {
        QDir::setCurrent(originalDirectory);
        QTextStream(stderr) << error.what() << Qt::endl; return 1;
    }
}
