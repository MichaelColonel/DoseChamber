/*
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#pragma once

#include <QWidget>

class QPaintEvent;
class QResizeEvent;
class QMouseEvent;

class TCanvas;

class QRootCanvas : public QWidget {
  Q_OBJECT

public:
  QRootCanvas(QWidget* parent = 0);
  virtual ~QRootCanvas();
  TCanvas* getCanvas() { return canvas; }

protected:
  virtual void mouseMoveEvent(QMouseEvent* e);
  virtual void mousePressEvent(QMouseEvent* e);
  virtual void mouseReleaseEvent(QMouseEvent* e);
  virtual void paintEvent(QPaintEvent* e);
  virtual void resizeEvent(QResizeEvent* e);

  TCanvas* canvas;
};
