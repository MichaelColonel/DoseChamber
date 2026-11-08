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

#include "TCanvasWidget.h"

#include <TCanvas.h>
#include <TROOT.h>
#include <TClass.h>
#include <TEnv.h>

#include <TWebCanvas.h>

#include <iostream>

QT_BEGIN_NAMESPACE

TCanvasWidget::TCanvasWidget(QWidget *parent)
  :
  QWidget(parent)
{
  setObjectName("TCanvasWidget");

  setSizeIncrement(QSize(100, 100));

  setUpdatesEnabled(true);
  setMouseTracking(true);

  setFocusPolicy(Qt::TabFocus);
  setCursor(Qt::CrossCursor);

  setAcceptDrops(true);

  static int wincnt = 1;

  rootCanvas = new TCanvas(kFALSE);
  rootCanvas->SetName(Form("Canvas%d", wincnt++));
  rootCanvas->SetTitle("Test Canvas");
  rootCanvas->ResetBit(TCanvas::kShowEditor);
  rootCanvas->SetCanvas(rootCanvas);
  rootCanvas->SetBatch(kTRUE); // mark canvas as batch

  TPad* pad = dynamic_cast< TPad* >(rootCanvas->GetPad(0));
  if (pad)
  {
    pad->Clear();
    pad->cd();
  }
  bool read_only = gEnv->GetValue("WebGui.FullCanvas", (Int_t) 1) == 0;
  TWebCanvas* web = new TWebCanvas(rootCanvas, "title", 0, 0, 800, 600, read_only);

  rootCanvas->SetCanvasImp(web);

  SetPrivateCanvasFields(true);

  web->SetCanCreateObjects(kFALSE); // not yet create objects on server side
  web->SetUpdatedHandler([this](){ emit CanvasUpdated(); });
  web->SetActivePadChangedHandler([this](TPad *pad){ emit SelectedPadChanged(pad); });
  web->SetPadClickedHandler([this](TPad *pad, int x, int y) { emit PadClicked(pad,x,y); });
  web->SetPadDblClickedHandler([this](TPad *pad, int x, int y) { emit PadDblClicked(pad,x,y); });
  auto where = ROOT::RWebDisplayArgs::GetQt5EmbedQualifier(this, "noopenui", QT_VERSION);

  web->ShowWebWindow(where);

  view = findChild< QWebEngineView* >("RootWebView");
  if (!view)
  {
    std::cerr << "FAIL TO FIND QWebEngineView - ROOT Qt5Web plugin does not work properly !!!!!" << std::endl;
    exit(11);
  }
  view->resize(width(), height());
  rootCanvas->SetCanvasSize(width(), height());
}

TCanvasWidget::~TCanvasWidget()
{
  if (rootCanvas)
  {
    SetPrivateCanvasFields(false);
    gROOT->GetListOfCanvases()->Remove(rootCanvas);

    rootCanvas->Close();
    delete rootCanvas;
    rootCanvas = nullptr;
  }
}

void TCanvasWidget::SetPrivateCanvasFields(bool on_init)
{
  Long_t offset = TCanvas::Class()->GetDataMemberOffset("fCanvasID");
  if (offset > 0)
  {
    Int_t *id = (Int_t *)((char*) rootCanvas + offset);
    if (*id == rootCanvas->GetCanvasID())
    {
      *id = on_init ? 111222333 : -1;
    }
  }
  else
  {
    std::cerr << "ERROR: Cannot modify fCanvasID data member" << std::endl;
  }

  offset = TCanvas::Class()->GetDataMemberOffset("fMother");
  if (offset > 0)
  {
    TPad **moth = (TPad **)((char*) rootCanvas + offset);
    if (*moth == rootCanvas->GetMother())
    {
      *moth = on_init ? rootCanvas : nullptr;
    }
   }
  else
  {
    std::cerr << "ERROR: Cannot set fMother data member in canvas" << std::endl;
  }
}

void TCanvasWidget::resizeEvent(QResizeEvent *event)
{
  Q_UNUSED(event);
  view->resize(width(), height());
  rootCanvas->SetCanvasSize(width(), height());
}

void TCanvasWidget::paintEvent(QPaintEvent* event)
{
  Q_UNUSED(event);
}

void TCanvasWidget::activateEditor(TPad *pad, TObject *obj)
{
  TWebCanvas *cimp = dynamic_cast< TWebCanvas* >(rootCanvas->GetCanvasImp());
  if (cimp)
  {
    cimp->ShowEditor(kTRUE);
    cimp->ActivateInEditor(pad, obj);
  }
}

void TCanvasWidget::setEditorVisible(bool flag)
{
  TCanvasImp *cimp = rootCanvas->GetCanvasImp();
  if (cimp)
  {
    cimp->ShowEditor(flag);
  }
}

void TCanvasWidget::activateStatusLine()
{
  TCanvasImp *cimp = rootCanvas->GetCanvasImp();
  if (cimp)
  {
    cimp->ShowStatusBar(kFALSE);
  }
}

TPad* TCanvasWidget::getPad(int subPadNumber) const
{
  return (rootCanvas) ? dynamic_cast< TPad* >(rootCanvas->GetPad(subPadNumber)) : nullptr;
}

QT_END_NAMESPACE
