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
#include <QElapsedTimer>

#include <TFile.h>
#include <TTree.h>
#include <TKey.h>
#include <TF1.h>
#include <TROOT.h>
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
#include <TH1.h>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics/stats.hpp>
#include <boost/accumulators/statistics/mean.hpp>
#include <boost/accumulators/statistics/moment.hpp>

#include <sstream>
#include <bitset>
#include <cstring>

namespace
{

//constexpr double ADC_COUNT_TIME_MS = 1582.01 / 100000.;
constexpr double ADC_COUNT_TIME_MS = 10.6E-03 * 4.;
constexpr double DOSE_CHAMBER_CAPACITY = 275.; // pF
bool acquisition_flag = false;

typedef boost::accumulators::accumulator_set< \
  Double_t, \
  boost::accumulators::stats< \
    boost::accumulators::tag::sum, \
    boost::accumulators::tag::count, \
    boost::accumulators::tag::mean, \
    boost::accumulators::tag::moment< 2 > \
  > \
> MeanDispAccumSet;


Double_t discharge(Double_t *x, Double_t* par)
{
  return par[0] * (1. - std::exp(-1. * par[1] * (x[0] - par[2])));
}

Double_t charge(Double_t *x, Double_t* par)
{
  return par[0] * std::exp(-1. * par[1] * (x[0] - par[2]));
}

Double_t line(Double_t *x, Double_t* par)
{
  return par[0] + x[0] * par[1];
}

unsigned short crc16_update(unsigned short crc, unsigned char a)
{
  crc ^= a;
  for (int i = 0; i < CHAR_BIT; ++i)
  {
    if (crc & 1)
    {
      crc = (crc >> 1) ^ 0xA001;
    }
    else
    {
      crc = (crc >> 1);
    }
  }

  return crc;
}

unsigned short crc16_calc(const unsigned char* buffer, size_t size)
{
  unsigned short crc = 0xFFFF;
  for (size_t i = 0; i < size; ++i)
  {
    crc = crc16_update(crc, buffer[i]);
  }

  return crc;
}

bool check_crc16(const unsigned char* buffer, std::size_t dataSize)
{
  unsigned short dataCRC16 = crc16_calc(buffer, dataSize - 2); // calculated CRC16 check sum for buffer data
  unsigned short comCRC16 = (buffer[dataSize - 1] << CHAR_BIT) | buffer[dataSize - 2]; // check sum from buffer
  return (dataCRC16 == comCRC16);
}

const int decimation = 10;

}

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , acquisitionPort(new QSerialPort(this))
  , dcSupplyPort(new QSerialPort(this))
  , initiationTimer(new QTimer(this))
  , initiationProgress(new QProgressDialog(this))
  , adcGraph(new TGraph(1000))
  , elapsedTimer(new QElapsedTimer())
{
#if QT_VERSION < QT_VERSION_CHECK(6,0,0)
  gROOT->SetWebDisplay("qt5");
#else
  gROOT->SetWebDisplay("qt6");
#endif
  ui->setupUi(this);

  this->initiationProgress->setWindowTitle(tr("Dose chamber"));
  this->initiationProgress->setAutoClose(false);
  connect(this->initiationProgress.get(), SIGNAL(finished(int)), this, SLOT(onInitiateDeviceFinished(int)));
  connect(this->initiationProgress.get(), SIGNAL(canceled()), this, SLOT(onInitiateDeviceStopClicked()));
  this->initiationProgress->setLabelText("Reset ATmega64L input command buffer...");
  this->initiationProgress->setRange(0, 0);
  this->initiationProgress->close();

  this->adcCanvas = this->ui->RootCanvas_AdcGraph->getCanvas();
  this->adcCanvas->cd();
  this->adcPad = new TPad("adcPad", "Grid", 0., 0., 1., 1.);
  this->adcPad->SetGrid();
  this->adcPad->Draw();
  this->adcPad->cd();
  this->adcGraph.reset(new TGraph());
  this->adcGraph->SetTitle("ADC data;Time, ms;ADC count");
  this->adcGraph->Draw("AL*");

  this->adcCanvasErr = this->ui->RootCanvas_AdcGraphErr->getCanvas();
  this->adcCanvasErr->cd();
  this->adcPadErr = new TPad("adcPadErr", "Grid", 0., 0., 1., 1.);
  this->adcPadErr->SetGrid();
  this->adcPadErr->Draw();
  this->adcPadErr->cd();
  this->adcGraphErr.reset(new TGraphErrors());
  this->adcGraphErr->SetTitle("ADC data;Time, ms;ADC count");
  this->adcGraphErr->Draw("AL*");

  connect( ui->PushButton_AcquisitionConnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceConnectClicked()));
  connect( ui->PushButton_AcquisitionDisconnect, SIGNAL(clicked()), this, SLOT(onAcquisitionDeviceDisconnectClicked()));
  connect( ui->PushButton_ExternalStart, SIGNAL(clicked()), this, SLOT(onExternalStartClicked()));
  connect( ui->PushButton_Acquisition, SIGNAL(clicked()), this, SLOT(onAcquisitionClicked()));
  connect( ui->PushButton_ExternalCounter, SIGNAL(clicked()), this, SLOT(onExternalCounterClicked()));
  connect( ui->PushButton_Test, SIGNAL(clicked()), this, SLOT(onTestClicked()));
  connect( ui->PushButton_DelayTime, SIGNAL(clicked()), this, SLOT(onSetDelayTimeClicked()));
  connect( ui->PushButton_SetDischarge, SIGNAL(clicked()), this, SLOT(onSetDischargeClicked()));
  connect( ui->PushButton_AcquisitionTime, SIGNAL(clicked()), this, SLOT(onSetAcquisitionTimeClicked()));

  connect( ui->PushButton_SetExternalStart, SIGNAL(clicked()), this, SLOT(onSetExternalStartClicked()));

  connect( ui->PushButton_Type1, SIGNAL(clicked()), this, SLOT(onType1Clicked()));
  connect( ui->PushButton_Type2, SIGNAL(clicked()), this, SLOT(onType2Clicked()));
  connect( ui->PushButton_Type3, SIGNAL(clicked()), this, SLOT(onType3Clicked()));
  connect( ui->PushButton_Type4, SIGNAL(clicked()), this, SLOT(onType4Clicked()));
  connect( ui->PushButton_SoftReset, SIGNAL(clicked()), this, SLOT(onSoftResetClicked()));
  connect( ui->PushButton_PT, SIGNAL(clicked()), this, SLOT(onTemperaturePressureClicked()));
  connect( ui->PushButton_SetSamples1k, SIGNAL(clicked()), this, SLOT(onSetSamplesClicked()));
  connect( ui->PushButton_FitTest, SIGNAL(clicked()), this, SLOT(onFitTestClicked()));

  connect( ui->PushButton_OpenFile, SIGNAL(clicked()), this, SLOT(onOpenRootFileClicked()));
  connect( ui->ListWidget_Spills, SIGNAL(currentItemChanged(QListWidgetItem*,QListWidgetItem*)), this, SLOT(onCurrentSpillItemChanged(QListWidgetItem*,QListWidgetItem*)));

  connect( ui->ButtonGroup_K1, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK1Changed(QAbstractButton*)));
  connect( ui->ButtonGroup_K2, SIGNAL(buttonClicked(QAbstractButton*)), this, SLOT(onRadioGroupK2Changed(QAbstractButton*)));

  connect(this->acquisitionPort.get(), SIGNAL(readyRead()), this, SLOT(acquisitionSerialPortDataReady()));
  connect(this->acquisitionPort.get(), SIGNAL(bytesWritten(qint64)), this, SLOT(acquisitionSerialPortBytesWritten(qint64)));
  connect(this->acquisitionPort.get(), SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(acquisitionSerialPortError(QSerialPort::SerialPortError)));

  connect(this->dcSupplyPort.get(), SIGNAL(readyRead()), this, SLOT(dcSerialPortDataReady()));
  connect(this->dcSupplyPort.get(), SIGNAL(bytesWritten(qint64)), this, SLOT(dcSerialPortBytesWritten(qint64)));
  connect(this->dcSupplyPort.get(), SIGNAL(errorOccurred(QSerialPort::SerialPortError)), this, SLOT(dcSerialPortError(QSerialPort::SerialPortError)));
  connect(this->ui->PushButton_ConnectHspy, SIGNAL(clicked()), this, SLOT(onDcPowerSupplyConnectClicked()));
  connect(this->ui->PushButton_OnOff, SIGNAL(clicked()), this, SLOT(onSetOutputClicked()));
  connect(this->ui->PushButton_SetVoltage, SIGNAL(clicked()), this, SLOT(onSetVoltageClicked()));
  connect(this->ui->PushButton_SetCurrent, SIGNAL(clicked()), this, SLOT(onSetCurrentClicked()));
  connect(this->ui->PushButton_GetCurrent, SIGNAL(clicked()), this, SLOT(onGetCurrentClicked()));
  connect(this->ui->PushButton_GetVoltage, SIGNAL(clicked()), this, SLOT(onGetVoltageClicked()));

  connect(this->ui->PushButton_SetXray1, SIGNAL(clicked()), this, SLOT(onSetXray1Clicked()));
  connect(this->ui->PushButton_SetXray2, SIGNAL(clicked()), this, SLOT(onSetXray2Clicked()));
  connect(this->ui->PushButton_ResetXray12, SIGNAL(clicked()), this, SLOT(onResetXray12Clicked()));

  connect( initiationTimer.get(), SIGNAL(timeout()), this, SLOT(onFirstConnectResponce())); // first responce timeout
  this->initiationTimer->setInterval(1000);
  loadSettings();
  this->initiationTimer->stop();
}

MainWindow::~MainWindow()
{
  saveSettings();

  if (this->rootFile)
  {
    this->rootFile->Write();
//    this->rootFile->Close();
    delete this->rootFile;
    this->rootFile = nullptr;
  }
  this->adcPad->GetListOfPrimitives()->Remove(this->adcGraph.get());
  this->adcPadErr->GetListOfPrimitives()->Remove(this->adcGraphErr.get());

  this->adcGraph.reset();
  this->adcGraphErr.reset();

/*
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
*/
  delete ui;
}

void MainWindow::onDcPowerSupplyConnectClicked()
{
  bool enabled = false;
  if (this->dcSupplyPort && !this->dcSupplyPort->isOpen())
  {
    QString name = ui->LineEdit_DcPowerSupply->text();
    this->dcSupplyPort->setPortName(name);

    this->dcSupplyPort->setBaudRate(QSerialPort::Baud9600);
    this->dcSupplyPort->setDataBits(QSerialPort::Data8);
    this->dcSupplyPort->setParity(QSerialPort::NoParity);
    this->dcSupplyPort->setStopBits(QSerialPort::OneStop);
    this->dcSupplyPort->setFlowControl(QSerialPort::NoFlowControl);
    enabled = this->dcSupplyPort->open(QIODevice::ReadWrite);
    enabled &= this->dcSupplyPort->flush();
    this->ui->PushButton_ConnectHspy->setText("Disconnect");
  }
  else if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    this->dcSupplyPort->flush();
    this->dcSupplyPort->close();
    this->ui->PushButton_ConnectHspy->setText("Connect");
  }
  if (enabled)
  {
    qDebug() << "Device connected";
  }
  else
  {
    qDebug() << "Device disconnected";
  }
}

void MainWindow::dcSerialPortBytesWritten(qint64 bytes)
{
  qDebug() << Q_FUNC_INFO << "Serial port data written bytes: " << bytes;
}

void MainWindow::dcSerialPortDataReady()
{
  int address = this->ui->HorizontalSlider_DcAddress->value();
  QByteArray res = this->dcSupplyPort->readAll();
  qDebug() << Q_FUNC_INFO << "Input res: " << res;
  this->dcPortData.append(res);
  const char* data = this->dcPortData.data();
  const unsigned char* buf = reinterpret_cast< const unsigned char* >(data);
  if (buf && (this->dcPortData.size() == 8) && (buf[0] == address) && (buf[1] == 0x06))
  {
    if (check_crc16(buf, 8))
    {
      qDebug() << Q_FUNC_INFO << "Output is changed";
    }
    else
    {
      qDebug() << Q_FUNC_INFO << "Output wrong CRC";
    }
    this->dcPortData.clear();
  }
  if (buf && (this->dcPortData.size() == 7) && (buf[0] == address) && (buf[1] == 0x03))
  {
    if (check_crc16(buf, 7) && buf[2] == 0x02)
    {
      uint16_t v = (buf[3] << CHAR_BIT) | buf[4];
      qDebug() << Q_FUNC_INFO << "Voltage value: " << double(v) / double(decimation) << " V";
    }
    if (check_crc16(buf, 7) && buf[2] == 0x03)
    {
        uint16_t i = (buf[3] << CHAR_BIT) | buf[4];
        qDebug() << Q_FUNC_INFO << "Current value: " << double(i) / double(decimation) << " mA";
    }
    if (!check_crc16(buf, 7))
    {
      qDebug() << Q_FUNC_INFO << "Output wrong CRC";
    }
    this->dcPortData.clear();
  }
}

void MainWindow::dcSerialPortError(QSerialPort::SerialPortError)
{
}

void MainWindow::onSetCurrentClicked()
{
  int current_mA = 20 * decimation;
  int address = this->ui->HorizontalSlider_DcAddress->value();
  uint8_t highCur = current_mA >> CHAR_BIT;
  uint8_t lowCur = current_mA & 0xFF;
  unsigned char command[8] = { static_cast< unsigned char >(address), 0x06, 0x10, 0x01, highCur, lowCur };
  unsigned short checkS = crc16_calc(command, 6);
  command[6] = checkS & 0xFF;
  command[7] = (checkS >> CHAR_BIT);
  QByteArray com = QByteArray::fromRawData(reinterpret_cast< char* >(command), 8);
  if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    qint64 res = dcSupplyPort->write(com);
    if (res > 0)
    {
      qDebug() << Q_FUNC_INFO << "Written current: " << res;
    }
  }
  else
  {
    qDebug() << Q_FUNC_INFO << "Can't write";
  }
}

void MainWindow::onSetVoltageClicked()
{
  int volt = this->ui->HorizontalSlider_Voltage->value() * decimation;
  int address = this->ui->HorizontalSlider_DcAddress->value();
  uint8_t highVol = volt >> CHAR_BIT;
  uint8_t lowVol = volt & 0xFF;
  unsigned char command[8] = { static_cast< unsigned char >(address), 0x06, 0x10, 0x00, highVol, lowVol };
  unsigned short checkS = crc16_calc(command, 6);
  command[6] = checkS & 0xFF;
  command[7] = (checkS >> CHAR_BIT);
  QByteArray com = QByteArray::fromRawData(reinterpret_cast< char* >(command), 8);
  if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    qint64 res = dcSupplyPort->write(com);
    if (res > 0)
    {
      qDebug() << Q_FUNC_INFO << "Written voltage: " << res;
    }
  }
  else
  {
    qDebug() << Q_FUNC_INFO << "Can't write";
  }
}

void MainWindow::onGetVoltageClicked()
{
  int address = this->ui->HorizontalSlider_DcAddress->value();
  unsigned char command[8] = { static_cast< unsigned char >(address), 0x03, 0x10, 0x02, 0x00, 0x01 };
  unsigned short checkS = crc16_calc(command, 6);
  command[6] = checkS & 0xFF;
  command[7] = (checkS >> CHAR_BIT);
  QByteArray com = QByteArray::fromRawData(reinterpret_cast< char* >(command), 8);
  if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    qint64 res = dcSupplyPort->write(com);
    if (res > 0)
    {
      qDebug() << Q_FUNC_INFO << "Get voltage: " << res;
    }
  }
  else
  {
    qDebug() << Q_FUNC_INFO << "Can't write";
  }
}

void MainWindow::onGetCurrentClicked()
{
  int address = this->ui->HorizontalSlider_DcAddress->value();
  unsigned char command[8] = { static_cast< unsigned char >(address), 0x03, 0x10, 0x03, 0x00, 0x01 };
  unsigned short checkS = crc16_calc(command, 6);
  command[6] = checkS & 0xFF;
  command[7] = (checkS >> CHAR_BIT);
  QByteArray com = QByteArray::fromRawData(reinterpret_cast< char* >(command), 8);
  if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    qint64 res = dcSupplyPort->write(com);
    if (res > 0)
    {
      qDebug() << Q_FUNC_INFO << "Get current: " << res;
    }
  }
  else
  {
    qDebug() << Q_FUNC_INFO << "Can't write";
  }
}

void MainWindow::onSetOutputClicked()
{
  bool state = this->ui->CheckBox_OnOff->isChecked();
  int address = this->ui->HorizontalSlider_DcAddress->value();
  unsigned char command[8] = { static_cast< unsigned char >(address), 0x06, 0x10, 0x04, 0x00, state };
  unsigned short checkS = crc16_calc(command, 6);
  command[6] = checkS & 0xFF;
  command[7] = (checkS >> CHAR_BIT);
  QByteArray com = QByteArray::fromRawData(reinterpret_cast< char* >(command), 8);
  if (this->dcSupplyPort && this->dcSupplyPort->isOpen())
  {
    qint64 res = dcSupplyPort->write(com);
    if (res > 0)
    {
      qDebug() << Q_FUNC_INFO << "Written state: " << res;
    }
  }
  else
  {
    qDebug() << Q_FUNC_INFO << "Can't write";
  }
}

void MainWindow::onAcquisitionDeviceConnectClicked()
{
  bool enabled = false;
  if (this->acquisitionPort)
  {
    QString name = ui->LineEdit_AcquisitionDeviceName->text();
    this->acquisitionPort->setPortName(name);

//    this->acquisitionPort->setBaudRate(QSerialPort::Baud57600);
//    this->acquisitionPort->setDataBits(QSerialPort::Data8);
//    this->acquisitionPort->setParity(QSerialPort::NoParity);
//    this->acquisitionPort->setStopBits(QSerialPort::OneStop);
//    this->acquisitionPort->setFlowControl(QSerialPort::NoFlowControl);

    enabled = true;
    ui->statusbar->showMessage(QObject::tr("Acquisition device connected"), 1000);
    this->acquisitionPortData.clear();

    this->initiationProgress->show();
    this->initiationTimer->start();

    QDateTime dateTime = QDateTime::currentDateTime();
    std::stringstream sstream;
    sstream << "RUN_" << dateTime.toString("ddMMyyyy_hhmmss").toStdString() << ".root";
    std::string filename = sstream.str();
    if (this->rootFile)
    {
      this->rootFile->Write();
      this->rootFile->Close();
      delete this->rootFile;
      this->rootFile = nullptr;
    }
    this->rootFile = new TFile(filename.c_str(), "RECREATE");
    enabled = this->acquisitionPort->open(QIODevice::ReadWrite);
    enabled &= this->acquisitionPort->flush();
  }
  ui->PushButton_AcquisitionConnect->setEnabled(!enabled);
  ui->PushButton_AcquisitionDisconnect->setEnabled(enabled);
  this->enableButtons(enabled);
}

void MainWindow::onAcquisitionDeviceDisconnectClicked()
{
  bool enabled = false;
  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    this->acquisitionPort->flush();
    this->acquisitionPort->close();
  }

  if (this->rootFile)
  {
    this->rootFile->Write();
    this->rootFile->Close();
    delete this->rootFile;
    this->rootFile = nullptr;
  }

  this->acquisitionPortData.clear();
  this->lastCommandData.clear();
  ui->PushButton_AcquisitionConnect->setEnabled(!enabled);
  ui->PushButton_AcquisitionDisconnect->setEnabled(enabled);
  this->enableButtons(false);

  this->initiationTimer->stop();

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

  const int dataBegin = std::strlen("Start");
  if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.size() == dataBegin)
  {
    qDebug() << Q_FUNC_INFO << "Slow extraction begins";
    timeWaitingBegin = std::chrono::steady_clock::now();
    this->elapsedTimer->start();
    acquisition_flag = true;
    return;
  }
  if (this->acquisitionPortData.startsWith("Start") && this->acquisitionPortData.endsWith("Finish"))
  {
    acquisition_flag = true;
    timeWaitingEnd = std::chrono::steady_clock::now();
    std::chrono::duration< float, std::milli > spillDuration(timeWaitingEnd - timeWaitingBegin);
    ui->statusbar->showMessage(QObject::tr("%1 ms").arg(spillDuration.count()), 3000);

    qDebug() << Q_FUNC_INFO << "Spill time (ms): " << spillDuration.count() << ' ' << this->elapsedTimer->elapsed();

    const QByteArray& data = this->acquisitionPortData;
    int dataEnd = data.indexOf("Finish");
    std::vector< int > adcData;
    constexpr int ADC_HIGH_BITS = CHAR_BIT;
    constexpr int ADC_LOW_BITS = CHAR_BIT;
    constexpr int ADC_BITS = ADC_LOW_BITS + ADC_HIGH_BITS;
    for (int i = dataBegin; i < dataEnd; i += 2)
    {
      std::bitset< ADC_HIGH_BITS > high(data[i + 1]);
      std::bitset< ADC_LOW_BITS > low(data[i]);
      std::bitset< ADC_BITS > count((high.to_ulong() << 8) | low.to_ulong());
      adcData.push_back(static_cast< int >(count.to_ulong()));
    }
    qDebug() << Q_FUNC_INFO << "Slow extraction ends, data size: " << adcData.size() << " Time for ADC count: "
             << adcData.size() / spillDuration.count() << " ms^-1";
    int i = 0;
    if (adcData.size())
    {
      this->adcCanvas->cd();
      this->adcPad->cd();
      if (this->adcGraph)
      {
        this->adcPad->GetListOfPrimitives()->Remove(this->adcGraph.get());
      }
      this->adcGraph.reset(new TGraph(adcData.size()));
      this->adcGraph->SetTitle("ADC data;Time, ms;ADC count");
    }
    for (int v : adcData)
    {
      this->adcGraph->SetPoint( i, Double_t(i * ADC_COUNT_TIME_MS), Double_t(v));
      i++;
    }
    this->adcGraph->Draw("AL*");

    this->adcPad->Modified();
    this->adcPad->Update();

    constexpr Int_t binWidthMs = 20;
    Int_t nofBins = this->adcGraph->GetN() * ADC_COUNT_TIME_MS / Double_t(binWidthMs);
    Int_t posBinWidth = this->adcGraph->GetN() / nofBins;
    std::vector< double > vx, vy, vxerr, vyerr;

    for (Int_t bin = 0; bin < nofBins; ++bin)
    {
      MeanDispAccumSet x, y;
      for (Int_t i = 0; i < posBinWidth; ++i)
      {
        Double_t xPos, yPos;
        Int_t pos = bin * posBinWidth + i;
        this->adcGraph->GetPoint(pos, xPos , yPos);
        x(xPos);
        y(yPos);
      }
      Double_t meanX = boost::accumulators::mean(x);
      Double_t meanY = boost::accumulators::mean(y);
      Double_t mom2X = boost::accumulators::moment< 2 >(x);
      Double_t mom2Y = boost::accumulators::moment< 2 >(y);
      vx.push_back(meanX);
      vy.push_back(meanY);
      vxerr.push_back(std::sqrt(mom2X - meanX * meanX));
      vyerr.push_back(std::sqrt(mom2Y - meanY * meanY));
    }

    TGraphErrors* graphErr = new TGraphErrors(nofBins);
    for (int i = 0; i < nofBins; ++i)
    {
      graphErr->SetPoint(i, vx[i], vy[i]);
      graphErr->SetPointError(i, vxerr[i], vyerr[i]);
    }
    this->adcPadErr->Clear();
    this->adcPadErr->cd();
    this->adcGraphErr.reset(graphErr);
    this->adcGraphErr->SetTitle("ADC data;Time, ms;ADC count");
    this->adcGraphErr->Draw("AL*");
    this->adcPadErr->Update();
    this->adcPadErr->Modified();
/*
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
*/
    this->acquisitionPortData.clear();
    if (acquisition_flag)
    {
      this->onExternalCounterClicked();
    }
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
    const int finishOK = std::strlen("OK");
    this->acquisitionPortData.remove(0, pos + finishOK);

    this->externalCounter = count;
    this->acquisitionPortData.clear();
    if (acquisition_flag)
    {
      this->onTemperaturePressureClicked();
    }
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
  else if (this->acquisitionPortData.size() == 3 &&
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
    if (this->acquisitionPortData.startsWith('M') && this->acquisitionPortData.endsWith("OK"))
    {
      this->samples1k = this->ui->SpinBox_Samples1k->value();
      qDebug() << Q_FUNC_INFO << "Set samples * 1000: " << this->samples1k;
    }
    if (this->acquisitionPortData.startsWith('d') && this->acquisitionPortData.endsWith("OK"))
    {
      qDebug() << Q_FUNC_INFO << "Delay time is set";
    }
    if (this->acquisitionPortData.startsWith('D') && this->acquisitionPortData.endsWith("OK"))
    {
      qDebug() << Q_FUNC_INFO << "Acquisition time is set";
    }
    if (this->acquisitionPortData.contains("eOK"))
    {
      qDebug() << Q_FUNC_INFO << "Initiation is finished";
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
    if (acquisition_flag)
    {
      this->saveAcquisitionData(this->adcGraph.get(), this->samples1k, temperature, pressure, this->externalCounter);
      acquisition_flag = false;
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
//  char buf[COMMAND_BUFFER_SIZE] = { 's' };
  this->lastCommandData = QByteArray( buf, COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      this->elapsedTimer->start();
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
      this->elapsedTimer->start();
      qDebug() << Q_FUNC_INFO << "Acquisition";
    }
  }
}

void MainWindow::onTestClicked()
{
//  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'X' };
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'x' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      this->elapsedTimer->start();
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

void MainWindow::onSetDelayTimeClicked()
{
  int delay = ui->SpinBox_DelayTime->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'd' };
  buf[1] = (delay >> CHAR_BIT) & 0xFF;
  buf[2] = delay & 0xFF;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Delay time: " << delay << " ms";
    }
  }
}

void MainWindow::onSetDischargeClicked()
{
  int dis = ui->SpinBox_Discharge->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'Q', static_cast< unsigned char >(dis) };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Discharge: " << dis;
    }
  }
}

void MainWindow::onSetAcquisitionTimeClicked()
{
  int delay = ui->SpinBox_AcquisitionTime->value();
  unsigned char buf[COMMAND_BUFFER_SIZE] = { 'D' };
  buf[1] = (delay >> CHAR_BIT) & 0xFF;
  buf[2] = delay & 0xFF;
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Acquisition time: " << delay << " ms";
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
  this->initiationTimer->stop();
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
  ui->PushButton_SetDischarge->setEnabled(enable);
  ui->PushButton_ExternalCounter->setEnabled(enable);
  ui->PushButton_SetExternalStart->setEnabled(enable);
  ui->PushButton_DelayTime->setEnabled(enable);
  ui->PushButton_AcquisitionTime->setEnabled(enable);
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
  QStringList fileNames, filters;

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
  QString dateName = treeName.left(15);
  QDateTime dt = QDateTime::fromString(dateName, "ddMMyyyy_hhmmss");
  QDateTime dt1 = dt.addSecs(-234);
  qDebug() << Q_FUNC_INFO << dt1.toString("ddMMyyyy_hhmmss");

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
      this->ui->Label_Samples1k->setText(tr("Samples * 1k: %1").arg(samples));
      this->ui->SpinBox_Samples1k->setValue(samples);
    }
    TGraph* graph = this->adcGraph.get();
    spillTree->SetBranchAddress("graph", &graph);
    nentries = static_cast<Int_t>(spillTree->GetEntries());
    for (Int_t i = 0; i < nentries; i++)
    {
      spillTree->GetEntry(i);
    }
  }
  f->Close();
  this->adcPad->Modified();
  this->adcPad->Update();

  constexpr Int_t binWidthMs = 20;
  Int_t nofBins = this->adcGraph->GetN() * ADC_COUNT_TIME_MS / Double_t(binWidthMs);
  Int_t posBinWidth = this->adcGraph->GetN() / nofBins;
  std::vector< double > vx, vy, vxerr, vyerr;

  for (Int_t bin = 0; bin < nofBins; ++bin)
  {
    MeanDispAccumSet x, y;
    for (Int_t i = 0; i < posBinWidth; ++i)
    {
      Double_t xPos, yPos;
      Int_t pos = bin * posBinWidth + i;
      this->adcGraph->GetPoint(pos, xPos , yPos);
      x(xPos);
      y(yPos);
    }
    Double_t meanX = boost::accumulators::mean(x);
    Double_t meanY = boost::accumulators::mean(y);
    Double_t mom2X = boost::accumulators::moment< 2 >(x);
    Double_t mom2Y = boost::accumulators::moment< 2 >(y);
    vx.push_back(meanX);
    vy.push_back(meanY);
    vxerr.push_back(std::sqrt(mom2X - meanX * meanX));
    vyerr.push_back(std::sqrt(mom2Y - meanY * meanY));
  }

  TGraphErrors* graphErr = new TGraphErrors(nofBins);
  for (int i = 0; i < nofBins; ++i)
  {
    graphErr->SetPoint(i, vx[i], vy[i]);
    graphErr->SetPointError(i, vxerr[i], vyerr[i]);
  }
  this->adcPadErr->Clear();
  this->adcPadErr->cd();
  graphErr->SetTitle("ADC data;Time, ms;ADC count");
  graphErr->Draw("AL*");
  this->adcGraphErr.reset(graphErr);
  this->adcPadErr->Update();
  this->adcPadErr->Modified();
  Double_t initValue = this->adcGraphErr->GetPointY(0);
  Double_t integralValue = this->adcGraphErr->GetPointY(0) - initValue;
  Double_t initPos = this->adcGraphErr->GetPointX(0);
  integralValue *= initPos;
  for (Int_t i = 1; i < this->adcGraphErr->GetN(); ++i)
  {
    Double_t y, x;
    this->adcGraphErr->GetPoint(i, x, y);
    Double_t width = x - initPos;
    integralValue += (y - initValue) * width;
    initPos = x;
    if (x <= 880.)
    {
      qDebug() << "ADC counts:" << "X:" << x << " y:" << y << "initValue:" << initValue << "integralValue:" << integralValue;
    }
  }
/*
  QScopedPointer< QFile > dataFile(new QFile("/tmp/DoseChamber_dump.txt"));
  bool res = dataFile->open(QIODevice::WriteOnly | QIODevice::Text);
  if (!res)
  {
    return;
  }

  QTextStream dataFileStream(dataFile.get()); // dump info to temp file for amplitude calibration
  for (Int_t i = 0; i < this->adcGraph->GetN(); ++i)
  {
    Double_t y, x;
    this->adcGraph->GetPoint(i, x, y);
    int yy = int(y);
    dataFileStream << Qt::dec << i << ' ' << Qt::hex << " 0x" << (yy >> CHAR_BIT) << " 0x" << (yy & 0xFF) << " 0x" << yy << Qt::endl;
  }
  dataFileStream.flush();
  res = dataFile->flush();
  if (res)
  {
    dataFile->close();
  }
*/
}

void MainWindow::onFitTestClicked()
{
/*
  if (this->canvas && this->adcGraphErr)
  {
    this->canvas->GetListOfPrimitives()->Remove(this->adcGraphErr.get());
  }
  if (this->canvas && this->fit)
  {
    this->canvas->GetListOfPrimitives()->Remove(this->fit.get());
  }

  this->canvas.reset(new TCanvas("standalone", "Standalone canvas"));
  this->canvas->cd();

  this->adcGraphErr->Draw("AL*");
*/
  this->adcPadErr->cd();
  TF1* form = new TF1("lin", line, 100., 200., 2);
  form->SetParameter(0, 1000.);
  form->SetParLimits(0,-1000., 2000.);
  form->SetParameter(1, 1.);
  form->SetParLimits(1, 0.1, 10.);
  this->adcGraphErr->Fit(form, "RB0");
  form->Draw("SAME");
  this->fit.reset(form);
  this->adcPadErr->Modified();
  this->adcPadErr->Update();
//  canvas->Modified();
//  canvas->Update();
/*
  this->pedFit = std::unique_ptr< TF1 >(new TF1( "pedFit", discharge, 0.9, 5.0, 3));
  this->pedFit->SetParameter(0, 16383.);
  this->pedFit->SetParameter(1, 500.);
  this->pedFit->SetParameter(2, 0.9);
  this->pedFit->SetParNames("U0","tau", "t0");
  this->adcCanvas->cd();
  this->adcPad->cd();
  this->pedGraph->Fit(this->pedFit.get(), "RB0");
  this->pedFit->Draw("same");
  this->adcPad->Update();
  this->adcPad->Modified();
*/
/*
  this->signalFit = std::unique_ptr< TF1 >(new TF1( "sigFit", charge, 128.8, 137.7, 3));
  this->signalFit->SetParameter(0, 16383.);
  this->signalFit->SetParameter(1, 500.);
  this->signalFit->SetParameter(2, 128.8);
  this->signalFit->SetParNames("U0","tau", "t2");
  this->pedCanvas->cd();
  this->pedPad->cd();
  this->pedGraph->Fit(this->signalFit.get(), "RB0");
  this->signalFit->Draw("same");
*/
}

void MainWindow::onSetXray1Clicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '5' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Set x-ray 1:";
    }
  }
}

void MainWindow::onSetXray2Clicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '6' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Set x-ray 2:";
    }
  }
}

void MainWindow::onResetXray12Clicked()
{
  unsigned char buf[COMMAND_BUFFER_SIZE] = { '7' };
  this->lastCommandData = QByteArray( reinterpret_cast<char*>(buf), COMMAND_BUFFER_SIZE);

  if (this->acquisitionPort && this->acquisitionPort->isOpen())
  {
    bool result = this->acquisitionPort->write(this->lastCommandData);
    if (result)
    {
      qDebug() << Q_FUNC_INFO << "Reset x-ray 1 and 2:";
    }
  }
}
