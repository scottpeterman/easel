// MainWindow: pages. A document holds several drawings, shown as tabs under
// the canvas; the window works on one of them at a time.

#include "mainwindow.h"

#include "brushtool.h"
#include "canvasarea.h"
#include "canvasview.h"
#include "documentio.h"
#include "eyedroppertool.h"
#include "layerpanel.h"
#include "newdocumentdialog.h"
#include "selecttools.h"

#include <QAction>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

QString MainWindow::pageName(int index) const
{
    return index >= 0 && index < pageCount() ? m_pages[index].name : QString();
}

QSize MainWindow::pageSize(int index) const
{
    if (index < 0 || index >= pageCount())
        return {};
    if (index == m_page)
        return canvasSize();
    return m_pages[index].stack ? m_pages[index].stack->size() : QSize();
}

QString MainWindow::uniquePageName(const QString &base) const
{
    const auto taken = [this](const QString &name) {
        return std::any_of(m_pages.cbegin(), m_pages.cend(), [&](const Page &p) { return p.name == name; });
    };
    if (!base.isEmpty() && !taken(base))
        return base;
    // "Page 3" for a new page; "Walk copy 2" for a second copy.
    for (int n = base.isEmpty() ? pageCount() + 1 : 2;; ++n) {
        const QString name = base.isEmpty() ? tr("Page %1").arg(n) : tr("%1 %2").arg(base).arg(n);
        if (!taken(name))
            return name;
    }
}

bool MainWindow::leavePage()
{
    if (!m_stack || m_view->isStroking())
        return false;
    endFilterPreview(false); // a filter being tried out isn't applied
    commitFloating(); // text being typed, a paste being placed, a transform under way
    m_lasso->cancel();
    return true;
}

void MainWindow::parkPage()
{
    Page &page = m_pages[m_page];
    syncComposite(); // the composite is what the page shows when it comes back
    page.selection = m_selection;
    page.cleanId = m_cleanId;
    page.fitted = m_view->isAutoFit();
    page.zoom = m_view->zoom();
    page.rotation = m_view->rotation();
    page.pan = m_view->pan();

    // Nothing may point into the page once it's put away.
    m_brush->setDocument(nullptr, {}, nullptr);
    m_eyedropper->setDocument(nullptr, {});
    m_layerPanel->setStack(nullptr);
    m_view->setDocument(nullptr, {});
    m_moveDragging = false;
    m_opacityLayer = 0;
    m_adjustLayer = 0;
    m_editMask = false;

    std::swap(page.history, m_history);
    page.stack = std::move(m_stack);
}

void MainWindow::unparkPage(int index)
{
    m_page = index;
    Page &page = m_pages[index];
    m_stack = std::move(page.stack);
    std::swap(page.history, m_history);
    m_cleanId = page.cleanId;
    m_opacityLayer = 0;
    m_adjustLayer = 0;
    m_editMask = false;

    m_view->setDocument(m_stack->compositeStore(), canvasSize());
    if (!page.fitted) {
        // Back where it was left. (A page not yet looked at fits the window.)
        m_view->setZoomCentered(page.zoom);
        m_view->rotateBy(page.rotation - m_view->rotation());
        m_view->setPan(page.pan);
    }
    bindTools();
    m_layerPanel->setStack(m_stack.get());
    setSelection(page.selection);
    page.selection = {};
    showPanelForActiveLayer(); // the Adjustment panel for an adjustment layer

    syncPageTabs();
    historyChanged();
    updateTitle();
    updateMemoryLabel();
}

bool MainWindow::setCurrentPage(int index)
{
    if (index < 0 || index >= pageCount())
        return false;
    if (index == m_page)
        return true;
    if (!leavePage()) {
        syncPageTabs(); // the tab clicked goes back to the page still shown
        return false;
    }
    parkPage();
    unparkPage(index);
    return true;
}

bool MainWindow::insertPage(std::unique_ptr<easeletch::LayerStack> stack, const QString &name,
                            const QString &historyLabel)
{
    if (!leavePage())
        return false;
    const int at = m_page + 1;
    parkPage();
    Page page;
    page.id = m_nextPageId++;
    page.name = name;
    page.stack = std::move(stack);
    page.history.reset(historyLabel);
    page.cleanId = page.history.stateId();
    m_pages.insert(m_pages.begin() + at, std::move(page));
    ++m_pagesRevision; // the new page isn't in the saved file
    unparkPage(at);
    return true;
}

bool MainWindow::addPage(const QSize &size, const QColor &background, const QString &paper)
{
    if (size.isEmpty())
        return false;
    auto stack = blankStack(size, background, paper);
    return insertPage(std::move(stack), uniquePageName({}),
                      tr("New %1 × %2").arg(size.width()).arg(size.height()));
}

bool MainWindow::duplicatePage()
{
    if (!leavePage())
        return false;
    syncComposite();
    // Tiles are shared with the original until either is painted on.
    auto stack = std::make_unique<easeletch::LayerStack>(*m_stack);
    const QString name = pageName(m_page);
    return insertPage(std::move(stack), uniquePageName(tr("%1 copy").arg(name)), tr("Copy of %1").arg(name));
}

bool MainWindow::deletePage(int index)
{
    if (index < 0 || index >= pageCount() || !m_stack || m_view->isStroking())
        return false;
    if (pageCount() < 2) {
        statusBar()->showMessage(tr("A document needs at least one page"), 4000);
        return false;
    }
    if (index == m_page) {
        endFilterPreview(false);
        m_floating.cancel(); // nothing floating on a page that's going away needs placing
        endTransform();
        m_lasso->cancel();
        if (m_text.active) {
            m_text = TextSession();
            hideTextPanel();
        }
        m_shape = ShapeSession();
        m_shapeTool->setWorking(false);
        m_view->setHandles({});
        parkPage();
        m_pages.erase(m_pages.begin() + index);
        ++m_pagesRevision;
        unparkPage(std::min(index, pageCount() - 1));
        return true;
    }
    m_pages.erase(m_pages.begin() + index);
    if (index < m_page)
        --m_page;
    pagesChanged();
    return true;
}

bool MainWindow::renamePage(int index, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (index < 0 || index >= pageCount() || trimmed.isEmpty() || trimmed == m_pages[index].name)
        return false;
    m_pages[index].name = trimmed;
    pagesChanged();
    return true;
}

bool MainWindow::movePage(int from, int to)
{
    if (from < 0 || from >= pageCount() || to < 0 || to >= pageCount() || from == to)
        return false;
    const int currentId = m_pages[m_page].id;
    Page page = std::move(m_pages[from]);
    m_pages.erase(m_pages.begin() + from);
    m_pages.insert(m_pages.begin() + to, std::move(page));
    for (int i = 0; i < pageCount(); ++i)
        if (m_pages[i].id == currentId)
            m_page = i;
    pagesChanged();
    return true;
}

void MainWindow::pagesChanged()
{
    ++m_pagesRevision;
    syncPageTabs();
    updateTitle();
}

void MainWindow::syncPageTabs()
{
    if (!m_pageTabs)
        return;
    m_syncingTabs = true;
    while (m_pageTabs->count() > pageCount())
        m_pageTabs->removeTab(m_pageTabs->count() - 1);
    while (m_pageTabs->count() < pageCount())
        m_pageTabs->addTab(QString());
    for (int i = 0; i < pageCount(); ++i) {
        // "&" would otherwise underline the next letter.
        m_pageTabs->setTabText(i, QString(m_pages[i].name).replace(QLatin1Char('&'), QLatin1String("&&")));
        const QSize size = pageSize(i);
        m_pageTabs->setTabToolTip(i, tr("%1 (%2 × %3). Double-click to rename, drag to reorder.")
                                         .arg(m_pages[i].name)
                                         .arg(size.width())
                                         .arg(size.height()));
    }
    m_pageTabs->setCurrentIndex(m_page);
    m_syncingTabs = false;
    if (m_deletePageAct) {
        m_deletePageAct->setEnabled(pageCount() > 1);
        m_nextPageAct->setEnabled(pageCount() > 1);
        m_prevPageAct->setEnabled(pageCount() > 1);
    }
}

void MainWindow::createPageBar()
{
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(new CanvasArea(m_view, central), 1);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    m_pageTabs = new QTabBar(central);
    m_pageTabs->setObjectName(QStringLiteral("PageTabs"));
    m_pageTabs->setShape(QTabBar::RoundedSouth);
    m_pageTabs->setExpanding(false);
    m_pageTabs->setDrawBase(false);
    m_pageTabs->setMovable(true);
    m_pageTabs->setUsesScrollButtons(true);
    m_pageTabs->setElideMode(Qt::ElideNone);
    // The keyboard stays with the canvas: its shortcuts are single letters.
    m_pageTabs->setFocusPolicy(Qt::NoFocus);
    m_pageTabs->setContextMenuPolicy(Qt::CustomContextMenu);
    row->addWidget(m_pageTabs);

    auto *add = new QToolButton(central);
    add->setObjectName(QStringLiteral("AddPageButton"));
    add->setText(tr("+"));
    add->setToolTip(tr("New page"));
    add->setAutoRaise(true);
    add->setFocusPolicy(Qt::NoFocus);
    connect(add, &QToolButton::clicked, this, &MainWindow::showNewPageDialog);
    row->addWidget(add);
    row->addStretch(1);
    column->addLayout(row);
    setCentralWidget(central);

    connect(m_pageTabs, &QTabBar::currentChanged, this, [this](int index) {
        if (!m_syncingTabs)
            setCurrentPage(index);
    });
    // Dragging a tab has already moved it in the bar: the pages follow. The
    // bar isn't rebuilt here, since the drag is still going on.
    connect(m_pageTabs, &QTabBar::tabMoved, this, [this](int from, int to) {
        if (m_syncingTabs || from < 0 || to < 0 || from >= pageCount() || to >= pageCount())
            return;
        const int currentId = m_pages[m_page].id;
        Page page = std::move(m_pages[from]);
        m_pages.erase(m_pages.begin() + from);
        m_pages.insert(m_pages.begin() + to, std::move(page));
        for (int i = 0; i < pageCount(); ++i)
            if (m_pages[i].id == currentId)
                m_page = i;
        ++m_pagesRevision;
        updateTitle();
    });
    connect(m_pageTabs, &QTabBar::tabBarDoubleClicked, this, [this](int index) {
        if (index >= 0)
            showRenamePageDialog(index);
    });
    connect(m_pageTabs, &QWidget::customContextMenuRequested, this, &MainWindow::showPageMenu);
}

void MainWindow::createPageMenu()
{
    QMenu *page = menuBar()->addMenu(tr("&Page"));
    auto *newAct = page->addAction(tr("&New Page..."), this, &MainWindow::showNewPageDialog);
    newAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_N));
    page->addAction(tr("&Duplicate Page"), this, &MainWindow::duplicatePage);
    page->addAction(tr("&Rename Page..."), this, [this] { showRenamePageDialog(m_page); });
    m_deletePageAct = page->addAction(tr("D&elete Page..."), this, [this] { confirmDeletePage(m_page); });
    page->addSeparator();
    m_nextPageAct = page->addAction(tr("Ne&xt Page"), this, [this] { setCurrentPage((m_page + 1) % pageCount()); });
    m_nextPageAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    m_prevPageAct = page->addAction(tr("&Previous Page"), this,
                                    [this] { setCurrentPage((m_page + pageCount() - 1) % pageCount()); });
    m_prevPageAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    page->addSeparator();
    page->addAction(tr("Move Page &Left"), this, [this] { movePage(m_page, m_page - 1); });
    page->addAction(tr("Move Page R&ight"), this, [this] { movePage(m_page, m_page + 1); });
    syncPageTabs();
}

void MainWindow::showNewPageDialog()
{
    if (!m_stack)
        return;
    // The same size as this page unless you say otherwise.
    NewDocumentDialog dlg(canvasSize().isEmpty() ? QSize(2000, 1500) : canvasSize(), this);
    dlg.setWindowTitle(tr("New Page"));
    if (dlg.exec() == QDialog::Accepted)
        addPage(dlg.canvasSize(), dlg.background(), dlg.paper());
}

void MainWindow::showRenamePageDialog(int index)
{
    if (index < 0 || index >= pageCount())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Rename Page"), tr("Page name:"), QLineEdit::Normal,
                                               m_pages[index].name, &ok);
    if (ok)
        renamePage(index, name);
}

void MainWindow::confirmDeletePage(int index)
{
    if (index < 0 || index >= pageCount())
        return;
    if (pageCount() < 2) {
        statusBar()->showMessage(tr("A document needs at least one page"), 4000);
        return;
    }
    // A page has its own undo history, and it goes with the page.
    const auto answer = QMessageBox::warning(
        this, tr("Delete Page"), tr("Delete the page \"%1\"? This can't be undone.").arg(m_pages[index].name),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer == QMessageBox::Yes)
        deletePage(index);
}

void MainWindow::showPageMenu(const QPoint &pos)
{
    const int index = m_pageTabs->tabAt(pos);
    QMenu menu(this);
    menu.addAction(tr("New Page..."), this, &MainWindow::showNewPageDialog);
    if (index >= 0) {
        menu.addAction(tr("Duplicate"), this, [this, index] {
            if (setCurrentPage(index))
                duplicatePage();
        });
        menu.addAction(tr("Rename..."), this, [this, index] { showRenamePageDialog(index); });
        QAction *del = menu.addAction(tr("Delete..."), this, [this, index] { confirmDeletePage(index); });
        del->setEnabled(pageCount() > 1);
        menu.addSeparator();
        QAction *left = menu.addAction(tr("Move Left"), this, [this, index] { movePage(index, index - 1); });
        left->setEnabled(index > 0);
        QAction *right = menu.addAction(tr("Move Right"), this, [this, index] { movePage(index, index + 1); });
        right->setEnabled(index < pageCount() - 1);
    }
    menu.exec(m_pageTabs->mapToGlobal(pos));
}
