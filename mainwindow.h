#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QAbstractSocket>
#include <QLabel>
#include <QMainWindow>
#include <Qmap>


#include "packet.h"

#include <QtCharts>
QT_CHARTS_USE_NAMESPACE

namespace Ui {
class MainWindow;
}

class CommParaSettingDialog;
class QTcpSocket;
class Callout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    virtual void keyPressEvent(QKeyEvent *ev);

private slots:
    void on_action_P_triggered();

    void on_action_C_triggered();

    void on_action_D_triggered();

    void on_action_I_triggered();

    void IPPortSet(QString ip, quint16 port);

    void receiveDatagram();

    void parse(const quint8 *data);

    void display(const quint8 *data);

    void currentFaultDisplay(const bool faultStatus, quint8 faultCode);

    void showFaultList(const quint8 *data);

    void mapInitial();

    QString FindFaultCode(const quint8 code);

    void on_readPushButton_clicked();

    void on_downloadPushButton_clicked();

    void SyncTimeCycle();

    void on_outPushButton_clicked();

    void checkBool(bool status, QLabel* label);

    void SocketStatusChanged(QAbstractSocket::SocketState socketState);

    void ConfigLoad();

    void labelNameUpdate();

    void WatchData_textChanged(const QString data);//可选通道随输入显示

    void DownloadData_textChanged(const QString data);//可选通道名称显示

    void on_sendPushButton_clicked();

    void lineEditInitial();

    quint8  upperSplit(int word);//16位拆分高低8位

    quint8  lowerSplit(int word);

    void recordData(const quint8 *data);

    void on_recordOutPushButton_clicked();

    void readWatchList();

    void displayVersion();

    void on_selectPushButton_toggled(bool checked);

    void drawChart(const QStringList *data);

    void clickedListWidget();

    void initialChart();//初始化图表

    void keepCallout();

    void tooltip(QPointF point, bool state);

    void setFltTableIndex(int number);

    void initialTableWidget();

    void initialFaultChart();

    void initialFaultSeries();

    void setFaultSeires(int fltGrp, QStringList *FltInfo);

    void on_addDelPushButton_clicked();



private:
    Ui::MainWindow *ui;
    CommParaSettingDialog *m_commParameterSettingDialog;
    QTcpSocket *m_ClientSocket;

    QGraphicsSimpleTextItem *m_coordX;
    QGraphicsSimpleTextItem *m_coordY;

    QTimer *m_cycle;

    QString IPtoConnect;
    quint16 PorttoConnect;
    bool TCPState;

    Packet m_sendPacket;
    Packet m_timePacket;

    int ReadFlag;//1 read 2 download

    bool faultDisplay[82] = {0};//故障显示初始状态

    bool isRecordAlive;//实时记录激活
    int recordIndex;//记录索引
    int maxValue;
    int minValue;

    QStringList BoolLabelName;//加载配置文件
    QStringList WordLabelName;
    QStringList WatchCHName;
    QStringList DownloadCHName;

    QStringList WatchList;//监控界面列表

    quint16 WatchData_uint[5] = {0};
    short WatchData_int[15] = {0};

    QMap<int,QStringList> map;
    QMap<int,QStringList>::iterator i_map;

    QList<QStringList> realList;//实时数据暂存
    QList<QLineSeries *> faultSeriesList;

    QLineSeries *m_series;
    QChart *m_chart;
    QDateTimeAxis *axisX;
    QValueAxis *axisY;
    QChartView *m_chartView;

    QLineSeries *m_faultSeries;
    QChart *m_faultChart;
    QChartView *m_faultChartView;

    Callout *m_tooltip;
    QList<Callout *> m_callouts;
};

#endif // MAINWINDOW_H
