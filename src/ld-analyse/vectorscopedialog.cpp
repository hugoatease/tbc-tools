/******************************************************************************
 * vectorscopedialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2022 Adam Sampson
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "vectorscopedialog.h"
#include "ui_vectorscopedialog.h"
#include "tbc/logging.h"

#include <cmath>
#include <random>

#include <QApplication>
#include <QButtonGroup>
#include <QDebug>
#include <QGroupBox>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QVBoxLayout>

VectorscopeDialog::VectorscopeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VectorscopeDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Window);

    // Set up field selection colors
    QColor firstFieldColor = QColor(255, 255, 0);   // Yellow for first field
    QColor secondFieldColor = QColor(0, 255, 255);  // Cyan for second field

    ui->fieldSelectFirstRadioButton->setStyleSheet(
        QString("color: %1;").arg(firstFieldColor.name()));
    ui->fieldSelectSecondRadioButton->setStyleSheet(
        QString("color: %1;").arg(secondFieldColor.name()));

    initialiseAdvancedControls();
}

VectorscopeDialog::~VectorscopeDialog()
{
    delete ui;
}

void VectorscopeDialog::showTraceImage(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters,
                                       const TbcSource::ViewMode& viewMode, const bool isFirstField)
{
    tbcDebugStream() << "VectorscopeDialog::showTraceImage(): Called";

    // Set/enable/disable controls based on view
    switch (viewMode) {
        case TbcSource::ViewMode::FRAME_VIEW:
        case TbcSource::ViewMode::SPLIT_VIEW:
            ui->fieldSelectAllRadioButton->setEnabled(true);
            ui->fieldSelectFirstRadioButton->setEnabled(true);
            ui->fieldSelectSecondRadioButton->setEnabled(true);
            ui->blendColorCheckBox->setEnabled(true);
            break;

        case TbcSource::ViewMode::FIELD_VIEW:
            ui->fieldSelectAllRadioButton->setEnabled(false);
            ui->blendColorCheckBox->setEnabled(false);
            ui->blendColorCheckBox->setChecked(false);

            if (isFirstField) {
                ui->fieldSelectFirstRadioButton->setEnabled(true);
                ui->fieldSelectSecondRadioButton->setEnabled(false);
                ui->fieldSelectFirstRadioButton->setChecked(true);
            } else {
                ui->fieldSelectFirstRadioButton->setEnabled(false);
                ui->fieldSelectSecondRadioButton->setEnabled(true);
                ui->fieldSelectSecondRadioButton->setChecked(true);
            }
            break;
    }


    updateAreaControlState(componentFrame, videoParameters);

    // Draw the image
    QImage traceImage = getTraceImage(componentFrame, videoParameters);

    // Add the QImage to the QLabel in the dialogue
    ui->scopeLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->scopeLabel->setAlignment(Qt::AlignCenter);
    ui->scopeLabel->setScaledContents(true);
    ui->scopeLabel->setPixmap(QPixmap::fromImage(traceImage));

    // QT Bug workaround for some macOS versions
    #if defined(Q_OS_MACOS)
        repaint();
    #endif
}

bool VectorscopeDialog::isCustomAreaModeSelected() const
{
    return areaModeCustomRadioButton && areaModeCustomRadioButton->isChecked();
}

QRect VectorscopeDialog::customAreaRect() const
{
    if (!hasAreaContext || areaFrameWidth <= 0 || areaFrameHeight <= 0) {
        return QRect();
    }

    return QRect(customXStart,
                 customYStart,
                 (customXEnd - customXStart) + 1,
                 (customYEnd - customYStart) + 1);
}

void VectorscopeDialog::setCustomAreaModeSelected(bool selected)
{
    if (!areaModeCustomRadioButton || areaModeCustomRadioButton->isChecked() == selected) {
        return;
    }

    areaModeCustomRadioButton->setChecked(selected);
    emit scopeChanged();
}

void VectorscopeDialog::setCustomAreaRect(const QRect &areaRect)
{
    if (!hasAreaContext || areaFrameWidth <= 0 || areaFrameHeight <= 0 || !areaModeCustomRadioButton) {
        return;
    }

    const bool wasCustomModeSelected = areaModeCustomRadioButton->isChecked();

    const QRect frameBounds(0, 0, areaFrameWidth, areaFrameHeight);
    const QRect clampedRect = areaRect.normalized().intersected(frameBounds);
    if (clampedRect.width() <= 0 || clampedRect.height() <= 0) {
        return;
    }

    const qint32 newCustomXStart = clampedRect.left();
    const qint32 newCustomXEnd = clampedRect.right();
    const qint32 newCustomYStart = clampedRect.top();
    const qint32 newCustomYEnd = clampedRect.bottom();

    const bool changed = (newCustomXStart != customXStart)
        || (newCustomXEnd != customXEnd)
        || (newCustomYStart != customYStart)
        || (newCustomYEnd != customYEnd);

    customXStart = newCustomXStart;
    customXEnd = newCustomXEnd;
    customYStart = newCustomYStart;
    customYEnd = newCustomYEnd;

    if (!wasCustomModeSelected) {
        areaModeCustomRadioButton->setChecked(true);
    }
    if (changed || !wasCustomModeSelected) {
        emit scopeChanged();
    }
}

QImage VectorscopeDialog::getTraceImage(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters)
{
    // Scope size and scale
    constexpr qint32 SIZE = 1024;
    constexpr qint32 SCALE = 65536 / SIZE;
    constexpr qint32 HALF_SIZE = SIZE / 2;
    const bool densityMode = renderModeDensityRadioButton && renderModeDensityRadioButton->isChecked();
    const bool blendColors = ui->blendColorCheckBox->isChecked();

    if (componentFrame.getWidth() <= 0 || componentFrame.getHeight() <= 0) {
        QImage emptyImage(SIZE, SIZE, QImage::Format_RGB888);
        emptyImage.fill(Qt::black);
        return emptyImage;
    }

    // Define image with width, height and format
    QImage scopeImage(SIZE, SIZE, densityMode ? QImage::Format_ARGB32 : QImage::Format_RGB888);
    QPainter scopePainter;

    // Set the background to black
    scopeImage.fill(Qt::black);

    // Attach the scope image to the painter
    scopePainter.begin(&scopeImage);

    // Blend the field colors
    if (blendColors || densityMode) {
        scopePainter.setCompositionMode(QPainter::CompositionMode_Plus);
    }

    // Initialise a cheap, predictable random number generator, for defocussing
    std::minstd_rand randomEngine(12345);
    std::normal_distribution<double> normalDist(0.0, 100.0);

    const bool defocus = ui->defocusCheckBox->isChecked();

    // Skip second field if first only is selected
    qint32 fieldCount = !ui->fieldSelectFirstRadioButton->isChecked() ? 2 : 1;

    // Skip first field if second only is selected
    qint32 startingFrame = !ui->fieldSelectSecondRadioButton->isChecked() ? 0 : 1;

    qint32 xStart = qBound<qint32>(0, videoParameters.activeVideoStart, componentFrame.getWidth() - 1);
    qint32 xEndInclusive = qBound<qint32>(xStart, videoParameters.activeVideoEnd - 1, componentFrame.getWidth() - 1);
    qint32 yStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, componentFrame.getHeight() - 1);
    qint32 yEndInclusive = qBound<qint32>(yStart, videoParameters.lastActiveFrameLine - 1, componentFrame.getHeight() - 1);

    if (areaModeFullRadioButton && areaModeFullRadioButton->isChecked()) {
        xStart = 0;
        xEndInclusive = componentFrame.getWidth() - 1;
        yStart = 0;
        yEndInclusive = componentFrame.getHeight() - 1;
    } else if (areaModeCustomRadioButton && areaModeCustomRadioButton->isChecked()) {
        xStart = qBound<qint32>(0, customXStart, componentFrame.getWidth() - 1);
        xEndInclusive = qBound<qint32>(xStart, customXEnd, componentFrame.getWidth() - 1);
        yStart = qBound<qint32>(0, customYStart, componentFrame.getHeight() - 1);
        yEndInclusive = qBound<qint32>(yStart, customYEnd, componentFrame.getHeight() - 1);
    }

    const qint32 xEnd = xEndInclusive + 1;
    const qint32 yEnd = yEndInclusive + 1;

    for (auto fieldN = startingFrame; fieldN < fieldCount; fieldN++) {
        QColor color = Qt::green;
        // Set color to cyan on second field if blend enabled...
        if (ui->fieldSelectAllRadioButton->isChecked() && (blendColors || densityMode) && fieldN == 1) {
            color = Qt::cyan;
        }

        // ...or second only is selected
        if (ui->fieldSelectSecondRadioButton->isChecked()) {
            color = Qt::cyan;
        }

        if (densityMode) {
            color.setAlpha(14);
        }

        scopePainter.setPen(color);

        // For each sample in the selected area, plot its U/V values on the chart
        for (qint32 lineNumber = yStart; lineNumber < yEnd; lineNumber++) {
            if ((lineNumber % 2) != fieldN) {
                continue;
            }

            const auto &uLine = componentFrame.u(lineNumber);
            const auto &vLine = componentFrame.v(lineNumber);
            for (qint32 xPosition = xStart; xPosition < xEnd; xPosition++) {
                // If defocussing, add a random (but normally-distributed) value to U/V
                double uOffset = defocus ? normalDist(randomEngine) : 0.0;
                double vOffset = defocus ? normalDist(randomEngine) : 0.0;

                // On a real vectorscope, U is positive to the right, and V is positive *upwards*
                qint32 x = HALF_SIZE + (static_cast<qint32>(uLine[xPosition] + uOffset) / SCALE);
                qint32 y = HALF_SIZE - (static_cast<qint32>(vLine[xPosition] + vOffset) / SCALE);

                scopePainter.drawPoint(x, y);
            }
        }
    }

    // Overlay the graticule, unless it's disabled
    if (!ui->graticuleNoneRadioButton->isChecked()) {
        scopePainter.setPen(Qt::white);

        // Draw the vertical/horizontal graticule lines and circle
        scopePainter.drawLine(HALF_SIZE, 0, HALF_SIZE, SIZE - 1);
        scopePainter.drawLine(0, HALF_SIZE, SIZE - 1, HALF_SIZE);
        scopePainter.drawEllipse(0, 0, SIZE - 1, SIZE - 1);

        // For NTSC: draw I/Q graticule lines, 33 degrees offset from the axes
        if (videoParameters.system == NTSC) {
            double theta = (-33.0 * M_PI) / 180;
            for (qint32 i = 0; i < 4; i++) {
                scopePainter.drawLine(HALF_SIZE + (0.2 * HALF_SIZE * cos(theta)),
                                      HALF_SIZE + (0.2 * HALF_SIZE * sin(theta)),
                                      HALF_SIZE + (HALF_SIZE * cos(theta)),
                                      HALF_SIZE + (HALF_SIZE * sin(theta)));
                theta += M_PI / 2.0;
            }
        }

        // Scaling factor for which graticule
        const double percent = ui->graticule75RadioButton->isChecked() ? 0.75 : 1.0;

        // Draw graticule targets for the six colour bars
        for (qint32 rgb = 1; rgb < 7; rgb++) {
            // R'G'B' for this bar
            const double R = percent * static_cast<double>((rgb >> 2) & 1);
            const double G = percent * static_cast<double>((rgb >> 1) & 1);
            const double B = percent * static_cast<double>(rgb & 1);

            // Convert R'G'B' to Y'UV [Poynton p337 eq 28.5]
            const double U = (R * -0.147141) + (G * -0.288869) + (B * 0.436010);
            const double V = (R * 0.614975)  + (G * -0.514965) + (B * -0.100010);

            // Convert to angle and magnitude, scaled to match scope coords
            const double barTheta = atan2(-V, U);
            const double barMag = sqrt((V * V) + (U * U)) * (videoParameters.white16bIre - videoParameters.black16bIre) / SCALE;

            // Draw the target grid, with 10 degree angle and 10% magnitude steps
            const double stepTheta = (10.0 * M_PI) / 180.0;
            const double stepMag = 0.1 * barMag;
            for (qint32 step = -1; step < 2; step++) {
                // XXX These should really be curved lines
                const double theta = barTheta + (step * stepTheta);
                scopePainter.drawLine(HALF_SIZE + ((barMag - stepMag) * cos(theta)), HALF_SIZE + ((barMag - stepMag) * sin(theta)),
                                      HALF_SIZE + ((barMag + stepMag) * cos(theta)), HALF_SIZE + ((barMag + stepMag) * sin(theta)));
            }
            for (qint32 step = -1; step < 2; step++) {
                const double mag = barMag + (step * stepMag);
                scopePainter.drawLine(HALF_SIZE + (mag * cos(barTheta - stepTheta)), HALF_SIZE + (mag * sin(barTheta - stepTheta)),
                                      HALF_SIZE + (mag * cos(barTheta + stepTheta)), HALF_SIZE + (mag * sin(barTheta + stepTheta)));
            }
        }

        // XXX Draw a line for the colourburst -- we don't decode it at the moment
    }

    // Return the QImage
    scopePainter.end();
    return scopeImage;
}

// GUI signal handlers ------------------------------------------------------------------------------------------------

void VectorscopeDialog::on_defocusCheckBox_clicked()
{
    emit scopeChanged();
}

void VectorscopeDialog::on_blendColorCheckBox_clicked()
{
    emit scopeChanged();
}

void VectorscopeDialog::on_graticuleButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
    emit scopeChanged();
}

void VectorscopeDialog::on_fieldSelectButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
    emit scopeChanged();
}

void VectorscopeDialog::on_renderModeButtonGroup_buttonClicked(QAbstractButton *button)
{
    (void) button;
    emit scopeChanged();
}

void VectorscopeDialog::on_areaModeButtonGroup_buttonClicked(QAbstractButton *button)
{
    if (button == areaModeActiveRadioButton || button == areaModeFullRadioButton) {
        applyAreaPreset();
    }

    emit scopeChanged();
}

void VectorscopeDialog::initialiseAdvancedControls()
{
    QVBoxLayout *controlsLayout = qobject_cast<QVBoxLayout *>(ui->frame->layout());
    if (!controlsLayout) {
        return;
    }

    QGroupBox *renderGroup = new QGroupBox(tr("Mode"), ui->frame);
    QVBoxLayout *renderLayout = new QVBoxLayout(renderGroup);
    renderModePointsRadioButton = new QRadioButton(tr("Point plot"), renderGroup);
    renderModeDensityRadioButton = new QRadioButton(tr("Density plot"), renderGroup);
    renderModePointsRadioButton->setChecked(true);
    renderLayout->addWidget(renderModePointsRadioButton);
    renderLayout->addWidget(renderModeDensityRadioButton);

    renderModeButtonGroup = new QButtonGroup(this);
    renderModeButtonGroup->addButton(renderModePointsRadioButton);
    renderModeButtonGroup->addButton(renderModeDensityRadioButton);
    connect(renderModeButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this,
            &VectorscopeDialog::on_renderModeButtonGroup_buttonClicked);

    QGroupBox *areaGroup = new QGroupBox(tr("Scope area"), ui->frame);
    QVBoxLayout *areaLayout = new QVBoxLayout(areaGroup);
    areaModeActiveRadioButton = new QRadioButton(tr("Active video"), areaGroup);
    areaModeFullRadioButton = new QRadioButton(tr("Full frame"), areaGroup);
    areaModeCustomRadioButton = new QRadioButton(tr("Custom"), areaGroup);
    areaModeActiveRadioButton->setChecked(true);
    areaLayout->addWidget(areaModeActiveRadioButton);
    areaLayout->addWidget(areaModeFullRadioButton);
    areaLayout->addWidget(areaModeCustomRadioButton);

    areaModeButtonGroup = new QButtonGroup(this);
    areaModeButtonGroup->addButton(areaModeActiveRadioButton);
    areaModeButtonGroup->addButton(areaModeFullRadioButton);
    areaModeButtonGroup->addButton(areaModeCustomRadioButton);
    connect(areaModeButtonGroup,
            QOverload<QAbstractButton *>::of(&QButtonGroup::buttonClicked),
            this,
            &VectorscopeDialog::on_areaModeButtonGroup_buttonClicked);
    QLabel *customAreaHintLabel = new QLabel(tr("Custom is set from the main-view selection tool."), areaGroup);
    customAreaHintLabel->setWordWrap(true);
    areaLayout->addWidget(customAreaHintLabel);

    const int graticuleLabelIndex = controlsLayout->indexOf(ui->graticuleLabel);
    const int insertIndex = graticuleLabelIndex >= 0 ? graticuleLabelIndex : controlsLayout->count();
    controlsLayout->insertWidget(insertIndex, renderGroup);
    controlsLayout->insertWidget(insertIndex + 1, areaGroup);

    const int preferredControlsWidth = qMax(renderGroup->sizeHint().width(), areaGroup->sizeHint().width()) + 24;
    ui->frame->setMinimumWidth(qMax(ui->frame->minimumWidth(), preferredControlsWidth));
}

void VectorscopeDialog::updateAreaControlState(const ComponentFrame &componentFrame, const LdDecodeMetaData::VideoParameters &videoParameters)
{

    const qint32 frameWidth = componentFrame.getWidth();
    const qint32 frameHeight = componentFrame.getHeight();
    if (frameWidth <= 0 || frameHeight <= 0) {
        return;
    }

    const bool frameSizeChanged = !hasAreaContext
                                  || frameWidth != areaFrameWidth
                                  || frameHeight != areaFrameHeight;

    areaFrameWidth = frameWidth;
    areaFrameHeight = frameHeight;
    hasAreaContext = true;

    activeXStart = qBound<qint32>(0, videoParameters.activeVideoStart, frameWidth - 1);
    activeXEnd = qBound<qint32>(activeXStart, videoParameters.activeVideoEnd - 1, frameWidth - 1);
    activeYStart = qBound<qint32>(0, videoParameters.firstActiveFrameLine, frameHeight - 1);
    activeYEnd = qBound<qint32>(activeYStart, videoParameters.lastActiveFrameLine - 1, frameHeight - 1);
    if (frameSizeChanged) {
        customXStart = activeXStart;
        customXEnd = activeXEnd;
        customYStart = activeYStart;
        customYEnd = activeYEnd;
    }

    customXStart = qBound<qint32>(0, customXStart, frameWidth - 1);
    customXEnd = qBound<qint32>(0, customXEnd, frameWidth - 1);
    customYStart = qBound<qint32>(0, customYStart, frameHeight - 1);
    customYEnd = qBound<qint32>(0, customYEnd, frameHeight - 1);

    if (customXEnd < customXStart) {
        customXEnd = customXStart;
    }
    if (customYEnd < customYStart) {
        customYEnd = customYStart;
    }

    if (frameSizeChanged || (areaModeActiveRadioButton && areaModeActiveRadioButton->isChecked())
        || (areaModeFullRadioButton && areaModeFullRadioButton->isChecked())) {
        applyAreaPreset();
    }
}

void VectorscopeDialog::applyAreaPreset()
{
    if (!hasAreaContext) {
        return;
    }

    if (areaModeFullRadioButton && areaModeFullRadioButton->isChecked()) {
        customXStart = 0;
        customXEnd = areaFrameWidth - 1;
        customYStart = 0;
        customYEnd = areaFrameHeight - 1;
        return;
    }

    if (areaModeActiveRadioButton && areaModeActiveRadioButton->isChecked()) {
        customXStart = activeXStart;
        customXEnd = activeXEnd;
        customYStart = activeYStart;
        customYEnd = activeYEnd;
    }
}
