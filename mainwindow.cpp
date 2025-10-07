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

constexpr double ADC_COUNT_TIME_MS = 1582.01 / 100000.;

}

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , timer(new QTimer(this))
  , initiationTimer(new QTimer(this))
  , initiationProgress(new QProgressDialog(this))
{
  ui->setupUi(this);

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

  connect( ui->PushButton_AcquisitionConnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceConnectClicked()));
  connect( ui->PushButton_AcquisitionDisconnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceDisconnectClicked()));
  connect( ui->PushButton_ExternalStart, SIGNAL(clicked()), this, SLOT(onExternalStartClicked()));
  connect( ui->PushButton_Acquisition, SIGNAL(clicked()), this, SLOT(onAcquisitionClicked()));
  connect( ui->PushButton_ExternalCounter, SIGNAL(clicked()), this, SLOT(onExternalCounterClicked()));
  connect( ui->PushButton_Test, SIGNAL(clicked()), this, SLOT(onTestClicked()));

  connect( ui->PushButton_SetExternalStart, SIGNAL(clicked()), this, SLOT(onSetExternalStartClicked()));

  connect( ui->PushButton_Type1, SIGNAL(clicked()), this, SLOT(onType1Clicked()));
  connect( ui->PushButton_Type2, SIGNAL(clicked()), this, SLOT(onType2Clicked()));
  connect( ui->PushButton_Type3, SIGNAL(clicked()), this, SLOT(onType3Clicked()));
  connect( ui->PushButton_Type4, SIGNAL(clicked()), this, SLOT(onType4Clicked()));
  connect( ui->PushButton_SoftReset, SIGNAL(clicked()), this, SLOT(onSoftResetClicked()));
  connect( ui->PushButton_PT, SIGNAL(clicked()), this, SLOT(onTemperaturePressureClicked()));
  connect( ui->PushButton_SetSamples1k, SIGNAL(clicked()), this, SLOT(onSetSamplesClicked()));

  connect( ui->PushButton_OpenFile, SIGNAL(clicked()), this, SLOT(onOpenRootFileClicked()));
  connect( ui->ListWidget_Spills, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(onCurrentSpillItemChanged(QListWidgetItem*,QListWidgetItem*)));

  connect( ui->ButtonGroup_K1, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK1Changed(QAbstractButton*)));
  connect( ui->ButtonGroup_K2, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK2Changed(QAbstractButton*)));
  connect( timer, SIGNAL(timeout()), this, SLOT(onRootEventsTimeout())); // iterate ROOT events

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
  this->enableButtons(true);
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
  this->enableButtons(false);

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
//  if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.size() == dataBegin)
//  {
//    qDebug() << Q_FUNC_INFO << "Slow extraction begins";
//    return;
//  }
  if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.endsWith("Finish"))
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
    for (int i = dataBegin; i < dataEnd; i += 2)
    {
      std::bitset< ADC_HIGH_BITS > high(data[i + 1] & 0x3F);
      std::bitset< ADC_LOW_BITS > low(data[i]);
      std::bitset< ADC_BITS > count((high.to_ulong() << ADC_LOW_BITS) | low.to_ulong());
      adcData.push_back(static_cast< int >(count.to_ulong()));
    }
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
    int count = int((tcnt1h.to_ulong() << CHAR_BIT) | tcnt1l.to_ulong()); // TCNT1
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
    ui->statusbar->showMessage(QObject::tr("Temperature: %1 deg C. Pressure: %2 (mbar)").arg(temperature).arg(pressure), 1000);
    qDebug() << Q_FUNC_INFO << "Temperature (deg C):" << temperature << ". Pressure (mbar):" << pressure;

    this->sensorTemperature = temperature;
    this->sensorPressure = pressure;
    this->updateTemperaturePressureCounter();

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
    this->saveAcquisitionData(this->adcGraph, this->samples1k, temperature, pressure, this->externalCounter);
    this->acquisitionPortData.clear();
    return;
  }
  else if (this->acquisitionPortData.size() == 3 && this->acquisitionPortData.startsWith('M') &&
      (this->acquisitionPortData.endsWith("OK") || this->acquisitionPortData.endsWith("NO")))
  {
    qDebug() << Q_FUNC_INFO << "Set samples * 1000 response:" << this->acquisitionPortData;
    if (this->acquisitionPortData.endsWith("OK"))
    {
      this->samples1k = this->ui->SpinBox_Samples1k->value();
    }
    this->acquisitionPortData.clear();
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

void MainWindow::onSetSamplesClicked()
{
  unsigned char com = ui->SpinBox_Samples1k->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'M', com };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Samples * 1000 value:" << com;
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

void MainWindow::onSoftResetClicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '1', 0x06 };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "HP206C I2C SOFT_RST command";
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

void MainWindow::saveSettings()
{
  QSettings set;
  QString acquisitionDeviceName = ui->LineEdit_AcquisitionDeviceName->text();
  set.setValue( "device-name", acquisitionDeviceName);
  int samples = ui->SpinBox_Samples1k->value();
  set.setValue( "samples-1k", samples);

}

void MainWindow::loadSettings()
{
  QSettings set;
  QString name = set.value( "device-name", "/dev/cabin26_platform").toString();
  ui->LineEdit_AcquisitionDeviceName->setText(name);
  int samples = set.value( "samples-1k", 100).toInt();
  ui->SpinBox_Samples1k->setValue(samples);
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

void MainWindow::saveAcquisitionData(TGraph* graph, int samples, double t, double p, int adcCount)
{
  if (!this->rootFile)
  {
    return;
  }

  QString prefix = this->ui->LineEdit_Prefix->text();

  std::string prefixString = prefix.toStdString();
  QDateTime timeStamp = QDateTime::currentDateTime();

  QString timeStampStr = timeStamp.toString("ddMMyyyy_hhmmss");
  QString titleString = tr("%5_%1_%2_%3_%4").arg(samples).arg(t).arg(p).arg(adcCount).arg(timeStampStr);
  std::string titleStdString = titleString.toStdString();

  std::unique_ptr< TTree > rootTree(new TTree(titleStdString.c_str(), prefixString.c_str()));

  rootTree->Branch("graph", graph);
  rootTree->Branch("samples1k", &samples, "samples1k/I");
  rootTree->Branch("temperature", &t, "temperature/D");
  rootTree->Branch("pressure", &p, "pressure/D");
  rootTree->Branch("adcCount", &adcCount, "adcCount/I");

  rootTree->Fill();
  rootTree->Write();
}

void MainWindow::enableButtons(bool enable)
{
  ui->PushButton_ExternalStart->setEnabled(enable);
  ui->PushButton_Acquisition->setEnabled(enable);
  ui->PushButton_ExternalCounter->setEnabled(enable);
  ui->PushButton_SetExternalStart->setEnabled(enable);
  ui->PushButton_Test->setEnabled(enable);
  ui->RadioButton_HighK1->setEnabled(enable);
  ui->RadioButton_HighK2->setEnabled(enable);
  ui->RadioButton_LowK1->setEnabled(enable);
  ui->RadioButton_LowK2->setEnabled(enable);
  ui->PushButton_Type1->setEnabled(enable);
  ui->PushButton_Type2->setEnabled(enable);
  ui->PushButton_Type3->setEnabled(enable);
  ui->PushButton_Type4->setEnabled(enable);
  ui->PushButton_SoftReset->setEnabled(enable);
  ui->PushButton_PT->setEnabled(enable);
  ui->PushButton_SetSamples1k->setEnabled(enable);
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
  int samples = -1;
  double pressure = -1.;
  double temperature = -1.;
  int adcCount = -1;
  if (spillTree)
  {
    spillTree->SetBranchAddress("adcCount", &adcCount);
    spillTree->SetBranchAddress("temperature", &temperature);
    spillTree->SetBranchAddress("pressure", &pressure);
    spillTree->SetBranchAddress("samples1k", &samples);

    Int_t nentries = static_cast<Int_t>(spillTree->GetEntries());

    for (Int_t i = 0; i < nentries; i++)
    {
      spillTree->GetEntry(i);
      this->ui->Label_AdcCount->setText(tr("ADC count: %1").arg(adcCount));
      this->ui->Label_Temperature->setText(tr("Temperature (C): %1").arg(temperature));
      this->ui->Label_Pressure->setText(tr("Pressure (mBar): %1").arg(pressure));
      this->ui->SpinBox_Samples1k->setValue(samples);
    }
    spillTree->SetBranchAddress("graph", &this->adcGraph);
    nentries = static_cast<Int_t>(spillTree->GetEntries());
    for (Int_t i = 0; i < nentries; i++)
    {
      spillTree->GetEntry(i);
    }
  }
  f->Close();
}
