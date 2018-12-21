/* This file is part of the KDE libraries
    Copyright (C) 1997 Martin Jones (mjones@kde.org)

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/
#pragma once
#include <QPushButton>

class KColorButtonPrivate;
/**
 * @short A pushbutton to display or allow user selection of a color.
 *
 * This widget can be used to display or allow user selection of a color.
 *
 * @see KColorDialog
 *
 * \image html kcolorbutton.png "KDE Color Button"
 */
class KColorButton final: public QPushButton
{
    Q_OBJECT
    Q_PROPERTY(QColor color READ color WRITE setColor USER true)
    Q_PROPERTY(QColor defaultColor READ defaultColor WRITE setDefaultColor)

public:
    /**
     * Creates a color button.
     */
    explicit KColorButton(QWidget* parent = nullptr);

    /**
     * Creates a color button with an initial color @p c.
     */
    explicit KColorButton(const QColor& c, QWidget* parent = nullptr);

    /**
     * Creates a color button with an initial color @p c and default color @p defaultColor.
     */
    KColorButton(const QColor& c, const QColor& defaultColor, QWidget* parent = nullptr);

    ~KColorButton() override;

    /**
     * Returns the currently chosen color.
     */
    QColor color() const;

    /**
     * Sets the current color to @p c.
     */
    void setColor(const QColor& c);

    /**
     * Returns the default color or an invalid color
     * if no default color is set.
     */
    QColor defaultColor() const;

    /**
     * Sets the default color to @p c.
     */
    void setDefaultColor(const QColor& c);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    /**
     * Emitted when the color of the widget
     * is changed, either with setColor() or via user selection.
     */
    void changed(const QColor& newColor);

protected:
    void paintEvent(QPaintEvent* pe) override;
    void dragEnterEvent(QDragEnterEvent* /*event*/) override;
    void dropEvent(QDropEvent* /*event*/) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    class KColorButtonPrivate final
    {
    public:
        KColorButtonPrivate(KColorButton* q)
            : q(q)
        {}

        void _k_chooseColor();

        KColorButton* q;
        QColor m_defaultColor;
        bool m_bdefaultColor : 1;

        bool dragFlag : 1;
        QColor col;
        QPoint mPos;

        void initStyleOption(QStyleOptionButton* opt) const;
    };

    KColorButtonPrivate* const d;

    Q_PRIVATE_SLOT(d, void _k_chooseColor())
};
