#include "FutureEditor.h"
#include "FutureWork.h"
#include "Archive.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

void editFutureWork(QWidget *parent, const QJsonObject &config, const QDate &date) {
    QDialog dialog(parent); dialog.setWindowTitle("Future work — " + date.toString(Qt::ISODate)); dialog.resize(1400, 650);
    auto *layout = new QVBoxLayout(&dialog);
    auto *help = new QLabel("선택한 보고 날짜의 계획입니다. 문장 칸에는 보고서에 들어갈 완성된 문장을 적어 주세요.\n출처와 다음 행동이 있고 '확정'한 항목만 보고서에 포함됩니다. 지난 날짜의 항목은 원래 날짜로 보존됩니다.");
    help->setWordWrap(true); layout->addWidget(help);
    auto *table = new QTableWidget(0, 10);
    table->setHorizontalHeaderLabels({"연구", "세부 주제", "출처 / 근거", "현재 확인한 결과", "미해결 질문", "다음 행동", "목적", "상태", "일정 (선택)", "확정"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    for (int c = 0; c < 10; ++c) table->setColumnWidth(c, c == 0 ? 230 : c == 9 ? 60 : 220);
    layout->addWidget(table, 1);
    auto all = youngkly::futureItems(config); QJsonArray preserved;
    const QStringList keys{"project_id", "topic", "source", "current_result", "open_question", "next_action", "purpose", "status", "schedule", "confirmed"};
    auto add = [&](const QJsonObject &item) {
        int r = table->rowCount(); table->insertRow(r);
        auto *project = new QComboBox;
        for (auto v : config["sources"].toArray()) { auto s = v.toObject(); project->addItem(s["project_heading"].toString(), s["id"].toString()); }
        int index = project->findData(item["project_id"].toString());
        if (index < 0 && !item["project_id"].toString().isEmpty()) { project->addItem(item["project_id"].toString(), item["project_id"].toString()); index = project->count() - 1; }
        if (index >= 0) project->setCurrentIndex(index);
        table->setCellWidget(r, 0, project);
        for (int c = 1; c <= 8; ++c) {
            auto *cell = new QTableWidgetItem(item[keys[c]].toString(c == 7 ? "unknown" : ""));
            if (c == 1) cell->setData(Qt::UserRole, item);
            table->setItem(r, c, cell);
        }
        auto *confirmed = new QCheckBox; confirmed->setChecked(item["confirmed"].toBool()); table->setCellWidget(r, 9, confirmed);
    };
    for (auto v : all) {
        if (v.toObject()["report_date"] == date.toString(Qt::ISODate)) add(v.toObject());
        else preserved << v;
    }
    if (!preserved.isEmpty()) {
        auto *prior = new QComboBox;
        for (auto v : preserved) {
            auto item = v.toObject();
            prior->addItem(item["report_date"].toString() + " / " + item["project_id"].toString() + " / " + item["topic"].toString(), item.toVariantMap());
        }
        auto *carry = new QPushButton("현재 날짜의 이월 후보로 추가");
        auto *history = new QHBoxLayout; history->addWidget(prior, 1); history->addWidget(carry); layout->addLayout(history);
        QObject::connect(carry, &QPushButton::clicked, &dialog, [&, prior] {
            auto item = QJsonObject::fromVariantMap(prior->currentData().toMap());
            item["source"] = "이월 후보: " + item["report_date"].toString() + " / " + item["source"].toString();
            item["current_result"] = ""; item["schedule"] = ""; item["status"] = "unknown";
            item["confirmed"] = false; item["report_date"] = date.toString(Qt::ISODate);
            add(item);
        });
    }
    auto *buttons = new QHBoxLayout;
    auto *insert = new QPushButton("계획 추가"); auto *remove = new QPushButton("선택한 계획 삭제");
    buttons->addWidget(insert); buttons->addWidget(remove); buttons->addStretch();
    auto *done = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); buttons->addWidget(done); layout->addLayout(buttons);
    QObject::connect(insert, &QPushButton::clicked, &dialog, [&] { add({}); });
    QObject::connect(remove, &QPushButton::clicked, &dialog, [&] { if (table->currentRow() >= 0) table->removeRow(table->currentRow()); });
    QObject::connect(done, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(done, &QDialogButtonBox::accepted, &dialog, [&] {
        try {
            auto output = preserved;
            for (int r = 0; r < table->rowCount(); ++r) {
                auto item = table->item(r, 1)->data(Qt::UserRole).toJsonObject();
                item["report_date"] = date.toString(Qt::ISODate);
                item["project_id"] = static_cast<QComboBox *>(table->cellWidget(r, 0))->currentData().toString();
                for (int c = 1; c <= 8; ++c) item[keys[c]] = table->item(r, c)->text().trimmed();
                item["confirmed"] = static_cast<QCheckBox *>(table->cellWidget(r, 9))->isChecked();
                if (item["confirmed"].toBool() && (item["source"].toString().isEmpty() || item["next_action"].toString().isEmpty() || youngkly::futureText(item).isEmpty()))
                    throw std::runtime_error("확정 계획에는 출처와 다음 행동의 완성된 문장이 필요합니다.");
                output << item;
            }
            auto path = config["future_work_file"].toString("config/future-work.json"); QDir().mkpath(QFileInfo(path).path());
            youngkly::writeFile(path, QJsonDocument(QJsonObject{{"schema_version", 1}, {"items", output}}).toJson()); dialog.accept();
        } catch (const std::exception &e) { QMessageBox::warning(&dialog, "Future work", QString::fromUtf8(e.what())); }
    });
    dialog.exec();
}
