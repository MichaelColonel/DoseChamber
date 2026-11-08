// Initial qtweb author: Sergey Linev, GSI  13/01/2021

/*************************************************************************
 * Copyright (C) 1995-2021, Rene Brun and Fons Rademakers.               *
 * All rights reserved.                                                  *
 *                                                                       *
 * For the licensing terms see $ROOTSYS/LICENSE.                         *
 * For the list of contributors see $ROOTSYS/README/CREDITS.             *
 *************************************************************************/

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 *
 */

#pragma once

#include <QWidget>
#include <QWebEngineView>

QT_BEGIN_NAMESPACE

class TCanvas;
class TPad;
class TObject;

class TCanvasWidget : public QWidget {
  Q_OBJECT

public:
  TCanvasWidget(QWidget *parent = nullptr);
  ~TCanvasWidget() override;

  /// returns canvas shown in the widget
  TCanvas* getCanvas() const { return rootCanvas; }
  TPad* getPad(int subPadNumber = 0) const;

signals:
  void CanvasUpdated();
  void SelectedPadChanged(TPad*);
  void PadClicked(TPad*,int,int);
  void PadDblClicked(TPad*,int,int);

public slots:
  void activateEditor(TPad *pad = nullptr, TObject *obj = nullptr);
  void activateStatusLine();
  void setEditorVisible(bool flag = true);

protected:
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent* e) override;
  void SetPrivateCanvasFields(bool on_init);
  QWebEngineView* view{nullptr};  ///< qt webwidget to show
  TCanvas* rootCanvas{nullptr};
};

QT_END_NAMESPACE
