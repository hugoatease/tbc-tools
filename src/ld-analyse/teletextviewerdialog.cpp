/******************************************************************************
 * teletextviewerdialog.cpp
 * ld-analyse - TBC output analysis GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2026 Harry Munday
 *
 * This file is part of ld-decode-tools.
 ******************************************************************************/

#include "teletextviewerdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

TeletextViewerDialog::TeletextViewerDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Teletext Viewer"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(900, 650);

    auto *mainLayout = new QVBoxLayout(this);

    auto *directoryLayout = new QHBoxLayout();
    directoryLayout->addWidget(new QLabel(tr("Directory:"), this));
    directoryLineEdit = new QLineEdit(this);
    directoryLineEdit->setReadOnly(true);
    directoryLayout->addWidget(directoryLineEdit, 1);
    browseDirectoryButton = new QPushButton(tr("Browse..."), this);
    refreshListButton = new QPushButton(tr("Refresh List"), this);
    directoryLayout->addWidget(browseDirectoryButton);
    directoryLayout->addWidget(refreshListButton);
    mainLayout->addLayout(directoryLayout);

    auto *pageLayout = new QHBoxLayout();
    pageLayout->addWidget(new QLabel(tr("Page:"), this));
    pageComboBox = new QComboBox(this);
    pageComboBox->setMinimumContentsLength(10);
    pageLayout->addWidget(pageComboBox, 1);
    refreshPageButton = new QPushButton(tr("Refresh Page"), this);
    openInBrowserButton = new QPushButton(tr("Open in Browser"), this);
    pageLayout->addWidget(refreshPageButton);
    pageLayout->addWidget(openInBrowserButton);
    mainLayout->addLayout(pageLayout);

    auto *optionsLayout = new QHBoxLayout();
    autoRefreshCheckBox = new QCheckBox(tr("Live refresh"), this);
    autoRefreshCheckBox->setChecked(true);
    optionsLayout->addWidget(autoRefreshCheckBox);
    optionsLayout->addStretch(1);
    mainLayout->addLayout(optionsLayout);

    pageViewer = new QTextBrowser(this);
    pageViewer->setOpenExternalLinks(true);
    mainLayout->addWidget(pageViewer, 1);

    refreshTimer = new QTimer(this);
    refreshTimer->setInterval(1000);
    refreshTimer->setSingleShot(false);

    connect(browseDirectoryButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::browseForDirectory);
    connect(refreshListButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::refreshPageList);
    connect(pageComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(refreshPageButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::loadSelectedPage);
    connect(openInBrowserButton, &QPushButton::clicked,
            this, &TeletextViewerDialog::openSelectedPageInBrowser);
    connect(autoRefreshCheckBox, &QCheckBox::toggled,
            this, &TeletextViewerDialog::setAutoRefreshEnabled);
    connect(refreshTimer, &QTimer::timeout,
            this, &TeletextViewerDialog::handlePeriodicRefresh);

    setAutoRefreshEnabled(autoRefreshCheckBox->isChecked());
}

void TeletextViewerDialog::setDirectory(const QString &directoryPath)
{
    const QString normalizedPath = QDir::cleanPath(directoryPath.trimmed());
    if (normalizedPath.isEmpty()) {
        return;
    }

    currentDirectoryPath = normalizedPath;
    directoryLineEdit->setText(currentDirectoryPath);
    refreshPageList();
}

QString TeletextViewerDialog::directory() const
{
    return currentDirectoryPath;
}

void TeletextViewerDialog::browseForDirectory()
{
    const QString startPath = currentDirectoryPath.isEmpty() ? QDir::homePath() : currentDirectoryPath;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Select teletext HTML directory"),
        startPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
    );
    if (selectedDirectory.isEmpty()) {
        return;
    }

    setDirectory(selectedDirectory);
}

void TeletextViewerDialog::refreshPageList()
{
    pageComboBox->blockSignals(true);

    const QString previousSelection = pageComboBox->currentText();
    pageComboBox->clear();

    if (!directoryContainsHtml()) {
        pageComboBox->blockSignals(false);
        pageViewer->setHtml(tr("<html><body><p>No teletext HTML pages were found in this directory.</p></body></html>"));
        setWindowTitle(tr("Teletext Viewer"));
        lastLoadedPagePath.clear();
        lastLoadedPageModified = QDateTime();
        return;
    }

    const QDir directory(currentDirectoryPath);
    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    pageComboBox->addItems(htmlPages);

    qint32 selectedIndex = htmlPages.indexOf(previousSelection);
    if (selectedIndex < 0) {
        selectedIndex = htmlPages.indexOf(QStringLiteral("100.html"));
    }
    if (selectedIndex < 0 && !htmlPages.isEmpty()) {
        selectedIndex = 0;
    }
    if (selectedIndex >= 0) {
        pageComboBox->setCurrentIndex(selectedIndex);
    }

    pageComboBox->blockSignals(false);
    loadSelectedPage();
}

void TeletextViewerDialog::loadSelectedPage()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QFileInfo pageInfo(pagePath);
    if (!pageInfo.exists()) {
        return;
    }

    if (lastLoadedPagePath == pagePath && lastLoadedPageModified == pageInfo.lastModified()) {
        return;
    }

    pageViewer->setSearchPaths(QStringList() << currentDirectoryPath);
    pageViewer->setSource(QUrl::fromLocalFile(pagePath));

    lastLoadedPagePath = pagePath;
    lastLoadedPageModified = pageInfo.lastModified();
    setWindowTitle(tr("Teletext Viewer - %1").arg(pageInfo.fileName()));
}

void TeletextViewerDialog::openSelectedPageInBrowser()
{
    const QString pagePath = selectedPagePath();
    if (pagePath.isEmpty()) {
        return;
    }

    const QUrl pageUrl = QUrl::fromLocalFile(pagePath);
    if (!QDesktopServices::openUrl(pageUrl)) {
        QMessageBox::warning(this, tr("Warning"),
                             tr("Could not open teletext page in browser:\n%1").arg(pagePath));
    }
}

void TeletextViewerDialog::setAutoRefreshEnabled(bool enabled)
{
    if (enabled) {
        refreshTimer->start();
    } else {
        refreshTimer->stop();
    }
}

void TeletextViewerDialog::handlePeriodicRefresh()
{
    refreshPageList();
}

bool TeletextViewerDialog::directoryContainsHtml() const
{
    if (currentDirectoryPath.trimmed().isEmpty()) {
        return false;
    }

    const QDir directory(currentDirectoryPath);
    if (!directory.exists()) {
        return false;
    }

    const QStringList htmlPages = directory.entryList(
        QStringList() << QStringLiteral("*.html"),
        QDir::Files,
        QDir::Name | QDir::IgnoreCase
    );
    return !htmlPages.isEmpty();
}

QString TeletextViewerDialog::selectedPagePath() const
{
    if (currentDirectoryPath.trimmed().isEmpty()) {
        return QString();
    }
    if (pageComboBox->currentIndex() < 0) {
        return QString();
    }

    return QDir(currentDirectoryPath).filePath(pageComboBox->currentText());
}
