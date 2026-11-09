#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QDateTime>
#include <QByteArray>
#include <QMainWindow>
#include <QSerialPort> // serial port
#include <QScopedPointer>

#include <chrono>
#include <memory>

class QTimer;
class QAbstractButton;
class QDataTime;
class QProgressDialog;
class QCheckBox;
class QListWidgetItem;
class QElapsedTimer;

class TGraph;
class TGraphErrors;
class TPad;
class TCanvas;
class TFile;
class TF1;
class TH1;

#define COMMAND_BUFFER_SIZE 3

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void onAcquisitionDeviceConnectClicked();
  void onAcquisitionDeviceDisconnectClicked();
  void acquisitionSerialPortBytesWritten(qint64);
  void acquisitionSerialPortDataReady();
  void acquisitionSerialPortError(QSerialPort::SerialPortError);
  void onExternalStartClicked();
  void onAcquisitionClicked();
  void onTestClicked();
  void onFitTestClicked();
  void onSetExternalStartClicked();
  void onExternalCounterClicked();
  void onRadioGroupK1Changed(QAbstractButton* b);
  void onRadioGroupK2Changed(QAbstractButton* b);
  void onExternalStartChecked(bool check);
  void onType1Clicked();
  void onType2Clicked();
  void onType3Clicked();
  void onType4Clicked();
  void onSetDelayTimeClicked();
  void onSetDischargeClicked();
  void onSetAcquisitionTimeClicked();
  void onSetSamplesClicked();
  void onSoftResetClicked();
  void onFirstConnectResponce();
  void onInitiateDeviceStopClicked();
  void onInitiateDeviceFinished(int);
  void onTemperaturePressureClicked();
  void onOpenRootFileClicked();
  void onCurrentSpillItemChanged(QListWidgetItem* newItem,QListWidgetItem*);

  void onDcPowerSupplyConnectClicked();
  void dcSerialPortBytesWritten(qint64);
  void dcSerialPortDataReady();
  void dcSerialPortError(QSerialPort::SerialPortError);
  void onSetVoltageClicked();
  void onSetCurrentClicked();
  void onGetVoltageClicked();
  void onGetCurrentClicked();
  void onSetOutputClicked();
  void onSetXray1Clicked();
  void onSetXray2Clicked();
  void onResetXray12Clicked();

private:
  void updateTemperaturePressureCounter();
  void saveAcquisitionData(TGraph*, int samples, double t, double p, int adcCount);
  void saveSettings();
  void loadSettings();
  void enableButtons(bool enable);

  Ui::MainWindow *ui{ nullptr };
  QScopedPointer< QSerialPort > acquisitionPort;
  QScopedPointer< QSerialPort > dcSupplyPort;

  QScopedPointer< QTimer > initiationTimer;
  QScopedPointer< QProgressDialog > initiationProgress;
  QScopedPointer< QElapsedTimer > elapsedTimer;

  QByteArray lastCommandData;
  QByteArray acquisitionPortData;
  QByteArray dcPortData;

  TCanvas* adcCanvas{ nullptr }; // TCanvas
  TPad* adcPad{ nullptr }; // TPad
  std::unique_ptr< TGraph > adcGraph; // TGraph

  TCanvas* adcCanvasErr{ nullptr }; // TCanvas
  TPad* adcPadErr{ nullptr }; // TPad
  std::unique_ptr< TGraphErrors > adcGraphErr; // TGraphError

  TCanvas* pedCanvas{ nullptr }; // TCanvas
  TPad* pedPad{ nullptr }; // TPad
  TGraph* pedGraph{ nullptr }; // TGraph

  TCanvas* signalCanvas{ nullptr }; // TCanvas
  TPad* signalPad{ nullptr }; // TPad
  TGraph* signalGraph{ nullptr }; // TGraph

  std::chrono::steady_clock::time_point timeWaitingBegin, timeWaitingEnd;
  int externalCounter{ -1 };
  int samples1k{ 100 };
  double sensorTemperature{ -1. };
  double sensorPressure{ -1. };
  TFile* rootFile{ nullptr };
  QString rootFileName;
  std::unique_ptr< TF1 > pedFit;
  std::unique_ptr< TF1 > signalFit;
  std::unique_ptr< TF1 > fit;
  std::unique_ptr< TCanvas > canvas;
};

#endif // MAINWINDOW_H
