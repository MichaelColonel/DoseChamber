#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QDebug>
#include <QTimer>
#include <QMessageBox>
#include <QtGlobal>
#include <QFileDialog>
#include <QSettings>
#include <QProgressDialog>
#include <QHBoxLayout>

#include <TFile.h>
#include <TTree.h>
#include <TSystem.h>
#include <TCanvas.h>
#include <TGraph.h>
#include <TGraphErrors.h>
#include <TMultiGraph.h>
#include <TPad.h>
#include <TH1F.h>
#include <TH2F.h>
#include <TLine.h>
#include <TMultiGraph.h>
#include <TServerSocket.h>
#include <TMessage.h>
#include <TGaxis.h>

#include <iomanip>
#include <fstream>
#include <bitset>
#include <cstring>
#include <algorithm>
#include <numeric>

namespace
{

Double_t linnn(Double_t *x, Double_t* par)
{
  return (par[0] + x[0] * par[1]);
}

constexpr double ADC_COUNT_TIME_MS = 1582.01 / 100000.;

std::map< int, std::pair< int, int > > xrayPedSignalRange;

}

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , timer(new QTimer(this))
  , initiationTimer(new QTimer(this))
  , initiationProgress(new QProgressDialog(this))
{
  ui->setupUi(this);

  xrayPedSignalRange[0] = std::make_pair(0, 110);
  xrayPedSignalRange[160] = std::make_pair(120, 230);
  xrayPedSignalRange[224] = std::make_pair(110, 167);
  xrayPedSignalRange[320] = std::make_pair(110, 238);
  xrayPedSignalRange[384] = std::make_pair(110, 287);
  xrayPedSignalRange[544] = std::make_pair(110, 410);
//  xrayPedSignalRange[640] = std::make_pair(110, 480);
  xrayPedSignalRange[640] = std::make_pair(110, 335);
  xrayPedSignalRange[800] = std::make_pair(110, 600);
  xrayPedSignalRange[960] = std::make_pair(110, 720);
  xrayPedSignalRange[1120] = std::make_pair(110, 840);
  xrayPedSignalRange[1280] = std::make_pair(110, 960);
  xrayPedSignalRange[1440] = std::make_pair(110, 1080);
  xrayPedSignalRange[1600] = std::make_pair(110, 1200);
  xrayPedSignalRange[1760] = std::make_pair(110, 1320);
  xrayPedSignalRange[1920] = std::make_pair(110, 1440);
  xrayPedSignalRange[2080] = std::make_pair(110, 1560);

  this->initiationProgress->setWindowTitle(tr("Dose chamber"));

  this->adcCanvas = this->ui->RootCanvas_AdcGraph->getCanvas();
  this->adcCanvas->cd();
  this->adcPad = new TPad("adcPad", "Grid", 0., 0., 1., 1.);
  this->adcPad->Draw();
  this->adcPad->SetGrid();
  this->adcPad->cd();
  this->adcGraph = new TGraph(100000);
  this->adcGraph->SetTitle("ADC data;Samples;ADC count");
  this->adcGraph->Draw("AL*");

  this->pedCanvas = this->ui->RootCanvas_Ped->getCanvas();
  this->pedCanvas->cd();
  this->pedPad = new TPad("pedPad", "Grid", 0., 0., 1., 1.);
  this->pedPad->Draw();
  this->pedPad->SetGrid();
  this->pedPad->cd();
  this->pedGraph = new TGraph(100000);
  this->pedGraph->SetTitle("ADC data;Time (ms);ADC count");
  this->pedGraph->Draw("AL*");

  this->signalCanvas = this->ui->RootCanvas_Signal->getCanvas();
  this->signalCanvas->cd();
  this->signalPad = new TPad("signalPad", "Grid", 0., 0., 1., 1.);
  this->signalPad->Draw();
  this->signalPad->SetGrid();
  this->signalPad->cd();
  this->signalGraph = new TGraph(100000);
  this->signalGraph->SetTitle("ADC data;Time (ms);ADC count");
  this->signalGraph->Draw("AL*");

  connect( ui->PushButton_AcquisitionConnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceConnectClicked()));
  connect( ui->PushButton_AcquisitionDisconnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceDisconnectClicked()));
  connect( ui->PushButton_EnableTimer, SIGNAL(clicked()), this, SLOT(onEnableTimerClicked()));
  connect( ui->PushButton_DisableTimer, SIGNAL(clicked()), this, SLOT(onDisableTimerClicked()));
  connect( ui->PushButton_EnableExtStart, SIGNAL(clicked()), this, SLOT(onEnableExtStartClicked()));
  connect( ui->PushButton_DisableExtStart, SIGNAL(clicked()), this, SLOT(onDisableExtStartClicked()));
  connect( ui->PushButton_ExternalStart, SIGNAL(clicked()), this, SLOT(onExternalStartClicked()));
//  connect( ui->pushButton_LowK1, SIGNAL(clicked()), this, SLOT(onLowK1Clicked()));
//  connect( ui->pushButton_HighK1, SIGNAL(clicked()), this, SLOT(onHighK1Clicked()));
//  connect( ui->pushButton_LowK2, SIGNAL(clicked()), this, SLOT(onLowK2Clicked()));
//  connect( ui->pushButton_HighK2, SIGNAL(clicked()), this, SLOT(onHighK2Clicked()));
  connect( ui->PushButton_Acquisition, SIGNAL(clicked()), this, SLOT(onAcquisitionClicked()));
  connect( ui->PushButton_ExternalCounter, SIGNAL(clicked()), this, SLOT(onExternalCounterClicked()));
  connect( ui->PushButton_Test, SIGNAL(clicked()), this, SLOT(onTestClicked()));
//  connect( ui->CheckBox_ExternalStart, SIGNAL(clicked(bool)), this, SLOT(onExternalStartChecked(bool)));
//  connect( ui->CheckBox_InternalTimer, SIGNAL(clicked(bool)), this, SLOT(onInternalTimerChecked(bool)));

  connect( ui->PushButton_PE0, SIGNAL(clicked()), this, SLOT(onPE0Clicked()));
  connect( ui->PushButton_PE1, SIGNAL(clicked()), this, SLOT(onPE1Clicked()));
  connect( ui->PushButton_PB1, SIGNAL(clicked()), this, SLOT(onPB1Clicked()));

  connect( ui->PushButton_SetExternalStart, SIGNAL(clicked()), this, SLOT(onSetExternalStartClicked()));
  connect( ui->PushButton_SetInternalTimer, SIGNAL(clicked()), this, SLOT(onSetInternalTimerClicked()));

  connect( ui->PushButton_e, SIGNAL(clicked()), this, SLOT(oneClicked()));

  connect( ui->PushButton_Type1, SIGNAL(clicked()), this, SLOT(onType1Clicked()));
  connect( ui->PushButton_Type2, SIGNAL(clicked()), this, SLOT(onType2Clicked()));
  connect( ui->PushButton_Type3, SIGNAL(clicked()), this, SLOT(onType3Clicked()));
  connect( ui->PushButton_Type4, SIGNAL(clicked()), this, SLOT(onType4Clicked()));
  connect( ui->PushButton_PT, SIGNAL(clicked()), this, SLOT(onTemperaturePressureClicked()));
  connect( ui->PushButton_I2CStuckReset, SIGNAL(clicked()), this, SLOT(onStuckResetClicked()));
  connect( ui->PushButton_XrayExpoTimer, SIGNAL(clicked()), this, SLOT(onXrayTimerClicked()));
  connect( ui->PushButton_AcquisitionXray, SIGNAL(clicked()), this, SLOT(onXrayAcquisitionClicked()));

  connect( ui->PushButton_OpenFile, SIGNAL(clicked()), this, SLOT(onOpenRootFileClicked()));
  connect( ui->ListWidget_Spills, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(onCurrentSpillItemChanged(QListWidgetItem*,QListWidgetItem*)));

  connect( ui->ButtonGroup_K1, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK1Changed(QAbstractButton*)));
  connect( ui->ButtonGroup_K2, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK2Changed(QAbstractButton*)));
  connect( timer, SIGNAL(timeout()), this, SLOT(onRootEventsTimeout())); // iterate ROOT events

  // fit
  connect( ui->PushButton_FitPed, SIGNAL(clicked()), this, SLOT(onFitPedClicked()));
  connect( ui->PushButton_FitSignal, SIGNAL(clicked()), this, SLOT(onFitSignalClicked()));

#ifndef DEBUG
  ui->pushButton_LowK1->hide();
  ui->pushButton_LowK2->hide();
  ui->pushButton_HighK1->hide();
  ui->pushButton_HighK2->hide();
  ui->PushButton_DisableTimer->hide();
  ui->PushButton_EnableTimer->hide();
  ui->PushButton_EnableExtStart->hide();
  ui->PushButton_DisableExtStart->hide();
  ui->PushButton_e->hide();
  ui->PushButton_I2CStuckReset->hide();
#endif

  connect( initiationTimer, SIGNAL(timeout()), this, SLOT(onFirstConnectResponce())); // first responce timeout
  this->initiationTimer->setInterval(1000);

  this->timer->start(20);
  loadSettings();
}

MainWindow::~MainWindow()
{
  timer->stop();
  saveSettings();

  if (this->rootFile)
  {
    this->rootFile->Close();
    delete this->rootFile;
    this->rootFile = nullptr;
  }

  delete this->adcGraph;
  this->adcGraph = nullptr;
  delete this->adcPad;
  this->adcPad = nullptr;

  delete this->pedGraph;
  this->pedGraph = nullptr;
  delete this->pedPad;
  this->pedPad = nullptr;

  delete this->signalGraph;
  this->signalGraph = nullptr;
  delete this->signalPad;
  this->signalPad = nullptr;

  delete ui;
}

/**
 * @brief MainWindow::onRootEventsTimeout
 * Iterate ROOT events
 */
void MainWindow::onRootEventsTimeout()
{
  // call the inner loop of ROOT
  gSystem->ProcessEvents();
}

void MainWindow::onAcquisitionDeviceConnectClicked()
{
  QString name = ui->LineEdit_AcquisitionDeviceName->text();
  this->acquisitionPort = new QSerialPort(name, this);

  bool enabled = false;
  if (this->acquisitionPort && this->acquisitionPort->open(QIODevice::ReadWrite))
  {
    this->acquisitionPort->setBaudRate(QSerialPort::Baud57600);
    this->acquisitionPort->setDataBits(QSerialPort::Data8);
    this->acquisitionPort->setParity(QSerialPort::NoParity);
    this->acquisitionPort->setStopBits(QSerialPort::OneStop);
    this->acquisitionPort->setFlowControl(QSerialPort::NoFlowControl);

    connect(this->acquisitionPort, SIGNAL(readyRead()), this, SLOT(acquisitionSerialPortDataReady()));
    connect(this->acquisitionPort, SIGNAL(bytesWritten(qint64)), this, SLOT(acquisitionSerialPortBytesWritten(qint64)));
    connect(this->acquisitionPort, SIGNAL(error(QSerialPort::SerialPortError)), this, SLOT(acquisitionSerialPortError(QSerialPort::SerialPortError)));

    connect( this->initiationProgress, SIGNAL(finished(int)), this, SLOT(onInitiateDeviceFinished(int)));
    connect( this->initiationProgress, SIGNAL(canceled()), this, SLOT(onInitiateDeviceStopClicked()));

    enabled = true;
    ui->statusbar->showMessage(QObject::tr("Acquisition device connected"), 1000);
    this->acquisitionPort->flush();
    this->acquisitionPortData.clear();

    this->initiationProgress->setLabelText("Reset ATmega64L input command buffer...");
    this->initiationProgress->setRange(0, 0);
    this->initiationProgress->setValue(0);
    this->initiationProgress->show();
    this->initiationTimer->start();

    QDateTime dateTime = QDateTime::currentDateTime();
    std::stringstream sstream;
    sstream << "RUN_" << dateTime.toString("ddMMyyyy_hhmmss").toStdString() << ".root";
    std::string name = sstream.str();
    if (this->rootFile)
    {
      this->rootFile->Close();
      delete this->rootFile;
      this->rootFile = nullptr;
    }
    this->rootFile = new TFile(name.c_str(), "RECREATE");

  }
  else if (this->acquisitionPort && !this->acquisitionPort->isOpen())
  {
    QMessageBox msgBox(this);
    msgBox.setModal(true);
    msgBox.setWindowTitle(tr("IC control"));
    msgBox.setText(tr("Unable to connect device %1.\n%2.").arg(this->acquisitionPort->portName()).arg(this->acquisitionPort->errorString()));
    msgBox.exec();
    ui->statusbar->showMessage(QObject::tr("Unable to connect device"), 1000);

    delete this->acquisitionPort;
    this->acquisitionPort = nullptr;
  }
  ui->PushButton_AcquisitionConnect->setEnabled(!enabled);
  ui->PushButton_AcquisitionDisconnect->setEnabled(enabled);
  ui->PushButton_EnableTimer->setEnabled(enabled);
  ui->PushButton_DisableTimer->setEnabled(enabled);
  ui->PushButton_EnableExtStart->setEnabled(enabled);
  ui->PushButton_DisableExtStart->setEnabled(enabled);
  ui->PushButton_ExternalStart->setEnabled(enabled);
  ui->PushButton_Acquisition->setEnabled(enabled);
  ui->PushButton_ExternalCounter->setEnabled(enabled);
  ui->PushButton_SetExternalStart->setEnabled(enabled);
  ui->PushButton_SetInternalTimer->setEnabled(enabled);

  ui->PushButton_Test->setEnabled(enabled);
  ui->RadioButton_HighK1->setEnabled(enabled);
  ui->RadioButton_HighK2->setEnabled(enabled);
  ui->RadioButton_LowK1->setEnabled(enabled);
  ui->RadioButton_LowK2->setEnabled(enabled);
}

void MainWindow::onAcquisitionDeviceDisconnectClicked()
{
  constexpr bool enabled = false;
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    this->acquisitionPort->flush();
    this->acquisitionPort->close();
    delete this->acquisitionPort;
    this->acquisitionPort = nullptr;
  }
  else if (this->acquisitionPort && !this->acquisitionPort->isOpen())
  {
    delete this->acquisitionPort;
    this->acquisitionPort = nullptr;
  }

  if (this->rootFile)
  {
    this->rootFile->Close();
    delete this->rootFile;
    this->rootFile = nullptr;
  }

  this->acquisitionPortData.clear();
  this->lastCommandData.clear();
  ui->PushButton_AcquisitionConnect->setEnabled(!enabled);
  ui->PushButton_AcquisitionDisconnect->setEnabled(enabled);
  ui->PushButton_EnableTimer->setEnabled(enabled);
  ui->PushButton_DisableTimer->setEnabled(enabled);
  ui->PushButton_EnableExtStart->setEnabled(enabled);
  ui->PushButton_DisableExtStart->setEnabled(enabled);
  ui->PushButton_ExternalStart->setEnabled(enabled);
  ui->PushButton_Test->setEnabled(enabled);
  ui->PushButton_Acquisition->setEnabled(enabled);
  ui->PushButton_ExternalCounter->setEnabled(enabled);
  ui->PushButton_SetExternalStart->setEnabled(enabled);
  ui->PushButton_SetInternalTimer->setEnabled(enabled);

  ui->RadioButton_HighK1->setEnabled(enabled);
  ui->RadioButton_HighK2->setEnabled(enabled);
  ui->RadioButton_LowK1->setEnabled(enabled);
  ui->RadioButton_LowK2->setEnabled(enabled);

  ui->statusbar->showMessage(QObject::tr("Acquisition device disconnected"), 1000);
}

void MainWindow::acquisitionSerialPortBytesWritten(qint64 bytes)
{
  qDebug() << Q_FUNC_INFO << "Serial port data written bytes: " << bytes;
}

void MainWindow::acquisitionSerialPortDataReady()
{
  QByteArray portData = this->acquisitionPort->readAll();

  this->acquisitionPortData.push_back(portData);
  constexpr int dataBegin = std::strlen("Start");
  if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.size() == dataBegin)
  {
    qDebug() << Q_FUNC_INFO << "Slow extraction begins";
    return;
  }
  else if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.endsWith("Finish"))
  {
    timeWaitingEnd = std::chrono::steady_clock::now();
    std::chrono::duration< float, std::milli > spillDuration(timeWaitingEnd - timeWaitingBegin);
    ui->statusbar->showMessage(QObject::tr("%1 ms").arg(spillDuration.count()), 3000);

    qDebug() << Q_FUNC_INFO << "Spill time (ms): " << spillDuration.count();

    const QByteArray& data = this->acquisitionPortData;
    int dataEnd = data.indexOf("Finish");
    std::vector< int > adcData;
    constexpr int ADC_HIGH_BITS = 6;
    constexpr int ADC_LOW_BITS = CHAR_BIT;
    constexpr int ADC_BITS = ADC_LOW_BITS + ADC_HIGH_BITS;
///    std::ofstream file("adcData.txt");
    for (int i = dataBegin; i < dataEnd; i += 2)
    {
      std::bitset< ADC_HIGH_BITS > high(data[i + 1] & 0x3F);
      std::bitset< ADC_LOW_BITS > low(data[i]);
      std::bitset< ADC_BITS > count((high.to_ulong() << ADC_LOW_BITS) | low.to_ulong());
      adcData.push_back(static_cast< int >(count.to_ulong()));
///      file << high.to_string() << ' ' << low.to_string() << ' ' << count.to_string() << '\n';
    }
///    file.close();
    qDebug() << Q_FUNC_INFO << "Slow extraction ends, data size: " << adcData.size() << " Time for ADC count: "
             << adcData.size() / spillDuration.count() << " ms^-1";
    this->adcGraph->Set(static_cast< Int_t >(adcData.size()));
    int i = 0;
    for (int v : adcData)
    {
      this->adcGraph->SetPoint( i, Double_t(i), Double_t(v));
      i++;
    }
    this->adcPad->Modified();
    this->adcPad->Update();
    if (adcData.size())
    {
      int v0 = adcData[0];
      int v1 = adcData[i - 1];
      this->ui->Label_AcquisitionData->setText(tr("Ped: %1, Last: %2").arg(v0).arg(v1));
    }
    else
    {
      this->ui->Label_AcquisitionData->setText("");
    }
    int pos = this->acquisitionPortData.indexOf("Finish");
    constexpr int finishLen = std::strlen("Finish");
    this->acquisitionPortData.remove(0, pos + finishLen);
//    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 6 && this->acquisitionPortData.startsWith('C') && this->acquisitionPortData.endsWith("OK"))
  {
    const char* data = this->acquisitionPortData.data();
    std::bitset< CHAR_BIT > tcnt1l(data[1]);
    std::bitset< CHAR_BIT > tcnt1h(data[2]);
    std::bitset< CHAR_BIT > ovf(data[3]);
    int count = int((tcnt1h.to_ulong() << 8) | tcnt1l.to_ulong()); // TCNT1
    count += int(ovf.to_ulong() * USHRT_MAX); // overflows
    ui->Label_TemperaturePressureExtCount->setText(tr("External counter: %1").arg(count));
    qDebug() << Q_FUNC_INFO << "T1 counter: " << count;
    ui->statusbar->showMessage(QObject::tr("T1 external counter: %1").arg(count), 3000);
    int pos = this->acquisitionPortData.indexOf("OK");
    constexpr int finishOK = std::strlen("OK");
    this->acquisitionPortData.remove(0, pos + finishOK);

    this->externalCounter = count;
    this->updateTemperaturePressureCounter();

//    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 3 && (this->acquisitionPortData.startsWith('1') || this->acquisitionPortData.startsWith('2')) &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    qDebug() << Q_FUNC_INFO << "Type-1 or Type-2 response:" << this->acquisitionPortData;
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 4 && this->acquisitionPortData.startsWith('3') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    const char* data = this->acquisitionPortData.data();
    qDebug() << Q_FUNC_INFO << "Type-3 response:" << int(data[1]) << " " << char(data[2]) << char(data[3]);
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 7 && this->acquisitionPortData.startsWith('4') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    const char* data = this->acquisitionPortData.data();
    qDebug() << Q_FUNC_INFO << "Type-4 3-bytes response:" << int(data[1] - '0') \
             << ", data:" << quint8(data[2]) << ' ' << quint8(data[3]) << ' ' << quint8(data[4]) \
             << ' ' << char(data[5]) << char(data[6]);
    std::bitset< 20 > adcV((quint8(data[2]) << (2 * CHAR_BIT)) | (quint8(data[3]) << CHAR_BIT) | quint8(data[4]));
    double value = 0.;
    if (adcV.test(19))
    {
      adcV.flip();
      value = static_cast< double >(adcV.to_ulong() + 1) / 100.;
      value *= -1.;
    }
    else
    {
      value = static_cast< double >(adcV.to_ulong()) / 100.;
    }
    ui->statusbar->showMessage(QObject::tr("Value: %1").arg(value), 1000);
    qDebug() << Q_FUNC_INFO << "Value:" << value;
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 10 && this->acquisitionPortData.startsWith('4') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    const char* data = this->acquisitionPortData.data();
    qDebug() << Q_FUNC_INFO << "Type-4 6-bytes response:" << int(data[1] - '0') \
             << ", data:" << quint8(data[2]) << ' ' << quint8(data[3]) << ' ' << quint8(data[4]) \
             << ' ' << quint8(data[5]) << ' ' << quint8(data[6]) << ' ' << quint8(data[7]) << ' ' << char(data[8]) << char(data[9]);
    std::bitset< 20 > adcT((quint8(data[2]) << (2 * CHAR_BIT)) | (quint8(data[3]) << CHAR_BIT) | quint8(data[4]));
    std::bitset< 20 > adcP((quint8(data[5]) << (2 * CHAR_BIT)) | (quint8(data[6]) << CHAR_BIT) | quint8(data[7]));
    double temperature = 0.;
    double pressure = 0.;
    if (adcT.test(19))
    {
      adcT.flip();
      temperature = -1. * static_cast< double >(adcT.to_ulong() + 1) / 100.;
    }
    else
    {
      temperature = static_cast< double >(adcT.to_ulong()) / 100.;
    }
    if (adcP.test(19))
    {
      adcP.flip();
      pressure = -1. * static_cast< double >(adcP.to_ulong() + 1) / 100.;
    }
    else
    {
      pressure = static_cast< double >(adcP.to_ulong()) / 100.;
    }
    ui->statusbar->showMessage(QObject::tr("Temperature (deg C): %1. Pressure (mbar): %2").arg(temperature).arg(pressure), 1000);
    qDebug() << Q_FUNC_INFO << "Temperature (deg C):" << temperature << ". Pressure (mbar):" << pressure;

    this->sensorTemperature = temperature;
    this->sensorPressure = pressure;
    this->updateTemperaturePressureCounter();

    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 3 && !this->acquisitionPortData.startsWith('5') &&
           !this->acquisitionPortData.startsWith('6') && !this->acquisitionPortData.startsWith('7') &&
           (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    qDebug() << Q_FUNC_INFO << this->acquisitionPortData;
    if (this->acquisitionPortData.endsWith("OK"))
    {
      ui->statusbar->showMessage(QObject::tr("Correct response: %1").arg(char(this->acquisitionPortData[0])), 3000);
    }
    else if (this->acquisitionPortData.endsWith("NO"))
    {
      ui->statusbar->showMessage(QObject::tr("Wrong response: %1").arg(char(this->acquisitionPortData[0])), 3000);
    }
    if (this->initiationProgress->isVisible())
    {
      this->initiationProgress->hide();
      this->initiationProgress->close();
    }
    this->initiationTimer->stop();
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 5 && this->acquisitionPortData.startsWith('q') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    const char* data = this->acquisitionPortData.data();
    std::bitset< CHAR_BIT > adch(data[1]);
    std::bitset< CHAR_BIT > adcl(data[2]);
    double temperature = 0.;
    if (adch.test(7))
    {
      adch.flip();
      temperature = -1. * static_cast< double >(adch.to_ulong() + 1);
    }
    else
    {
      temperature = static_cast< double >(adch.to_ulong());
    }
    if (adcl.test(7))
    {
      temperature += 0.5;
    }
    qDebug() << Q_FUNC_INFO << "Temperature data:" << temperature << ' ' << char(data[3]) << char(data[4]);
    ui->statusbar->showMessage(QObject::tr("Temperature (deg C): %1").arg(temperature), 3000);

    this->sensorTemperature = temperature;
    this->updateTemperaturePressureCounter();

    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 9 && this->acquisitionPortData.startsWith('P') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    const char* data = this->acquisitionPortData.data();
    qDebug() << Q_FUNC_INFO << "Temperature and pressure data:" \
             << quint8(data[1]) << ' ' << quint8(data[2]) << ' ' << quint8(data[3]) << ' ' << quint8(data[4]) \
             << ' ' << quint8(data[5]) << ' ' << quint8(data[6]) << ' ' << char(data[7]) << ' ' << char(data[8]);
    std::bitset< 20 > adcT((quint8(data[1]) << (2 * CHAR_BIT)) | (quint8(data[2]) << CHAR_BIT) | quint8(data[3]));
    std::bitset< 20 > adcP((quint8(data[4]) << (2 * CHAR_BIT)) | (quint8(data[5]) << CHAR_BIT) | quint8(data[6]));
    double temperature = 0.;
    double pressure = 0.;
    if (adcT.test(19))
    {
      adcT.flip();
      temperature = -1. * static_cast< double >(adcT.to_ulong() + 1) / 100.;
    }
    else
    {
      temperature = static_cast< double >(adcT.to_ulong()) / 100.;
    }
    if (adcP.test(19))
    {
      adcP.flip();
      pressure = -1. * static_cast< double >(adcP.to_ulong() + 1) / 100.;
    }
    else
    {
      pressure = static_cast< double >(adcP.to_ulong()) / 100.;
    }
    ui->statusbar->showMessage(QObject::tr("Temperature (deg C): %1. Pressure (mbar): %2").arg(temperature).arg(pressure), 1000);
    ui->Label_TemperatureAndPressure->setText(QObject::tr("Temperature (deg C): %1. Pressure (mbar): %2").arg(temperature).arg(pressure));
    qDebug() << Q_FUNC_INFO << "Temperature (deg C):" << temperature << ". Pressure (mbar):" << pressure;

    this->sensorTemperature = temperature;
    this->sensorPressure = pressure;
    this->updateTemperaturePressureCounter();
    this->saveAcquisitionData(this->adcGraph, this->xrayExpoTimer, temperature, pressure, this->externalCounter);
    this->acquisitionPortData.clear();
    QTimer::singleShot(10, this, SLOT(onPB1Clicked()));
    return;
  }
  else if (this->acquisitionPortData == "5OK")
  {
    qDebug() << Q_FUNC_INFO << "Ready: " << this->acquisitionPortData;
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData == "7OK")
  {
    qDebug() << Q_FUNC_INFO << "End: " << this->acquisitionPortData;
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData == "6OK")
  {
    qDebug() << Q_FUNC_INFO << "Go: " << this->acquisitionPortData;
    this->acquisitionPortData.clear();
    QTimer::singleShot(10, this, SLOT(onExternalStartClicked()));
    return;
  }
}

void MainWindow::acquisitionSerialPortError(QSerialPort::SerialPortError)
{
}

void MainWindow::onExternalStartClicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { 'a' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      timeWaitingBegin = std::chrono::steady_clock::now();
      qDebug() << Q_FUNC_INFO << "External start";
      this->xrayExpoTimer = 0;
    }
  }
}

void MainWindow::onAcquisitionClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'B' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      timeWaitingBegin = std::chrono::steady_clock::now();
      qDebug() << Q_FUNC_INFO << "Acquisition";
    }
  }
}

void MainWindow::onTestClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'X' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      timeWaitingBegin = std::chrono::steady_clock::now();
      qDebug() << Q_FUNC_INFO << "Test";
    }
  }
}

void MainWindow::onExternalCounterClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'C' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "External counter";
    }
  }
}

void MainWindow::onEnableTimerClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'T' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Enable timer";
    }
  }
}

void MainWindow::onDisableTimerClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 't' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Disable timer";
    }
  }
}

void MainWindow::onInternalTimerChecked(bool check)
{
  char t = (check) ? 'T' : 't';
  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = t;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Internal timer: " << t;
    }
  }
}

void MainWindow::onExternalStartChecked(bool check)
{
  char e = (check) ? 'E' : 'e';
  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = e;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "External start: " << e;
    }
  }
}

void MainWindow::onStuckResetClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'R' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "I2C stuck reset";
    }
  }
}

void MainWindow::onEnableExtStartClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'E' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Enable external start";
    }
  }
}

void MainWindow::onRadioGroupK1Changed(QAbstractButton* aButton)
{
  QRadioButton* rButton = qobject_cast< QRadioButton* >(aButton);
  char c = 0;
  if (rButton && rButton == this->ui->RadioButton_LowK1)
  {
    c = 'y';
  }
  else if (rButton && rButton == this->ui->RadioButton_HighK1)
  {
    c = 'Y';
  }
  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = c;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    if (c && this->acquisitionPort->write(this->lastCommandData))
    {
      qDebug() << Q_FUNC_INFO << "Success: " << c;
    }
    else
    {
      qWarning() << Q_FUNC_INFO << "Failure: " << c;
    }
  }
  else
  {
    qWarning() << "Device is invalid or not connected";
  }
}

void MainWindow::onRadioGroupK2Changed(QAbstractButton* aButton)
{
  QRadioButton* rButton = qobject_cast< QRadioButton* >(aButton);
  char c = 0;
  if (rButton && rButton == this->ui->RadioButton_LowK2)
  {
    c = 'z';
  }
  else if (rButton && rButton == this->ui->RadioButton_HighK2)
  {
    c = 'Z';
  }

  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = c;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    if (c && this->acquisitionPort->write(this->lastCommandData))
    {
      qDebug() << Q_FUNC_INFO << "Success: " << c;
    }
    else
    {
      qWarning() << Q_FUNC_INFO << "Failure: " << c;
    }
  }
  else
  {
    qWarning() << "Device is invalid or not connected";
  }
}

void MainWindow::onSetInternalTimerClicked()
{
  char t = (ui->CheckBox_InternalTimer->isChecked()) ? 'T' : 't';
  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = t;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Internal timer: " << t;
    }
  }
}

void MainWindow::onSetExternalStartClicked()
{
  char e = (ui->CheckBox_ExternalStart->isChecked()) ? 'E' : 'e';
  unsigned char buf[COMMAND_BUFFER_SIZE] = {};
  buf[0] = e;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "External start: " << e;
    }
  }
}

/*
void MainWindow::onLowK1Clicked()
{
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->putChar('y');
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Low K1";
    }
  }
}

void MainWindow::onLowK2Clicked()
{
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->putChar('z');
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Low K2";
    }
  }
}

void MainWindow::onHighK1Clicked()
{
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->putChar('Y');
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "High K1";
    }
  }
}

void MainWindow::onHighK2Clicked()
{
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->putChar('Z');
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "High K2";
    }
  }
}
*/
void MainWindow::onDisableExtStartClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'e' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Disable external start";
    }
  }
}

void MainWindow::onType1Clicked()
{
  unsigned char com = ui->SpinBox_I2CCommand->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '1', com };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "HP206C I2C Type-1 command:" << com;
    }
  }
}

void MainWindow::onType2Clicked()
{
  unsigned char regAddress = ui->SpinBox_I2CCommand->value();
  unsigned char regValue = ui->SpinBox_I2CCommand->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '2', regAddress, regValue };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "HP206C I2C Type-2 register:" << regAddress << ", value:" << regValue;
    }
  }
}

void MainWindow::onType3Clicked()
{
  unsigned char regAddress = ui->SpinBox_I2CCommand->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '3', regAddress };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "HP206C I2C Type-3 register:" << regAddress;
    }
  }
}

void MainWindow::onType4Clicked()
{
  unsigned char com = ui->SpinBox_I2CCommand->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '4', com };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "HP206C I2C Type-4 command:" << com;
    }
  }
}

void MainWindow::onTemperaturePressureClicked()
{
  unsigned char com = ui->SpinBox_AdcCvt->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'P', com };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Temperature and pressure:" << com;
    }
  }
}

void MainWindow::onFirstConnectResponce()
{
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->putChar('e');
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "e reset";
    }
  }
}

void MainWindow::onPE0Clicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { '5' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "E0";
    }
  }
}

void MainWindow::onPE1Clicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { '6' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "E1";
    }
  }
}

void MainWindow::onPB1Clicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { '7' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "B1";
    }
  }
}

void MainWindow::oneClicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { 'q' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "get temperature";
    }
  }
}

void MainWindow::saveSettings()
{
  QSettings set;
  QString acquisitionDeviceName = ui->LineEdit_AcquisitionDeviceName->text();
  set.setValue( "device-name", acquisitionDeviceName);
}

void MainWindow::loadSettings()
{
  QSettings set;
  QString name = set.value( "device-name", "/dev/cabin26_platform").toString();
  ui->LineEdit_AcquisitionDeviceName->setText(name);
}

void MainWindow::onInitiateDeviceStopClicked()
{
  if (this->initiationProgress->isVisible())
  {
    this->initiationProgress->hide();
    this->initiationProgress->close();
  }
  this->initiationTimer->stop();
  qDebug() << Q_FUNC_INFO << "stop clicked";
}

void MainWindow::onInitiateDeviceFinished(int res)
{
  qDebug() << Q_FUNC_INFO << "finished result:" << res;
}

void MainWindow::updateTemperaturePressureCounter()
{
  double& p = this->sensorPressure;
  double& t = this->sensorTemperature;
  int& c = this->externalCounter;

  this->ui->Label_TemperaturePressureExtCount->setText(
    QObject::tr("Temperature (deg C): %1. Pressure (mbar): %2. Extenal count: %3").arg(t).arg(p).arg(c));
}

void MainWindow::onXrayAcquisitionClicked()
{
  char buf[COMMAND_BUFFER_SIZE] = { 'R' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      timeWaitingBegin = std::chrono::steady_clock::now();
      qDebug() << Q_FUNC_INFO << "X-ray acquisition start";
      this->xrayExpoTimer = this->ui->SpinBox_XrayExpoTimer->value();
    }
  }
}

void MainWindow::onXrayTimerClicked()
{
  int v = this->ui->SpinBox_XrayExpoTimer->value() / 32;
  char buf[COMMAND_BUFFER_SIZE] = { 'D', char(v) };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      timeWaitingBegin = std::chrono::steady_clock::now();
      qDebug() << Q_FUNC_INFO << "X-ray acquisition start, counter: " << v;
    }
  }
}

void MainWindow::saveAcquisitionData(TGraph* graph, int xrayCount, double t, double p, int adcCount)
{
  if (!this->rootFile)
  {
    return;
  }

  QString prefix = this->ui->LineEdit_Prefix->text();

  std::string prefixString = prefix.toStdString();
  QDateTime timeStamp = QDateTime::currentDateTime();

  QString timeStampStr = timeStamp.toString("ddMMyyyy_hhmmss");
  QString titleString = tr("%5_%1_%2_%3_%4").arg(xrayCount).arg(t).arg(p).arg(adcCount).arg(timeStampStr);
  std::string titleStdString = titleString.toStdString();

  std::unique_ptr< TTree > rootTree(new TTree(titleStdString.c_str(), prefixString.c_str()));

  rootTree->Branch("graph", graph);
  rootTree->Branch("xrayExpoTime", &xrayCount, "xrayExpoTime/I");
  rootTree->Branch("temperature", &t, "temperature/D");
  rootTree->Branch("pressure", &p, "pressure/D");
  rootTree->Branch("adcCount", &adcCount, "adcCount/I");

  rootTree->Fill();
  rootTree->Write();
}

void MainWindow::onOpenRootFileClicked()
{
  QFileDialog* dialog = new QFileDialog( this, tr("Open ROOT File..."));
  QStringList fileNames;

  dialog->setAcceptMode(QFileDialog::AcceptOpen);
  dialog->setFileMode(QFileDialog::ExistingFile);

#if QT_VERSION >= 0x050000
  filters << tr("ROOT Files *.root (*.root)");
  dialog->setNameFilters(filters);
#elif (QT_VERSION >= 0x040000 && QT_VERSION < 0x050000)
  dialog->setFilter(tr("ROOT Files *.root (*.root)"));
#endif

  if (dialog->exec())
  {
    fileNames = dialog->selectedFiles();

    if (!fileNames.isEmpty())
    {
      rootFileName = fileNames[0];
    }
  }
  delete dialog;

  if (rootFileName.isEmpty())
  {
    return;
  }

  this->ui->ListWidget_Spills->clear();

  std::string fname = rootFileName.toStdString();
  TFile* f = TFile::Open(fname.c_str());
  TIter keyList(f->GetListOfKeys());
  TKey* key = nullptr;
  while ((key = dynamic_cast<TKey*>(keyList())))
  {
    TClass* cl = gROOT->GetClass(key->GetClassName());
    if (cl && !cl->InheritsFrom("TTree"))
    {
      continue;
    }

    TTree *spillTree = dynamic_cast<TTree*>(key->ReadObj());
    if (spillTree)
    {
      const char* name = spillTree->GetName();
      ui->ListWidget_Spills->addItem(QString(name));
    }
  }
}

void MainWindow::onCurrentSpillItemChanged(QListWidgetItem* newItem,QListWidgetItem*)
{
  if (rootFileName.isEmpty())
  {
    return;
  }
  if (!newItem)
  {
    return;
  }

  std::string rootStdName = this->rootFileName.toStdString();
  TFile* f = TFile::Open(rootStdName.c_str());

  QString treeName = newItem->text();
  std::string treeStdName = treeName.toStdString();
  TTree* spillTree = dynamic_cast< TTree* >(f->Get(treeStdName.c_str()));
  int xrayExpoTime = -1;
  double pressure = -1.;
  double temperature = -1.;
  int adcCount = -1;
  if (spillTree)
  {
    spillTree->SetBranchAddress("adcCount", &adcCount);
    spillTree->SetBranchAddress("temperature", &temperature);
    spillTree->SetBranchAddress("pressure", &pressure);
    spillTree->SetBranchAddress("xrayExpoTime", &xrayExpoTime);

    Int_t nentries = static_cast<Int_t>(spillTree->GetEntries());

    for (Int_t i = 0; i < nentries; i++)
    {
      spillTree->GetEntry(i);
      this->ui->Label_AdcCount->setText(tr("ADC count: %1").arg(adcCount));
      this->ui->Label_Temperature->setText(tr("Temperature (C): %1").arg(temperature));
      this->ui->Label_Pressure->setText(tr("Pressure (mBar): %1").arg(pressure));
      this->ui->Label_ExposureTime->setText(tr("X-ray exposure time (ms): %1, code( %2 )").arg(xrayExpoTime).arg(xrayExpoTime / 32));
    }
    spillTree->SetBranchAddress("graph", &this->adcGraph);
    nentries = static_cast<Int_t>(spillTree->GetEntries());
    for (Int_t i = 0; i < nentries; i++)
    {
      spillTree->GetEntry(i);
    }
  }
  if (xrayExpoTime > 0)
  {
    Int_t n = this->adcGraph->GetN();
    for (Int_t i = 0; i < n; ++i)
    {
      Double_t x, y;
      Int_t z = this->adcGraph->GetPoint(i, x, y);
      if (z != -1)
      {
        this->signalGraph->SetPoint(i, x * ADC_COUNT_TIME_MS, y);
      }
    }
//    this->pedCanvas->cd();
//    this->pedPad->cd();
//    this->signalGraph->Draw("same");
//    this->pedPad->Update();
//    this->pedPad->Modified();
    this->signalPad->Update();
    this->signalPad->Modified();
    this->fitExpoSignalData(xrayExpoTime);
  }
  else
  {
    Int_t n = this->adcGraph->GetN();
    for (Int_t i = 0; i < n; ++i)
    {
      Double_t x, y;
      Int_t z = this->adcGraph->GetPoint(i, x, y);
      if (z != -1)
      {
        this->pedGraph->SetPoint(i, x * ADC_COUNT_TIME_MS, y);
      }
    }
    this->pedPad->Update();
    this->pedPad->Modified();
  }
  f->Close();
}

void MainWindow::onFitPedClicked()
{
  int pedBegin = this->ui->SpinBox_PedBegin->value();
  int pedEnd = this->ui->SpinBox_PedEnd->value();
  this->pedFit = std::unique_ptr< TF1 >(new TF1( "pedFit", linnn, Double_t(pedBegin), Double_t(pedEnd), 2));
  this->pedFit->SetParameter(0, 110.);
  this->pedFit->SetParameter(1, 0.07);
  this->pedFit->SetParNames("y0","k");
  this->pedCanvas->cd();
  this->pedPad->cd();
  this->pedGraph->Fit(this->pedFit.get(), "RB0");
  this->pedFit->Draw("same");
  this->pedPad->Update();
  this->pedPad->Modified();
}

void MainWindow::onFitSignalClicked()
{
  int signalBegin = this->ui->SpinBox_SignalBegin->value();
  int signalEnd = this->ui->SpinBox_SignalEnd->value();
  this->signalFit = std::unique_ptr< TF1 >(new TF1( "signalFit", linnn, Double_t(signalBegin), Double_t(signalEnd), 2));
  this->signalFit->SetParameter(0, 110.);
  this->signalFit->SetParameter(1, 0.15);
  this->signalFit->SetParNames("y0","k");
  this->signalCanvas->cd();
  this->signalPad->cd();
  this->signalGraph->Fit(this->signalFit.get(), "RB0");
  this->signalFit->Draw("same");
  this->signalPad->Update();
  this->signalPad->Modified();

  double kped = this->pedFit->GetParameter(1);
  double ksignal = this->signalFit->GetParameter(1);
  double hped = kped * (signalEnd - signalBegin) * (signalEnd - signalBegin) / 2.;
  double hsignal = ksignal * (signalEnd - signalBegin) * (signalEnd - signalBegin) / 2.;
  qDebug() << Q_FUNC_INFO << "K ped: " << kped << ", K signal: " << ksignal << ", Integral value: " << (hsignal - hped);
}

void MainWindow::fitExpoSignalData(int expoTimer)
{

  std::pair< int, int >& pedRange = xrayPedSignalRange[0];

  int pedBegin = pedRange.first;
  int pedEnd = pedRange.second;
  this->pedFit = std::unique_ptr< TF1 >(new TF1( "pedFit", linnn, Double_t(pedBegin), Double_t(pedEnd), 2));
  this->pedFit->SetParameter(0, 110.);
  this->pedFit->SetParameter(1, 0.07);
  this->pedFit->SetParNames("y0","k");
  this->signalCanvas->cd();
  this->signalPad->cd();
  this->signalGraph->Fit(this->pedFit.get(), "RB0");
  this->pedFit->Draw("same");
  this->pedPad->Update();
  this->pedPad->Modified();

  std::pair< int, int >& sigRange = xrayPedSignalRange[expoTimer];
  int signalBegin = sigRange.first;
  int signalEnd = sigRange.second;
  this->signalFit = std::unique_ptr< TF1 >(new TF1( "signalFit", linnn, Double_t(signalBegin), Double_t(signalEnd), 2));
  this->signalFit->SetParameter(0, 110.);
  this->signalFit->SetParameter(1, 0.15);
  this->signalFit->SetParNames("y0","k");
  this->signalGraph->Fit(this->signalFit.get(), "RB0");
  this->signalFit->Draw("same");
  this->signalPad->Update();
  this->signalPad->Modified();

  double kped = this->pedFit->GetParameter(1);
  double ksignal = this->signalFit->GetParameter(1);
  double hped = kped * (signalEnd - signalBegin) * (signalEnd - signalBegin) / 2.;
  double hsignal = ksignal * (signalEnd - signalBegin) * (signalEnd - signalBegin) / 2.;
  qDebug() << Q_FUNC_INFO << "K ped: " << kped << ", K signal: " << ksignal << ", Integral value: " << (hsignal - hped);
}
