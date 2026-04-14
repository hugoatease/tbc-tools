#include "notesviewerdialog.h"

#include <QAbstractItemView>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QtMath>

NotesViewerDialog::NotesViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Marker Viewer"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumWidth(720);

    auto *mainLayout = new QVBoxLayout(this);

    auto *helpLabel = new QLabel(tr("Manage In/Out and user marker positions/notes"), this);
    helpLabel->setWordWrap(true);
    mainLayout->addWidget(helpLabel);

    auto *rangeLayout = new QGridLayout();
    rangeLayout->addWidget(new QLabel(tr("Field"), this), 0, 0);
    rangeLayout->addWidget(new QLabel(tr("Frame"), this), 0, 1);
    rangeLayout->addWidget(new QLabel(tr("Timecode"), this), 0, 2);
    rangeLayout->addWidget(new QLabel(tr("Go To"), this), 0, 3);

    auto configureFrameSpin = [](QSpinBox *spinBox) {
        spinBox->setRange(0, 1);
        spinBox->setSpecialValueText(QObject::tr("Unset"));
        spinBox->setMinimumWidth(120);
        spinBox->setAlignment(Qt::AlignRight);
    };

    rangeLayout->addWidget(new QLabel(tr("In marker"), this), 1, 0);
    inFrameSpin_ = new QSpinBox(this);
    configureFrameSpin(inFrameSpin_);
    rangeLayout->addWidget(inFrameSpin_, 1, 1);
    inTimecodeLabel_ = new QLabel(QStringLiteral("—"), this);
    rangeLayout->addWidget(inTimecodeLabel_, 1, 2);
    goInButton_ = new QPushButton(tr("Go To"), this);
    goInButton_->setAutoDefault(false);
    rangeLayout->addWidget(goInButton_, 1, 3);

    rangeLayout->addWidget(new QLabel(tr("Out marker"), this), 2, 0);
    outFrameSpin_ = new QSpinBox(this);
    configureFrameSpin(outFrameSpin_);
    rangeLayout->addWidget(outFrameSpin_, 2, 1);
    outTimecodeLabel_ = new QLabel(QStringLiteral("—"), this);
    rangeLayout->addWidget(outTimecodeLabel_, 2, 2);
    goOutButton_ = new QPushButton(tr("Go To"), this);
    goOutButton_->setAutoDefault(false);
    rangeLayout->addWidget(goOutButton_, 2, 3);

    mainLayout->addLayout(rangeLayout);

    auto *notesLabel = new QLabel(tr("User markers"), this);
    notesLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    mainLayout->addWidget(notesLabel);

    notesTable_ = new QTableWidget(this);
    notesTable_->setColumnCount(3);
    notesTable_->setHorizontalHeaderLabels({tr("Frame"), tr("Timecode"), tr("Comment")});
    notesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    notesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    notesTable_->setEditTriggers(QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::EditKeyPressed
                                 | QAbstractItemView::SelectedClicked);
    notesTable_->setAlternatingRowColors(true);
    notesTable_->verticalHeader()->setVisible(false);
    notesTable_->horizontalHeader()->setStretchLastSection(true);
    notesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    notesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    mainLayout->addWidget(notesTable_, 1);

    auto *editorLayout = new QGridLayout();
    editorLayout->addWidget(new QLabel(tr("Frame"), this), 0, 0);
    noteFrameSpin_ = new QSpinBox(this);
    configureFrameSpin(noteFrameSpin_);
    editorLayout->addWidget(noteFrameSpin_, 0, 1);
    editorLayout->addWidget(new QLabel(tr("Comment"), this), 0, 2);
    noteCommentEdit_ = new QLineEdit(this);
    noteCommentEdit_->setPlaceholderText(tr("Optional note/comment for this frame"));
    editorLayout->addWidget(noteCommentEdit_, 0, 3, 1, 3);
    mainLayout->addLayout(editorLayout);

    auto *noteButtonsLayout = new QHBoxLayout();
    addOrUpdateNoteButton_ = new QPushButton(tr("Add / Update Marker"), this);
    addOrUpdateNoteButton_->setAutoDefault(false);
    removeNoteButton_ = new QPushButton(tr("Remove Selected"), this);
    removeNoteButton_->setAutoDefault(false);
    goSelectedNoteButton_ = new QPushButton(tr("Go To Selected"), this);
    goSelectedNoteButton_->setAutoDefault(false);
    auto *addCurrentButton = new QPushButton(tr("Add Marker at Current Frame"), this);
    addCurrentButton->setAutoDefault(false);
    noteButtonsLayout->addWidget(addOrUpdateNoteButton_);
    noteButtonsLayout->addWidget(removeNoteButton_);
    noteButtonsLayout->addWidget(goSelectedNoteButton_);
    noteButtonsLayout->addWidget(addCurrentButton);
    noteButtonsLayout->addStretch(1);
    mainLayout->addLayout(noteButtonsLayout);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch(1);
    auto *applyButton = new QPushButton(tr("Apply"), this);
    applyButton->setAutoDefault(false);
    auto *closeButton = new QPushButton(tr("Close"), this);
    closeButton->setAutoDefault(false);
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(closeButton);
    mainLayout->addLayout(buttonLayout);

    const auto updateRangeUi = [this]() {
        if (applyingState_) {
            return;
        }
        updateTimecodeLabels();
        updateGoButtons();
    };

    connect(inFrameSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateRangeUi](int) { updateRangeUi(); });
    connect(outFrameSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this, [updateRangeUi](int) { updateRangeUi(); });

    connect(goInButton_, &QPushButton::clicked, this, [this]() {
        const qint32 frame = inFrameSpin_->value();
        if (frame > 0) {
            emit goToFrameRequested(frame);
        }
    });
    connect(goOutButton_, &QPushButton::clicked, this, [this]() {
        const qint32 frame = outFrameSpin_->value();
        if (frame > 0) {
            emit goToFrameRequested(frame);
        }
    });

    connect(notesTable_, &QTableWidget::itemSelectionChanged, this, &NotesViewerDialog::setEditorFromSelectedRow);
    connect(notesTable_, &QTableWidget::cellChanged, this, &NotesViewerDialog::handleTableItemChanged);
    connect(notesTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= noteFrames_.size()) {
            return;
        }
        emit goToFrameRequested(noteFrames_.at(row));
    });

    connect(addOrUpdateNoteButton_, &QPushButton::clicked, this, &NotesViewerDialog::upsertEditorNote);
    connect(removeNoteButton_, &QPushButton::clicked, this, &NotesViewerDialog::removeSelectedNote);
    connect(goSelectedNoteButton_, &QPushButton::clicked, this, [this]() {
        const int row = notesTable_->currentRow();
        if (row < 0 || row >= noteFrames_.size()) {
            return;
        }
        emit goToFrameRequested(noteFrames_.at(row));
    });
    connect(addCurrentButton, &QPushButton::clicked, this, [this]() {
        const qint32 currentFrame = normaliseFrameInput(currentState_.currentFrame);
        if (currentFrame <= 0) {
            return;
        }
        notesTable_->clearSelection();
        noteFrameSpin_->setValue(currentFrame);
        upsertEditorNote();
    });

    connect(applyButton, &QPushButton::clicked, this, [this]() {
        const int selectedRow = notesTable_->currentRow();
        if (selectedRow >= 0 && selectedRow < noteFrames_.size()) {
            upsertEditorNote();
        }
        qint32 inFrame = (inFrameSpin_->value() > 0) ? inFrameSpin_->value() : -1;
        qint32 outFrame = (outFrameSpin_->value() > 0) ? outFrameSpin_->value() : -1;
        if (inFrame > 0 && outFrame > 0 && outFrame < inFrame) {
            outFrame = inFrame;
        }
        emit notesUpdated(inFrame, outFrame, noteFrames_, noteComments_);
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    updateTimecodeLabels();
    updateGoButtons();
}

void NotesViewerDialog::setState(const NotesViewerState &state)
{
    currentState_ = state;
    currentState_.totalFrames = qMax<qint32>(0, state.totalFrames);
    currentState_.frameRate = qMax(0.001, state.frameRate);
    currentState_.frameBaseRate = qMax<qint32>(1, state.frameBaseRate);
    currentState_.currentFrame = state.currentFrame;

    const int maxFrame = qMax<qint32>(1, currentState_.totalFrames);
    const qint32 currentFrame = normaliseFrameInput(currentState_.currentFrame);

    applyingState_ = true;
    inFrameSpin_->setRange(0, maxFrame);
    outFrameSpin_->setRange(0, maxFrame);
    noteFrameSpin_->setRange(0, maxFrame);
    inFrameSpin_->setValue(normaliseFrameInput(currentState_.inFrame));
    outFrameSpin_->setValue(normaliseFrameInput(currentState_.outFrame));

    QMap<qint32, QString> notesByFrame;
    for (int i = 0; i < currentState_.noteFrames.size(); ++i) {
        const qint32 frame = normaliseFrameInput(currentState_.noteFrames.at(i));
        if (frame <= 0) {
            continue;
        }
        const QString comment = (i < currentState_.noteComments.size())
                                    ? currentState_.noteComments.at(i).trimmed()
                                    : QString();
        notesByFrame.insert(frame, comment);
    }

    setNotesFromMap(notesByFrame);

    rebuildNotesTable();
    noteFrameSpin_->setValue(currentFrame);
    noteCommentEdit_->clear();
    applyingState_ = false;

    updateTimecodeLabels();
    updateGoButtons();
}

qint32 NotesViewerDialog::normaliseFrameInput(qint32 frameNumber) const
{
    if (frameNumber <= 0 || currentState_.totalFrames <= 0) {
        return 0;
    }
    return qBound<qint32>(1, frameNumber, currentState_.totalFrames);
}

QString NotesViewerDialog::frameToTimecode(qint32 frameNumber) const
{
    if (frameNumber <= 0) {
        return QStringLiteral("—");
    }

    const qint64 frameIndex = qMax<qint64>(0, static_cast<qint64>(frameNumber) - 1);
    const double totalSecondsExact = static_cast<double>(frameIndex) / currentState_.frameRate;
    const qint64 totalSeconds = static_cast<qint64>(qFloor(totalSecondsExact));
    const double fractionalSeconds = totalSecondsExact - static_cast<double>(totalSeconds);
    const int framePart = qBound(0,
                                 static_cast<int>(qFloor((fractionalSeconds * currentState_.frameBaseRate) + 1e-9)),
                                 currentState_.frameBaseRate - 1);
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(framePart, 2, 10, QChar('0'));
}

void NotesViewerDialog::updateTimecodeLabels()
{
    inTimecodeLabel_->setText(frameToTimecode(inFrameSpin_->value()));
    outTimecodeLabel_->setText(frameToTimecode(outFrameSpin_->value()));
}

void NotesViewerDialog::updateGoButtons()
{
    goInButton_->setEnabled(inFrameSpin_->value() > 0);
    goOutButton_->setEnabled(outFrameSpin_->value() > 0);

    const int row = notesTable_->currentRow();
    const bool hasSelection = row >= 0 && row < noteFrames_.size();
    removeNoteButton_->setEnabled(hasSelection);
    goSelectedNoteButton_->setEnabled(hasSelection);
    addOrUpdateNoteButton_->setEnabled(noteFrameSpin_->value() > 0);
}

void NotesViewerDialog::rebuildNotesTable()
{
    applyingState_ = true;
    notesTable_->setRowCount(noteFrames_.size());
    for (int row = 0; row < noteFrames_.size(); ++row) {
        auto *frameItem = new QTableWidgetItem(QString::number(noteFrames_.at(row)));
        frameItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto *timecodeItem = new QTableWidgetItem(frameToTimecode(noteFrames_.at(row)));
        timecodeItem->setTextAlignment(Qt::AlignCenter);
        auto *commentItem = new QTableWidgetItem((row < noteComments_.size()) ? noteComments_.at(row) : QString());
        timecodeItem->setFlags(timecodeItem->flags() & ~Qt::ItemIsEditable);

        notesTable_->setItem(row, 0, frameItem);
        notesTable_->setItem(row, 1, timecodeItem);
        notesTable_->setItem(row, 2, commentItem);
    }
    notesTable_->clearSelection();
    notesTable_->setCurrentItem(nullptr);
    applyingState_ = false;
}

void NotesViewerDialog::setEditorFromSelectedRow()
{
    if (applyingState_) {
        return;
    }
    const int row = notesTable_->currentRow();
    if (row < 0 || row >= noteFrames_.size()) {
        updateGoButtons();
        return;
    }

    applyingState_ = true;
    noteFrameSpin_->setValue(noteFrames_.at(row));
    noteCommentEdit_->setText((row < noteComments_.size()) ? noteComments_.at(row) : QString());
    applyingState_ = false;
    updateGoButtons();
}

void NotesViewerDialog::handleTableItemChanged(int row, int column)
{
    if (applyingState_) {
        return;
    }
    if (row < 0 || row >= noteFrames_.size()) {
        return;
    }
    if (column != 0 && column != 2) {
        return;
    }

    const qint32 originalFrame = noteFrames_.at(row);
    const QString originalComment = (row < noteComments_.size()) ? noteComments_.at(row) : QString();

    qint32 editedFrame = originalFrame;
    QString editedComment = originalComment;

    if (column == 0) {
        bool ok = false;
        const QString frameText = notesTable_->item(row, 0) ? notesTable_->item(row, 0)->text().trimmed() : QString();
        editedFrame = normaliseFrameInput(frameText.toInt(&ok));
        if (!ok || editedFrame <= 0) {
            editedFrame = originalFrame;
        }
    }

    if (column == 2) {
        editedComment = notesTable_->item(row, 2) ? notesTable_->item(row, 2)->text().trimmed() : QString();
    } else {
        editedComment = originalComment;
    }

    QMap<qint32, QString> notesByFrame;
    for (int i = 0; i < noteFrames_.size(); ++i) {
        if (i == row) {
            continue;
        }
        const QString comment = (i < noteComments_.size()) ? noteComments_.at(i) : QString();
        notesByFrame.insert(noteFrames_.at(i), comment);
    }
    notesByFrame.insert(editedFrame, editedComment);

    setNotesFromMap(notesByFrame);
    rebuildNotesTable();
    selectNoteFrame(editedFrame);
    updateGoButtons();
}

void NotesViewerDialog::upsertEditorNote()
{
    const qint32 frame = normaliseFrameInput(noteFrameSpin_->value());
    if (frame <= 0) {
        updateGoButtons();
        return;
    }
    const int selectedRow = notesTable_->currentRow();

    QMap<qint32, QString> notesByFrame;
    for (int i = 0; i < noteFrames_.size(); ++i) {
        if (i == selectedRow) {
            continue;
        }
        const QString comment = (i < noteComments_.size()) ? noteComments_.at(i) : QString();
        notesByFrame.insert(noteFrames_.at(i), comment);
    }
    notesByFrame.insert(frame, noteCommentEdit_->text().trimmed());
    setNotesFromMap(notesByFrame);

    rebuildNotesTable();
    selectNoteFrame(frame);
    updateGoButtons();
}

void NotesViewerDialog::removeSelectedNote()
{
    const int row = notesTable_->currentRow();
    if (row < 0 || row >= noteFrames_.size()) {
        updateGoButtons();
        return;
    }

    noteFrames_.removeAt(row);
    if (row < noteComments_.size()) {
        noteComments_.removeAt(row);
    }

    rebuildNotesTable();
    if (!noteFrames_.isEmpty()) {
        const int nextRow = qBound(0, row, noteFrames_.size() - 1);
        notesTable_->selectRow(nextRow);
    } else {
        noteCommentEdit_->clear();
    }
    updateGoButtons();
}

void NotesViewerDialog::selectNoteFrame(qint32 frameNumber)
{
    for (int row = 0; row < noteFrames_.size(); ++row) {
        if (noteFrames_.at(row) == frameNumber) {
            notesTable_->selectRow(row);
            return;
        }
    }
}

void NotesViewerDialog::setNotesFromMap(const QMap<qint32, QString> &notesByFrame)
{
    noteFrames_.clear();
    noteComments_.clear();
    noteFrames_.reserve(notesByFrame.size());
    noteComments_.reserve(notesByFrame.size());
    for (auto it = notesByFrame.cbegin(); it != notesByFrame.cend(); ++it) {
        noteFrames_.append(it.key());
        noteComments_.append(it.value());
    }
}
