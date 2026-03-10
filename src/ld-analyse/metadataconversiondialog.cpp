/******************************************************************************
 * metadataconversiondialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "metadataconversiondialog.h"
#include "ui_metadataconversiondialog.h"

#include "metadataconverterutil.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
namespace {
QWidget *dialogParentWidget(QWidget *widget)
{
    if (!widget) {
        return nullptr;
    }

    QWidget *window = widget->window();
    return window ? window : widget;
}

QStringList dialogNameFilters(const QString &filters)
{
    return filters.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
}

void applyCommonFileDialogOptions(QFileDialog *dialog)
{
    if (!dialog) {
        return;
    }

    dialog->setOption(QFileDialog::DontResolveSymlinks, true);
#if defined(Q_OS_MACOS)
    dialog->setOption(QFileDialog::DontUseNativeDialog, true);
#endif
}

QString runOpenFileDialog(QWidget *parent,
                          const QString &title,
                          const QString &startPath,
                          const QString &filters)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setNameFilters(dialogNameFilters(filters));
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}

QString runSaveFileDialog(QWidget *parent,
                          const QString &title,
                          const QString &startPath,
                          const QString &filters)
{
    QFileDialog dialog(dialogParentWidget(parent), title, startPath);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters(dialogNameFilters(filters));
    if (!startPath.isEmpty()) {
        dialog.selectFile(startPath);
    }
    applyCommonFileDialogOptions(&dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) {
        return QString();
    }
    return dialog.selectedFiles().constFirst();
}
} // namespace

MetadataConversionDialog::MetadataConversionDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::MetadataConversionDialog)
{
    ui->setupUi(this);
}

MetadataConversionDialog::~MetadataConversionDialog()
{
    delete ui;
}

void MetadataConversionDialog::setSourceDirectory(const QString &directory)
{
    if (!directory.isEmpty()) {
        sourceDirectory = directory;
    }
}
void MetadataConversionDialog::setDefaultInput(const QString &inputFilename)
{
    if (inputFilename.isEmpty()) {
        return;
    }

    const QFileInfo inputInfo(inputFilename);
    const QString suffix = inputInfo.suffix().toLower();
    const bool isJson = (suffix == QLatin1String("json"));
    const bool isDb = (suffix == QLatin1String("db"));
    if (!isJson && !isDb) {
        return;
    }

    const bool jsonToSqlite = isJson;
    ui->directionComboBox->setCurrentIndex(jsonToSqlite ? 0 : 1);
    ui->inputLineEdit->setText(inputFilename);
    ui->outputLineEdit->setText(MetadataConverterUtil::defaultMetadataOutputPath(inputFilename, jsonToSqlite));
    ui->statusLabel->clear();

    if (inputInfo.exists()) {
        sourceDirectory = inputInfo.absolutePath();
    }
}

void MetadataConversionDialog::on_inputBrowseButton_clicked()
{
    const bool jsonToSqlite = (ui->directionComboBox->currentIndex() == 0);
    const QString filter = jsonToSqlite
                               ? tr("JSON metadata (*.json);;All Files (*)")
                               : tr("SQLite metadata (*.db);;All Files (*)");
    const QString inputFileName = runOpenFileDialog(this,
                                                    tr("Select metadata input"),
                                                    sourceDirectory,
                                                    filter);
    if (inputFileName.isEmpty()) {
        return;
    }

    ui->inputLineEdit->setText(inputFileName);
    ui->outputLineEdit->setText(MetadataConverterUtil::defaultMetadataOutputPath(inputFileName, jsonToSqlite));
    ui->statusLabel->clear();

    const QFileInfo selectedInfo(inputFileName);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void MetadataConversionDialog::on_outputBrowseButton_clicked()
{
    const bool jsonToSqlite = (ui->directionComboBox->currentIndex() == 0);
    const QString filter = jsonToSqlite
                               ? tr("SQLite metadata (*.db);;All Files (*)")
                               : tr("JSON metadata (*.json);;All Files (*)");
    QString suggestedOutput = ui->outputLineEdit->text().trimmed();
    if (suggestedOutput.isEmpty()) {
        suggestedOutput = MetadataConverterUtil::defaultMetadataOutputPath(ui->inputLineEdit->text().trimmed(), jsonToSqlite);
    }
    const QString outputFileName = runSaveFileDialog(this,
                                                     tr("Select metadata output"),
                                                     suggestedOutput,
                                                     filter);
    if (outputFileName.isEmpty()) {
        return;
    }

    ui->outputLineEdit->setText(outputFileName);
    ui->statusLabel->clear();

    const QFileInfo selectedInfo(outputFileName);
    if (selectedInfo.exists()) {
        sourceDirectory = selectedInfo.absolutePath();
    }
}

void MetadataConversionDialog::on_directionComboBox_currentIndexChanged(int)
{
    const bool jsonToSqlite = (ui->directionComboBox->currentIndex() == 0);
    const QString inputFileName = ui->inputLineEdit->text().trimmed();
    if (!inputFileName.isEmpty()) {
        ui->outputLineEdit->setText(MetadataConverterUtil::defaultMetadataOutputPath(inputFileName, jsonToSqlite));
    }
    ui->statusLabel->clear();
}

void MetadataConversionDialog::on_convertButton_clicked()
{
    const QString inputFileName = ui->inputLineEdit->text().trimmed();
    const QString outputFileName = ui->outputLineEdit->text().trimmed();
    if (inputFileName.isEmpty() || outputFileName.isEmpty()) {
        ui->statusLabel->setText(tr("Please choose both an input and output file."));
        return;
    }

    const bool jsonToSqlite = (ui->directionComboBox->currentIndex() == 0);
    const QString direction = jsonToSqlite ? QStringLiteral("json-to-sqlite")
                                           : QStringLiteral("sqlite-to-json");
    QString errorMessage;
    if (!MetadataConverterUtil::runMetadataConverter(direction, inputFileName, outputFileName, &errorMessage)) {
        ui->statusLabel->setText(tr("Conversion failed."));
        QMessageBox messageBox;
        messageBox.warning(this, tr("Error"), errorMessage);
        return;
    }

    ui->statusLabel->setText(tr("Conversion complete."));
}
