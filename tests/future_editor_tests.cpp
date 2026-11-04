#include "FutureEditor.h"
#include "FutureWork.h"
#include "Archive.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QTextStream>
#include <stdexcept>
void check(bool ok, const char *why) { if (!ok) throw std::runtime_error(why); }
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    try {
        QTemporaryDir temp;
        auto file = temp.filePath("plans.json");
        QJsonObject config{{"future_work_file", file}, {"sources", QJsonArray{QJsonObject{{"id", "p"}, {"project_heading", "Test"}}}}};
        QJsonObject prior{{"report_date", "2026-10-14"}, {"project_id", "p"}, {"next_action", "prior"}, {"confirmed", true}};
        youngkly::writeFile(file, QJsonDocument(QJsonObject{{"items", QJsonArray{prior}}}).toJson());
        bool ran = false;
        auto automate = [&](auto action) {
            QTimer::singleShot(50, &app, [&, action] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (!dialog) { app.exit(3); return; }
                action(dialog); ran = true;
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
            });
            editFutureWork(nullptr, config, QDate(2026, 10, 21));
            check(ran, "Editor automation ran"); ran = false;
        };
        automate([](QDialog *dialog) {
            for (auto *button : dialog->findChildren<QPushButton *>()) if (button->text() == "계획 추가") button->click();
            auto *table = dialog->findChild<QTableWidget *>();
            table->item(0, 1)->setText("주제"); table->item(0, 2)->setText("test-only note");
            table->item(0, 5)->setText("다음 단계의 계산을 진행하고자 합니다.");
            static_cast<QCheckBox *>(table->cellWidget(0, 9))->setChecked(true);
        });
        check(youngkly::futureItems(config).size() == 2 && youngkly::confirmedFuture(config, "2026-10-21", "p").size() == 1, "Editor persisted new plan and prior date");
        automate([](QDialog *dialog) { dialog->findChild<QTableWidget *>()->item(0, 5)->setText("수정된 계산 계획입니다."); });
        check(youngkly::confirmedFuture(config, "2026-10-21", "p")[0].toObject()["next_action"] == "수정된 계산 계획입니다.", "Editor reload and edit");
        automate([](QDialog *dialog) {
            auto *table = dialog->findChild<QTableWidget *>(); table->setCurrentCell(0, 1);
            for (auto *button : dialog->findChildren<QPushButton *>()) if (button->text() == "선택한 계획 삭제") button->click();
        });
        check(youngkly::futureItems(config) == QJsonArray{prior}, "Delete retains other report dates verbatim");
        QTextStream(stdout) << "Future editor add/edit/reopen/delete and date preservation passed\n";
        return 0;
    } catch (const std::exception &e) { QTextStream(stderr) << e.what() << "\n"; return 1; }
}
