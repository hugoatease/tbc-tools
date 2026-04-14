#ifndef NOTESVIEWERDIALOG_H
#define NOTESVIEWERDIALOG_H

#include <QDialog>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QtGlobal>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

struct NotesViewerState {
    qint32 totalFrames = 0;
    double frameRate = 30000.0 / 1001.0;
    qint32 frameBaseRate = 30;
    qint32 currentFrame = -1;
    qint32 inFrame = -1;
    qint32 outFrame = -1;
    QVector<qint32> noteFrames;
    QStringList noteComments;
};

class NotesViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit NotesViewerDialog(QWidget *parent = nullptr);
    void setState(const NotesViewerState &state);

signals:
    void goToFrameRequested(qint32 frameNumber);
    void notesUpdated(qint32 inFrame, qint32 outFrame, const QVector<qint32> &noteFrames, const QStringList &noteComments);

private:
    QString frameToTimecode(qint32 frameNumber) const;
    qint32 normaliseFrameInput(qint32 frameNumber) const;
    void updateTimecodeLabels();
    void updateGoButtons();
    void rebuildNotesTable();
    void setEditorFromSelectedRow();
    void handleTableItemChanged(int row, int column);
    void upsertEditorNote();
    void removeSelectedNote();
    void selectNoteFrame(qint32 frameNumber);
    void setNotesFromMap(const QMap<qint32, QString> &notesByFrame);

    NotesViewerState currentState_;
    bool applyingState_ = false;

    QSpinBox *inFrameSpin_ = nullptr;
    QSpinBox *outFrameSpin_ = nullptr;
    QSpinBox *noteFrameSpin_ = nullptr;
    QLabel *inTimecodeLabel_ = nullptr;
    QLabel *outTimecodeLabel_ = nullptr;
    QLineEdit *noteCommentEdit_ = nullptr;
    QTableWidget *notesTable_ = nullptr;
    QPushButton *goInButton_ = nullptr;
    QPushButton *goOutButton_ = nullptr;
    QPushButton *addOrUpdateNoteButton_ = nullptr;
    QPushButton *removeNoteButton_ = nullptr;
    QPushButton *goSelectedNoteButton_ = nullptr;

    QVector<qint32> noteFrames_;
    QStringList noteComments_;
};

#endif // NOTESVIEWERDIALOG_H
