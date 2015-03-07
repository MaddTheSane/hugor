#include <QDebug>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QClipboard>
#include <QPainter>
#include <QTimer>
#include <QTextCodec>
#include <QMenu>

extern "C" {
#include "heheader.h"
}
#include "hframe.h"
#include "happlication.h"
#include "hmainwindow.h"
#include "hmarginwidget.h"
#include "hugodefs.h"
#include "settings.h"


HFrame* hFrame = 0;


HFrame::HFrame( QWidget* parent )
    : QWidget(parent),
      fInputMode(NoInput),
      fInputReady(false),
      fInputStartX(0),
      fInputStartY(0),
      fInputCurrentChar(0),
      fMaxHistCap(200),
      fCurHistIndex(0),
      fFgColor(HUGO_BLACK),
      fBgColor(HUGO_WHITE),
      fUseFixedFont(false),
      fUseUnderlineFont(false),
      fUseItalicFont(false),
      fUseBoldFont(false),
      fFontMetrics(QFont()),
      fPixmap(1, 1),
      fFlushXPos(0),
      fFlushYPos(0),
      fCursorPos(0, 0),
      fLastCursorPos(0, 0),
      fCursorVisible(false),
      fBlinkVisible(false),
      fBlinkTimer(new QTimer(this)),
      fMuteTimer(new QTimer(this))
{
    // We handle player input, so we need to accept focus.
    this->setFocusPolicy(Qt::WheelFocus);

    connect(this->fBlinkTimer, SIGNAL(timeout()), this, SLOT(fBlinkCursor()));
    this->resetCursorBlinking();
    //this->setCursorVisible(true);

    // Our initial height is the height of the current proportional font.
    this->fHeight = QFontMetrics(hApp->settings()->propFont).height();

    // We need to check whether the application lost focus.
    connect(qApp, SIGNAL(focusChanged(QWidget*,QWidget*)), SLOT(fHandleFocusChange(QWidget*,QWidget*)));

    this->fMuteTimer->setSingleShot(true);
    connect(this->fMuteTimer, SIGNAL(timeout()), SLOT(fMuteSound()));

    // Requesting scrollback simply triggers the scrollback window.
    // Since focus is lost, subsequent scrolling/paging events will work as expected.
    connect(this, SIGNAL(requestScrollback()), hMainWin, SLOT(showScrollback()));

    this->setAttribute(Qt::WA_InputMethodEnabled);
    hFrame = this;
}


void
HFrame::fBlinkCursor()
{
    this->fBlinkVisible = not this->fBlinkVisible;
    this->update(this->fCursorPos.x(), this->fCursorPos.y() + 1, this->fCursorPos.x() + 2,
                 this->fCursorPos.y() + this->fHeight + 2);
}


void
HFrame::fMuteSound()
{
    muteSound(true);
}


void
HFrame::fHandleFocusChange( QWidget* old, QWidget* now )
{
    if (now == 0) {
        // Mute the sound after a while (if a mute isn't already scheduled.)
        // We do this delayed mute because Qt sometimes makes it appear as
        // though the whole application has lost focus when switching from
        // the main window to a dialog.  This happens only for a very brief
        // amount of time.  This allows us to avoid muting the sound if focus
        // is lost only for a very brief moment (which can sometimes confuse
        // the audio backend.)
        if (hApp->settings()->muteSoundInBackground and not this->fMuteTimer->isActive()) {
            this->fMuteTimer->start(60);
        }
        // The application window lost focus.  Disable cursor blinking.
        this->fBlinkTimer->stop();
#ifdef Q_WS_MAC
        // On the Mac, when applications lose focus the cursor must be disabled.
        if (this->fBlinkVisible) {
            this->fBlinkCursor();
        }
#else
        // On all other systems we assume the cursor must stay visible.
        if (not this->fBlinkVisible) {
            this->fBlinkCursor();
        }
#endif
    } else if (old == 0 and now != 0) {
        this->fMuteTimer->stop();
        muteSound(false);
        // The application window gained focus.  Reset cursor blinking.
        this->resetCursorBlinking();
    }
}


void HFrame::fEndInputMode( bool addToHistory )
{
    this->fInputReady = true;
    this->fInputMode = NoInput;
    // The current command only needs to be appended to the history if
    // it's not empty and differs from the previous command in the history.
    if (((this->fHistory.isEmpty() and not fInputBuf.isEmpty())
         or (not this->fHistory.isEmpty()
             and not fInputBuf.isEmpty()
             and fInputBuf != this->fHistory.last()))
        and addToHistory)
    {
        this->fHistory.append(fInputBuf);
        // If we're about to overflow the max history cap, delete the
        // oldest command from the history.
        if (this->fHistory.size() > this->fMaxHistCap) {
            this->fHistory.removeFirst();
        }
    }
    this->fCurHistIndex = 0;
    emit inputReady();
}


void
HFrame::paintEvent( QPaintEvent* )
{
    QPainter p(this);
    p.drawPixmap(0, 0, this->fPixmap);

    // Draw our current input. We need to do this here, after the pixmap
    // has already been painted, so that the input gets painted on top.
    // Otherwise, we could not erase text during editing.
    if (this->fInputMode == NormalInput and not this->fInputBuf.isEmpty()) {
        QFont f(this->fUseFixedFont ? hApp->settings()->fixedFont : hApp->settings()->propFont);
        f.setUnderline(this->fUseUnderlineFont);
        f.setItalic(this->fUseItalicFont);
        f.setBold(this->fUseBoldFont);
        QFontMetrics m(f);
        p.setFont(f);
        p.setPen(hugoColorToQt(this->fFgColor));
        p.setBackgroundMode(Qt::OpaqueMode);
        p.setBackground(QBrush(hugoColorToQt(this->fBgColor)));
        p.drawText(this->fInputStartX, this->fInputStartY + m.ascent() + 1, this->fInputBuf);
    }

    // Likewise, the input caret needs to be painted on top of the input text.
    if (this->fCursorVisible and this->fBlinkVisible) {
        p.setPen(hugoColorToQt(this->fFgColor));
        p.drawLine(this->fCursorPos.x(), this->fCursorPos.y() + 1,
                   this->fCursorPos.x(), this->fCursorPos.y() + 1 + this->fHeight);
    }
}


void
HFrame::resizeEvent( QResizeEvent* e )
{
    // Ignore invalid resizes.  No idea why that happens sometimes, but it
    // does (we get negative values for width or height.)
    if (not e->size().isValid()) {
        e->ignore();
        return;
    }

    // Save a copy of the current pixmap.
    const QPixmap& tmp = this->fPixmap.copy();

    // Adjust the margins so that we get our final size.
    hApp->updateMargins(-1);

    // Create a new pixmap, using the new size and fill it with
    // the default background color.
    QPixmap newPixmap(this->size());
    newPixmap.fill(hugoColorToQt(this->fBgColor));

    // Draw the saved pixmap into the new one and use it as our new
    // display.
    QPainter p(&newPixmap);
    p.drawPixmap(0, 0, tmp);
    this->fPixmap = newPixmap;

    hugo_settextmode();
    display_needs_repaint = true;
}


void
HFrame::keyPressEvent( QKeyEvent* e )
{
    //qDebug() << Q_FUNC_INFO;

    //qDebug() << "Key pressed:" << hex << e->key();

    if (not hApp->gameRunning()) {
        QWidget::keyPressEvent(e);
        return;
    }

    if (e->key() == Qt::Key_Escape) {
        emit escKeyPressed();
    }

    if (this->fInputMode == NoInput) {
        this->singleKeyPressEvent(e);
        return;
    }

    // Just for having shorter identifiers.
    int& i = this->fInputCurrentChar;
    QString& buf = this->fInputBuf;

    // Enable mouse tracking when hiding the cursor so that we can
    // restore it when the mouse is moved.
    hApp->marginWidget()->setCursor(Qt::BlankCursor);
    this->setMouseTracking(true);
    hApp->marginWidget()->setMouseTracking(true);

    if (e->matches(QKeySequence::MoveToStartOfLine) or e->matches(QKeySequence::MoveToStartOfBlock)) {
        i = 0;
    } else if (e->matches(QKeySequence::MoveToEndOfLine) or e->matches(QKeySequence::MoveToEndOfBlock)) {
        i = buf.length();
#if QT_VERSION >= 0x040500
    } else if (e->matches(QKeySequence::InsertParagraphSeparator)) {
#else
    } else if (e->key() == Qt::Key_Enter or e->key() == Qt::Key_Return) {
#endif
        fEndInputMode(true);
        return;
    } else if (e->matches(QKeySequence::Delete)) {
        if (i < buf.length()) {
            buf.remove(i, 1);
        }
    } else if (e->matches(QKeySequence::DeleteEndOfWord)) {
        // Delete all non-alphanumerics first.
        while (i < buf.length() and not buf.at(i).isLetterOrNumber()) {
            buf.remove(i, 1);
        }
        // Delete all alphanumerics.
        while (i < buf.length() and buf.at(i).isLetterOrNumber()) {
            buf.remove(i, 1);
        }
    } else if (e->matches(QKeySequence::DeleteStartOfWord)) {
        // Delete all non-alphanumerics first.
        while (i > 0 and not buf.at(i - 1).isLetterOrNumber()) {
            buf.remove(i - 1, 1);
            --i;
        }
        // Delete all alphanumerics.
        while (i > 0 and buf.at(i - 1).isLetterOrNumber()) {
            buf.remove(i - 1, 1);
            --i;
        }
    } else if (e->matches(QKeySequence::MoveToPreviousChar)) {
        if (i > 0)
            --i;
    } else if (e->matches(QKeySequence::MoveToNextChar)) {
        if (i < buf.length())
            ++i;
    } else if (e->matches(QKeySequence::MoveToPreviousWord)) {
        // Skip all non-alphanumerics first.
        while (i > 0 and not buf.at(i - 1).isLetterOrNumber()) {
            --i;
        }
        // Skip all alphanumerics.
        while (i > 0 and buf.at(i - 1).isLetterOrNumber()) {
            --i;
        }
    } else if (e->matches(QKeySequence::MoveToNextWord)) {
        // Skip all non-alphanumerics first.
        while (i < buf.length() and not buf.at(i).isLetterOrNumber()) {
            ++i;
        }
        // Skip all alphanumerics.
        while (i < buf.length() and buf.at(i).isLetterOrNumber()) {
            ++i;
        }
    } else if (e->matches(QKeySequence::MoveToPreviousLine)) {
        // If we're already at the oldest command in the history, or
        // the history list is empty, don't do anything.
        if (this->fCurHistIndex == this->fHistory.size() or this->fHistory.isEmpty()) {
            return;
        }
        // If the current command is new and not in the history yet,
        // remember it so we can bring it back if the user recalls it.
        if (this->fCurHistIndex == 0) {
            this->fInputBufBackup = buf;
        }
        // Recall the previous command from the history.
        buf = this->fHistory[this->fHistory.size() - 1 - this->fCurHistIndex];
        ++this->fCurHistIndex;
        i = buf.length();
    } else if (e->matches(QKeySequence::MoveToNextLine)) {
        // If we're at the latest command, don't do anything.
        if (this->fCurHistIndex == 0) {
            return;
        }
        --this->fCurHistIndex;
        // If the next command is the latest one, it means it's current
        // new command we backed up previously. So restore it. If not,
        // recall the next command from the history.
        if (this->fCurHistIndex == 0) {
            buf = this->fInputBufBackup;
            this->fInputBufBackup.clear();
        } else {
            buf = this->fHistory[this->fHistory.size() - this->fCurHistIndex];
            i = buf.length();
        }
        i = buf.length();
    } else if (e->matches(QKeySequence::MoveToPreviousPage)) {
        this->requestScrollback();
    } else if (e->key() == Qt::Key_Backspace) {
        if (i > 0 and not buf.isEmpty()) {
            buf.remove(i - 1, 1);
            --i;
        }
    } else {
        QString strToAdd = e->text();
        if (e->matches(QKeySequence::Paste)) {
            strToAdd = QApplication::clipboard()->text();
        } else if (strToAdd.isEmpty() or not strToAdd.at(0).isPrint()) {
            QWidget::keyPressEvent(e);
            return;
        }
        buf.insert(i, strToAdd);
        i += strToAdd.length();
    }
    this->updateCursorPos();
    this->update();
}


void
HFrame::inputMethodEvent( QInputMethodEvent* e )
{
    if (not hApp->gameRunning() or (this->fInputMode == NormalInput and e->commitString().isEmpty())) {
        QWidget::inputMethodEvent(e);
        return;
    }

    // Enable mouse tracking when hiding the cursor so that we can
    // restore it when the mouse is moved.
    hApp->marginWidget()->setCursor(Qt::BlankCursor);
    this->setMouseTracking(true);
    hApp->marginWidget()->setMouseTracking(true);

    const QByteArray& bytes = hApp->hugoCodec()->fromUnicode(e->commitString());
    // If the keypress doesn't correspond to exactly one character, ignore
    // it.
    if (bytes.size() != 1) {
        QWidget::inputMethodEvent(e);
        return;
    }
    this->fKeyQueue.enqueue(bytes[0]);
}


void
HFrame::singleKeyPressEvent( QKeyEvent* event )
{
    //qDebug() << Q_FUNC_INFO;
    Q_ASSERT(this->fInputMode == NoInput);

    switch (event->key()) {
      case 0:
      case Qt::Key_unknown:
        QWidget::keyPressEvent(event);
        return;

      case Qt::Key_Left:
        this->fKeyQueue.enqueue(8);
        break;

      case Qt::Key_Up:
        this->fKeyQueue.enqueue(11);
        break;

      case Qt::Key_Right:
        this->fKeyQueue.enqueue(21);
        break;

      case Qt::Key_Down:
        this->fKeyQueue.enqueue(10);
        break;

      default:
        // If the keypress doesn't correspond to exactly one character, ignore
        // it.
        if (event->text().size() != 1) {
            QWidget::keyPressEvent(event);
            return;
        }
        this->fKeyQueue.enqueue(event->text().at(0).toLatin1());
    }
}


void
HFrame::mousePressEvent( QMouseEvent* e )
{
    if (e->button() != Qt::LeftButton) {
        e->ignore();
        return;
    }
    if (this->fInputMode == NoInput) {
        this->fKeyQueue.append(0);
        this->fClickQueue.append(e->pos());
    }
    e->accept();
}


void
HFrame::mouseDoubleClickEvent( QMouseEvent* e )
{
    if (this->fInputMode != NormalInput or e->button() != Qt::LeftButton) {
        return;
    }
    // Get the word at the double click position.
    QString word(TB_FindWord(e->x(), e->y()));
    if (word.isEmpty()) {
        // No word found.
        return;
    }
    this->insertInputText(word, false, false);
}


void
HFrame::mouseMoveEvent( QMouseEvent* e )
{
    if (this->cursor().shape() == Qt::BlankCursor) {
        this->setMouseTracking(false);
    }
    // Pass it on to our parent.
    e->ignore();
}


void
HFrame::getInput( char* buf, size_t buflen, int xPos, int yPos )
{
    this->flushText();
    //qDebug() << Q_FUNC_INFO;
    Q_ASSERT(buf != 0);

    this->fInputReady = false;
    this->fInputMode = NormalInput;
    this->fInputStartX = xPos;
    this->fInputStartY = yPos;
    this->fInputCurrentChar = 0;

    // Wait for a complete input line.
    while (hApp->gameRunning() and not this->fInputReady) {
        hApp->advanceEventLoop(QEventLoop::WaitForMoreEvents | QEventLoop::AllEvents);
    }

    // Make the input text part of the display pixmap.
    this->printText(this->fInputBuf.toLatin1().constData(), this->fInputStartX, this->fInputStartY);

    qstrncpy(buf, this->fInputBuf.toLatin1(), buflen);
    //qDebug() << this->fInputBuf;
    this->fInputBuf.clear();
}


int
HFrame::getNextKey()
{
    this->flushText();
    //qDebug() << Q_FUNC_INFO;

    //this->scrollDown();

    // If we have a key waiting, return it.
    if (not this->fKeyQueue.isEmpty()) {
        return this->fKeyQueue.dequeue();
    }

    // Wait for at least a key to become available.
    this->update();
    while (this->fKeyQueue.isEmpty() and hApp->gameRunning()) {
        hApp->advanceEventLoop(QEventLoop::WaitForMoreEvents | QEventLoop::AllEvents);
    }

    if (not hApp->gameRunning()) {
        // Game is quitting.
        return -3;
    }
    return this->fKeyQueue.dequeue();
}


QPoint
HFrame::getNextClick()
{
    Q_ASSERT(not this->fClickQueue.isEmpty());
    return this->fClickQueue.dequeue();
}


void
HFrame::clearRegion( int left, int top, int right, int bottom )
{
    this->flushText();
    if (left == 0 and top == 0 and right == 0 and bottom == 0) {
        this->fPixmap.fill(hugoColorToQt(this->fBgColor));
        return;
    }
    QPainter p(&this->fPixmap);
    p.fillRect(left, top, right - left + 1, bottom - top + 1, hugoColorToQt(this->fBgColor));
}


void
HFrame::setFontType( int hugoFont )
{
    this->flushText();
    this->fUseFixedFont = not (hugoFont & PROP_FONT);
    this->fUseUnderlineFont = hugoFont & UNDERLINE_FONT;
    this->fUseItalicFont = hugoFont & ITALIC_FONT;
    this->fUseBoldFont = hugoFont & BOLD_FONT;

    QFont f(this->fUseFixedFont ? hApp->settings()->fixedFont : hApp->settings()->propFont);
    f.setUnderline(this->fUseUnderlineFont);
    f.setItalic(this->fUseItalicFont);
    f.setBold(this->fUseBoldFont);
    this->fFontMetrics = QFontMetrics(f, &this->fPixmap);

    // Adjust text caret for new font.
    this->setCursorHeight(this->fFontMetrics.height());
}


void
HFrame::printText( const QString& str, int x, int y )
{
    if (this->fPrintBuffer.isEmpty()) {
        this->fFlushXPos = x;
        this->fFlushYPos = y;
    }
    this->fPrintBuffer += str;
}


void
HFrame::printImage( const QImage& img, int x, int y )
{
    this->flushText();
    QPainter p(&this->fPixmap);
    p.drawImage(x, y, img);
}


void
HFrame::scrollUp( int left, int top, int right, int bottom, int h )
{
    if (h == 0) {
        return;
    }

    this->flushText();
    QRegion exposed;
    ++right;
    ++bottom;

#if QT_VERSION < 0x040600
    // Qt versions prior to 4.6 lack the QPixmap::scroll() routine. For
    // those versions we implement a (slower) fallback, where we simply
    // paint the contents from source to destination.
    QRect scrRect(left, top, right - left, bottom - top);
    QRect dest = scrRect & fPixmap.rect();
    QRect src = dest.translated(0, h) & dest;
    if (src.isEmpty()) {
        return;
    }

    //this->fPixmap.detach();
    QPixmap pix = this->fPixmap;
    QPainter p(&pix);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawPixmap(src.translated(0, -h), this->fPixmap, src);
    p.end();
    this->fPixmap = pix;

    exposed += dest;
    exposed -= src.translated(0, -h);
#else
    // Qt 4.6 and newer have scroll(), which is fast and nice.
    this->fPixmap.scroll(0, -h, left, top, right - left, bottom - top, &exposed);
#endif

    // Fill exposed region.
    const QRect& r = exposed.boundingRect();
    this->clearRegion(r.left(), r.top(), r.left() + r.width(), r.top() + r.bottom());
    if (hApp->settings()->softTextScrolling) {
        if (hApp->settings()->extraButter) {
            hugo_timewait(59);
        } else {
            hApp->advanceEventLoop();
        }
    }
}


void
HFrame::flushText()
{
    if (this->fPrintBuffer.isEmpty())
        return;

    QFont f(this->fUseFixedFont ? hApp->settings()->fixedFont : hApp->settings()->propFont);
    f.setUnderline(this->fUseUnderlineFont);
    f.setItalic(this->fUseItalicFont);
    f.setBold(this->fUseBoldFont);
    QFontMetrics m(f);
    QPainter p(&this->fPixmap);
    p.setFont(f);
    p.setPen(hugoColorToQt(this->fFgColor));
    p.setBackgroundMode(Qt::OpaqueMode);
    p.setBackground(QBrush(hugoColorToQt(this->fBgColor)));
    /*
    for (int i = 0; i < this->fPrintBuffer.length(); ++i) {
        p.drawText(this->fFlushXPos, this->fFlushYPos + m.ascent(), QString(this->fPrintBuffer.at(i)));
        this->fFlushXPos += m.width(this->fPrintBuffer.at(i));
    }
    */
    p.drawText(this->fFlushXPos, this->fFlushYPos + m.ascent() + 1, this->fPrintBuffer);
    //qDebug() << this->fPrintBuffer;
    this->fPrintBuffer.clear();
}


void
HFrame::updateCursorPos()
{
    // Reset the blink timer.
    if (this->fBlinkTimer->isActive()) {
        this->fBlinkTimer->start();
    }

    // Blink-out first to ensure the cursor won't stay visible at the previous
    // position after we move it.
    if (this->fBlinkVisible) {
        this->fBlinkCursor();
    }

    // Blink-in.
    if (not this->fBlinkVisible) {
        this->fBlinkCursor();
    }

    int xOffs = this->currentFontMetrics().width(this->fInputBuf.left(this->fInputCurrentChar));
    this->moveCursorPos(QPoint(this->fInputStartX + xOffs, this->fInputStartY));

    // Blink-in.
    if (not this->fBlinkVisible) {
        this->fBlinkCursor();
    }
}


void
HFrame::resetCursorBlinking()
{
    // Start the timer unless cursor blinking is disabled.
    if (QApplication::cursorFlashTime() > 1) {
        this->fBlinkTimer->start(QApplication::cursorFlashTime() / 2);
    }
}


void
HFrame::insertInputText( QString txt, bool execute, bool clearCurrent )
{
    if (this->fInputMode != NormalInput) {
        return;
    }
    // Clear the current input, if requested.
    if (clearCurrent) {
        this->fInputBuf.clear();
        this->fInputCurrentChar = 0;
    }
    // If the command is not to be executed, append a space so it won't run
    // together with what the user types next.
    if (not execute) {
        txt.append(' ');
    }
    this->fInputBuf.insert(this->fInputCurrentChar, txt);
    this->fInputCurrentChar += txt.length();
    this->update();
    this->updateCursorPos();
    if (execute) {
        fEndInputMode(false);
    }
}


QList<const QAction*>
HFrame::getGameContextMenuEntries( QMenu& dst )
{
    QList<const QAction*> actions;
    if (fInputMode != NormalInput) {
        return actions;
    }
    for (int i = 0; i < context_commands; ++i) {
        if (qstrcmp(context_command[i], "-") == 0) {
            dst.addSeparator();
        } else {
            actions.append(dst.addAction(context_command[i]));
        }
    }
    return actions;
}
