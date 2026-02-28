/******************************************************************************
 * videoparametersdialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "videoparametersdialog.h"
#include "ui_videoparametersdialog.h"

VideoParametersDialog::VideoParametersDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VideoParametersDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);
}

void VideoParametersDialog::setShowExportBoundary(bool enabled)
{
    if (!ui->exportBoundaryCheckBox) {
        return;
    }

    ui->exportBoundaryCheckBox->blockSignals(true);
    ui->exportBoundaryCheckBox->setChecked(enabled);
    ui->exportBoundaryCheckBox->blockSignals(false);
}

VideoParametersDialog::~VideoParametersDialog()
{
    delete ui;
}

void VideoParametersDialog::setVideoParameters(const LdDecodeMetaData::VideoParameters &_videoParameters)
{
    videoParameters = _videoParameters;
    originalActiveVideoStart = videoParameters.activeVideoStart;
    originalActiveVideoWidth = videoParameters.activeVideoEnd - videoParameters.activeVideoStart;

    // Block signals while updating the UI to prevent spurious videoParametersChanged emissions
    ui->activeVideoWidthSpinBox->blockSignals(true);
    ui->activeVideoStartSpinBox->blockSignals(true);
    ui->aspectRatio169RadioButton->blockSignals(true);
    ui->aspectRatio43RadioButton->blockSignals(true);

    // Transfer values to the dialogue

    ui->activeVideoWidthSpinBox->setValue(videoParameters.activeVideoEnd - videoParameters.activeVideoStart);
    ui->activeVideoStartSpinBox->setValue(videoParameters.activeVideoStart);
    ui->activeVideoWidthSpinBox->setMaximum(videoParameters.fieldWidth - videoParameters.activeVideoStart);
    ui->activeVideoStartSpinBox->setMaximum(videoParameters.fieldWidth - 1);
    ui->activeVideoStartSpinBox->setMinimum(videoParameters.colourBurstEnd);

    if (videoParameters.isWidescreen) ui->aspectRatio169RadioButton->setChecked(true);
    else ui->aspectRatio43RadioButton->setChecked(true);

    // Unblock signals
    ui->activeVideoWidthSpinBox->blockSignals(false);
    ui->activeVideoStartSpinBox->blockSignals(false);
    ui->aspectRatio169RadioButton->blockSignals(false);
    ui->aspectRatio43RadioButton->blockSignals(false);
}

// Private slots


void VideoParametersDialog::on_activeVideoStartSpinBox_valueChanged(int value)
{
    videoParameters.activeVideoStart = value;
    // prevent the width from going over the actual field width
    ui->activeVideoWidthSpinBox->setMaximum(videoParameters.fieldWidth - value - 1);
    videoParameters.activeVideoEnd = value + ui->activeVideoWidthSpinBox->value();
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_activeVideoWidthSpinBox_valueChanged(int value)
{
    videoParameters.activeVideoEnd = videoParameters.activeVideoStart + value;
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_activeVideoStartResetButton_clicked()
{
    ui->activeVideoStartSpinBox->setValue(originalActiveVideoStart);
}

void VideoParametersDialog::on_activeVideoWidthResetButton_clicked()
{
    ui->activeVideoWidthSpinBox->setValue(originalActiveVideoWidth);
}

void VideoParametersDialog::on_aspectRatioButtonGroup_buttonClicked(QAbstractButton *button)
{
    videoParameters.isWidescreen = (button == ui->aspectRatio169RadioButton);
    emit videoParametersChanged(videoParameters);
}

void VideoParametersDialog::on_exportBoundaryCheckBox_toggled(bool checked)
{
    emit exportBoundaryToggled(checked);
}
