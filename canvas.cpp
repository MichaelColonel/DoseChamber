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

#include <QPaintEvent>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QDebug>

#include <TCanvas.h>
#include <TVirtualX.h>
#include <TSystem.h>

#include "canvas.h"

QRootCanvas::QRootCanvas(QWidget* parent)
  :
  QWidget(parent),
  canvas(nullptr)
{
  // QRootCanvas constructor.

  // set options needed to properly update the canvas when resizing the widget
  // and to properly handle context menus and mouse move events
  setAttribute( Qt::WA_PaintOnScreen, true);
  setAttribute( Qt::WA_OpaquePaintEvent, true);
  setMinimumSize( 300, 200);
  setUpdatesEnabled(kFALSE);
  setMouseTracking(kTRUE);

  // register the QWidget in TVirtualX, giving its native window id
  int wid = gVirtualX->AddWindow( winId(), 600, 400);
  // create the ROOT TCanvas, giving as argument the QWidget registered id
  canvas = new TCanvas( "RootCanvas", width(), height(), wid);
}

QRootCanvas::~QRootCanvas()
{
//    qDebug() << "GUI: RootCanvas Destructor";
  delete canvas;
}

void
QRootCanvas::mouseMoveEvent(QMouseEvent *event)
{
  // Handle mouse move events.
  if (event->buttons() & Qt::LeftButton)
  {
    canvas->HandleInput( kButton1Motion, event->x(), event->y());
  }
  else if (event->buttons() & Qt::MidButton)
  {
    canvas->HandleInput( kButton2Motion, event->x(), event->y());
  }
  else if (event->buttons() & Qt::RightButton)
  {
    canvas->HandleInput( kButton3Motion, event->x(), event->y());
  }
  else
  {
    canvas->HandleInput( kMouseMotion, event->x(), event->y());
  }
}

void
QRootCanvas::mousePressEvent(QMouseEvent *event)
{
  // Handle mouse button press events.
  switch (event->button())
  {
  case Qt::LeftButton:
    canvas->HandleInput( kButton1Down, event->x(), event->y());
    break;
  case Qt::MidButton:
    canvas->HandleInput( kButton2Down, event->x(), event->y());
    break;
  case Qt::RightButton:
    // does not work properly on Linux...
    // ...adding setAttribute(Qt::WA_PaintOnScreen, true)
    // seems to cure the problem
    canvas->HandleInput( kButton3Down, event->x(), event->y());
    break;
  default:
    break;
  }
}

void
QRootCanvas::mouseReleaseEvent(QMouseEvent *event)
{
  // Handle mouse button release events.
  switch (event->button())
  {
  case Qt::LeftButton:
    canvas->HandleInput( kButton1Up, event->x(), event->y());
    break;
  case Qt::MidButton:
    canvas->HandleInput( kButton2Up, event->x(), event->y());
    break;
  case Qt::RightButton:
    // does not work properly on Linux...
    // ...adding setAttribute(Qt::WA_PaintOnScreen, true)
    // seems to cure the problem
    canvas->HandleInput( kButton3Up, event->x(), event->y());
    break;
  default:
    break;
  }
}

void
QRootCanvas::resizeEvent(QResizeEvent*)
{
  // Handle resize events.
  canvas->Resize();
  canvas->Update();
}

void
QRootCanvas::paintEvent(QPaintEvent*)
{
  // Handle paint events.
  canvas->Resize();
  canvas->Update();
}
