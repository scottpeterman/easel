#pragma once

class QToolBar;
class QWidget;

// Moves a row of controls (a widget with a QHBoxLayout) into a toolbar, as
// separate toolbar items rather than one.
//
// A toolbar holding one wide widget can only squeeze it when the window is
// narrower than the row, which cuts labels short, and its smallest width is
// the whole row's, which makes the window's smallest width that too. As
// separate items, the ones that don't fit go behind the toolbar's own ">>"
// button and the bar can be any width.
//
// A label stays with the control after it. Fixed spacing in the row is kept as
// space before the next item; stretches are dropped. The row widget itself is
// deleted: everything in it now belongs to the toolbar.
void spreadAcrossToolBar(QToolBar *bar, QWidget *row);
