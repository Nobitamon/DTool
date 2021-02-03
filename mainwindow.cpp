#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "commparasettingdialog.h"
#include "callout.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QDateTime>

#include <QDebug>

#define FAULT_OFFSET 15
#define BOOL_OFFSET 26
#define DATA_OFFSET 30

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_commParameterSettingDialog(new CommParaSettingDialog(this)),
    m_ClientSocket(new QTcpSocket(this)),
    m_cycle(new QTimer(this)),
        IPtoConnect("192.168.1.12"),
        PorttoConnect(1028),
//    IPtoConnect("127.0.0.1"),//local host test
//    PorttoConnect(8080),
    TCPState(false),
    m_sendPacket(Packet::SendDataSize),
    m_timePacket(Packet::SendDataSize),
    isRecordAlive(false),
    m_series(new QLineSeries()),
    m_chart(new QChart()),
    axisX(new QDateTimeAxis),
    axisY(new QValueAxis),
    m_chartView(new QChartView()),
    m_faultSeries(new QLineSeries()),
    m_faultChart(new QChart()),
    m_faultChartView(new QChartView()),
    m_tooltip(0)
{
    ui->setupUi(this);

    //setWindowFlags(windowFlags()&~Qt::WindowMaximizeButtonHint);    //禁止最大化按钮
    //setFixedSize(this->width(),this->height());                     //禁止拖动窗口大小

    ui->stackedWidget->setCurrentIndex(1);//初始界面为未连接界面
    displayVersion();//显示当前软件版本

    m_chart->setAcceptHoverEvents(true);
    //connect(m_series, &QSplineSeries::clicked, this, &MainWindow::keepCallout);
    connect(m_series, &QLineSeries::hovered, this, &MainWindow::tooltip);

    this->setMouseTracking(true);

    ConfigLoad();//加载配置文件
    labelNameUpdate();//标签文本更新
    lineEditInitial();//输入列初始化，只能输入数字并连接信号
    initialTableWidget();//初始化表格
    readWatchList();////加载当前界面布尔量和word量的标签名，用于监控列表选择变量
    initialFaultSeries();
    initialFaultChart();

    ui->readPushButton->setEnabled(false);
    ui->downloadPushButton->setEnabled(false);
    ui->outPushButton->setEnabled(false);
    ui->recordOutPushButton->setEnabled(false);
    ui->selectPushButton->setEnabled(false);

    m_cycle->setInterval(1000);
    connect(m_cycle,&QTimer::timeout,this,&MainWindow::SyncTimeCycle);//时间同步定时器
    connect(m_commParameterSettingDialog, &CommParaSettingDialog::setIPPort, this, &MainWindow::IPPortSet);
    connect(m_ClientSocket,&QAbstractSocket::stateChanged,this, &MainWindow::SocketStatusChanged);
    connect(ui->seletceListWidget,&QListWidget::itemClicked,this, &MainWindow::clickedListWidget);

    //this->grabKeyboard();//获取键盘输入
    mapInitial();//初始化故障map
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::keyPressEvent(QKeyEvent *ev)
{
    switch (ev->key()) {
        case Qt::Key_1:
            setFltTableIndex(1);
        break;
        case Qt::Key_2:
            setFltTableIndex(2);
        break;
        case Qt::Key_3:
            setFltTableIndex(3);
        break;
        case Qt::Key_4:
            setFltTableIndex(4);
        break;
        case Qt::Key_5:
            setFltTableIndex(5);
        break;
        case Qt::Key_6:
            setFltTableIndex(6);
        break;
    }

    QWidget::keyPressEvent(ev);
}

void MainWindow::on_action_P_triggered()    //communication parameters setting dialog trigger
{
    m_commParameterSettingDialog->show();
}

void MainWindow::on_action_C_triggered()    //connect button trigger
{
    if(!TCPState)   //current tcp state judge
    {
        m_ClientSocket->connectToHost(QHostAddress(IPtoConnect),PorttoConnect);
        if(!m_ClientSocket->waitForConnected(3000))
        {
            QMessageBox::information(this,"Connect Fail",m_ClientSocket->errorString());
        }else
        {
            ui->action_C->setIcon(QIcon(":/Prefix/connected.png"));
            TCPState = true;
            connect(m_ClientSocket,&QIODevice::readyRead,this, &MainWindow::receiveDatagram);
            m_cycle->start();
        }
    }
    else
    {
        m_ClientSocket->disconnectFromHost();
        ui->action_C->setIcon(QIcon(":/Prefix/internet-earth-globe-pngrepo-com.png"));
        TCPState = false;
        disconnect(m_ClientSocket,&QIODevice::readyRead,this, &MainWindow::receiveDatagram);
        m_cycle->stop();
    }
}

void MainWindow::on_action_D_triggered()
{
    if(!TCPState)
    {
        QMessageBox::warning(this,"Warning","Connection not found");
    } else {
        int buttonClicked = QMessageBox::question(this,"Question","确认清除存储故障？",QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if(buttonClicked == QMessageBox::Yes)
        {
            quint8 *d = m_timePacket.appData();
            d[0] = 0x55;
            d[1] = 0xAA;
            d[2] = 0xAA;
            d[3] = 0x55;
            d[4] = 1;
            d[5] = 77;
            d[6] = QDateTime::currentDateTime().date().year()%100;
            d[7] = QDateTime::currentDateTime().date().month();
            d[8] = QDateTime::currentDateTime().date().day();
            d[9] = QDateTime::currentDateTime().time().hour();
            d[10] = QDateTime::currentDateTime().time().minute();
            d[11] = QDateTime::currentDateTime().time().second();
            int clear_number = 12;
            for(;clear_number<102;clear_number++) {
                d[clear_number] = 0;
            }
            d[12] = 2;
            d[13] = 43;
            m_ClientSocket->write(m_timePacket);
        }
    }
}

void MainWindow::on_action_I_triggered()
{
   int buttonClicked = QMessageBox::question(this,"Question","导入文件将会覆盖当前下载的故障数据，是否继续？",QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
   if(buttonClicked == QMessageBox::Yes)
   {
       QFileDialog* inFileDialog = new QFileDialog(this);
       QString inFileName = inFileDialog->getOpenFileName(this,tr("导入文件"),"",tr("Excel(*.csv)"));
       if(inFileName == "")
           return;
       QDir dir = QDir::current();
       QFile infile(dir.filePath(inFileName));
       if(!infile.open(QIODevice::ReadOnly))
           ui->resultLabel->setText(tr("导入文件失败!"));
       else
           ui->resultLabel->setText(tr("导入文件成功!"));
       QTextStream* in = new QTextStream(&infile);
       in->setCodec("UTF-8");


       mapInitial();
       initialFaultSeries();
       QStringList rowData = in->readAll().split("\n");
       QStringList cellData;
       for (int i = 1 ; i < rowData.count() ; i++)
       {
           cellData = rowData.at(i).split(",");           //一行中的单元格以，区分
           if(cellData.count() < 10)
               continue;
           map.insert(cellData.at(0).toInt(),cellData);
           setFaultSeires(cellData.at(0).toInt(),&cellData);
           qDebug()<<cellData.at(0);
       }
       infile.close();
   }
}

void MainWindow::IPPortSet(QString ip, quint16 port)
{
    IPtoConnect = ip;
    PorttoConnect = port;
}

void MainWindow::receiveDatagram()//receive
{
    QByteArray dg;
    dg.resize(Packet::ReceiveDataSize);
    while(m_ClientSocket->bytesAvailable() != 0) {
        m_ClientSocket->read(dg.data(),dg.size());
        Packet pkt(dg);//check
        if(!pkt.isValid())
            return;

        const quint8 *data = pkt.appData();

        //display
        if((data[4] << 8 | data[5]) == 0xFFFF)  {
            display(data);
            if(isRecordAlive == true)
                recordData(data);
            continue;
        }

        //show fault list
        if(ReadFlag == 1) {
            showFaultList(data);
            if(ui->faultListTableWidget->rowCount() > 0) {
                ui->downloadPushButton->setEnabled(true);
            }
            continue;
        }

        //parse data
        if(ReadFlag == 2) {
            parse(data);
            if(map.count() == 0) {
                ui->resultLabel->setText("下载不成功！无故障数据");
            } else {
                ui->outPushButton->setEnabled(true);
                ui->resultLabel->setText("下载成功！当前已下载" + QString::number(map.count())+ "组数据");
            }
        }
    }

}

void MainWindow::parse(const quint8 *data)
{
    QStringList *FaultInfo = new QStringList;
    int  FaultGroup = data[4] << 8 | data[5];
    //FaultInfo->append("故障序号：" + QString::number(data[4] << 8 | data[5]));
    FaultInfo->append(QString::number(FaultGroup));
    FaultInfo->append("20" + QString::number(data[6]) + "年" + QString::number(data[7]) + "月" + QString::number(data[8]) + "日" + QString::number(data[9]) + "时" +QString::number(data[10]) + "分" + QString::number(data[11]) + "秒");
    FaultInfo->append(FindFaultCode(data[13]));
    QString existFault = "";
    if((data[15] & 0x1) == 1) existFault += tr("系统主电路正极接地 ");
    if((data[15] >> 1 & 0x1) == 1) existFault += tr("系统主电路负极接地 ");
    if((data[15] >> 2 & 0x1) == 1) existFault += tr("（牵引）电网电压过高 ");
    if((data[15] >> 3 & 0x1) == 1) existFault += tr("（牵引）电网电压过低 ");
    if((data[15] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）中间电压过高 ");
    if((data[15] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）中间电压过低 ");
    if((data[15] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）线路接触器卡分 ");
    if((data[15] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）线路接触器卡合 ");

    if((data[16] & 0x1) == 1) existFault += tr("（牵引MC1）充电接触器卡分 ");
    if((data[16] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）充电接触器卡合 ");
    if((data[16] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1） 直流输入过流 ");
    if((data[16] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1） 预充电超时 ");
    if((data[16] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1） VLU开路 ");
    if((data[16] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）逆变模块温度高 ");
    if((data[16] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）逆变模块温度过高 ");
    if((data[16] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1） 线路滤波器温度高 ");

    if((data[17] & 0x1) == 1) existFault += tr("（牵引MC1） 线路滤波器温度过高 ");
    if((data[17] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）中间电压过高 ");
    if((data[17] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）中间电压过低 ");
    if((data[17] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2）线路接触器卡分 ");
    if((data[17] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2）线路接触器卡合 ");
    if((data[17] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）充电接触器卡分 ");
    if((data[17] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）充电接触器卡合 ");
    if((data[17] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2） 直流输入过流 ");

    if((data[18] & 0x1) == 1) existFault += tr("（牵引MC2） 预充电超时 ");
    if((data[18] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2） DC-link短路 ");
    if((data[18] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2） VLU开路 ");
    if((data[18] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2） 逆变模块温度高 ");
    if((data[18] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2） 逆变模块温度过高 ");
    if((data[18] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2） 线路滤波器温度高 ");
    if((data[18] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2） 线路滤波器温度过高 ");
    if((data[18] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）定子频率过高 ");

    if((data[19] & 0x1) == 1) existFault += tr("（牵引MC1）电机电流不平衡 ");
    if((data[19] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）电机1温度高 ");
    if((data[19] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1）电机1温度过高 ");
    if((data[19] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1）电机2温度高 ");
    if((data[19] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）电机2温度过高 ");
    if((data[19] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）IGBT-故障 ");
    if((data[19] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）电机过流 ");
    if((data[19] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）空转/滑行 ");

    if((data[20] & 0x1) == 1) existFault += tr("（牵引MC2）定子频率过高 ");
    if((data[20] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）电机电流不平衡 ");
    if((data[20] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）电机3温度高 ");
    if((data[20] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2）电机3温度过高 ");
    if((data[20] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2）电机4温度高 ");
    if((data[20] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）电机4温度过高 ");
    if((data[20] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）光纤故障 ");
    if((data[20] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）IGBT-故障 ");

    if((data[21] & 0x1) == 1) existFault += tr("（牵引MC2）电机过流 ");
    if((data[21] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）空转/滑行 ");
    if((data[21] >> 2 & 0x1) == 1) existFault += tr("（牵引）冷却液液位低 ");
    if((data[21] >> 3 & 0x1) == 1) existFault += tr("（牵引）冷却液液位过低 ");
    if((data[21] >> 4 & 0x1) == 1) existFault += tr("（牵引）冷却液不流通或流量故障 ");
    if((data[21] >> 5 & 0x1) == 1) existFault += tr("（牵引）水管温度过高 ");
    if((data[21] >> 6 & 0x1) == 1) existFault += tr("（牵引）冷却系统供电断路器断开 ");
    if((data[21] >> 8 & 0x1) == 1) existFault += tr("（牵引）CANopen通讯故障 ");

    if((data[22] & 0x1) == 1) existFault += tr("车辆牵引使能丢失 ");
    if((data[22] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）单个速度与列车参考速度差距太大 ");
    if((data[22] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）单个速度与列车参考速度差距太大 ");
    if((data[22] >> 3 & 0x1) == 1) existFault += tr("M1速度传感器故障 ");
    if((data[22] >> 4 & 0x1) == 1) existFault += tr("M2速度传感器故障 ");
    if((data[22] >> 5 & 0x1) == 1) existFault += tr("M3速度传感器故障 ");
    if((data[22] >> 6 & 0x1) == 1) existFault += tr("M4速度传感器故障 ");
    if((data[22] >> 8 & 0x1) == 1) existFault += tr("所有速度传感器故障 ");

    if((data[23] & 0x1) == 1) existFault += tr("轮径值超范围 ");
    if((data[23] >> 1 & 0x1) == 1) existFault += tr("向前向后指令同时有效 ");
    if((data[23] >> 2 & 0x1) == 1) existFault += tr("牵引制动指令同时存在 ");
    if((data[23] >> 3 & 0x1) == 1) existFault += tr("列车超速保护 ");
    if((data[23] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）U相故障 ");
    if((data[23] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）V相故障 ");
    if((data[23] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）W相故障 ");
    if((data[23] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）U相故障 ");

    if((data[24] & 0x1) == 1) existFault += tr("（牵引MC2）V相故障 ");
    if((data[24] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）W相故障 ");
    if((data[24] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1）U相过流 ");
    if((data[24] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1）V相过流 ");
    if((data[24] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）W相过流 ");
    if((data[24] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）U相过流 ");
    if((data[24] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）V相过流 ");
    if((data[24] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）W相过流 ");

    if((data[25] & 0x1) == 1) existFault += tr("（牵引MC1）硬件或驱动故障 ");
    if((data[25] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）硬件或驱动故障 ");
    FaultInfo->append(existFault);


    FaultInfo->append(QString::number(data[26] & 0x1));
    FaultInfo->append(QString::number(data[26] >> 1 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 2 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 3 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 4 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 5 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 6 & 0x1));
    FaultInfo->append(QString::number(data[26] >> 7 & 0x1));

    FaultInfo->append(QString::number(data[27] & 0x1));
    FaultInfo->append(QString::number(data[27] >> 1 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 2 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 3 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 4 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 5 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 6 & 0x1));
    FaultInfo->append(QString::number(data[27] >> 7 & 0x1));

    FaultInfo->append(QString::number(data[28] & 0x1));
    FaultInfo->append(QString::number(data[28] >> 1 & 0x1));
    FaultInfo->append(QString::number(data[28] >> 2 & 0x1));
    FaultInfo->append(QString::number(data[28] >> 3 & 0x1));

    FaultInfo->append(QString::number(data[29] & 0x1));
    FaultInfo->append(QString::number(data[29] >> 1 & 0x1));
    FaultInfo->append(QString::number(data[29] >> 2 & 0x1));
    FaultInfo->append(QString::number(data[29] >> 3 & 0x1));
    FaultInfo->append(QString::number(data[29] >> 4 & 0x1));
    FaultInfo->append(QString::number(data[29] >> 5 & 0x1));
    FaultInfo->append(QString::number(data[29] >> 6 & 0x1));

    FaultInfo->append(QString::number((data[DATA_OFFSET] << 8 | data[DATA_OFFSET + 1]) - 100));
    double temp = data[DATA_OFFSET + 2] << 8 | data[DATA_OFFSET + 3];
    FaultInfo->append(QString::number(temp/100));
    temp = data[34] << 8 | data[35];
    FaultInfo->append(QString::number(temp/100));
    temp = data[36] << 8 | data[37];
    FaultInfo->append(QString::number(temp/100));
    temp = data[38] << 8 | data[39];
    FaultInfo->append(QString::number(temp/100));
    temp = data[40] << 8 | data[41];
    FaultInfo->append(QString::number(temp/2));
    temp = data[42] << 8 | data[43];
    FaultInfo->append(QString::number(temp/2));
    temp = data[44] << 8 | data[45];
    FaultInfo->append(QString::number(temp/2));
    temp = data[46]<< 8 | data[47];
    FaultInfo->append(QString::number(temp/2));
    quint16 temp_uint = data[48] << 8 | data[49];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[50] << 8 | data[51];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[52] << 8 | data[53];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[54] << 8 | data[55];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[56] << 8 | data[57];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[58] << 8 | data[59];
    FaultInfo->append(QString::number(temp_uint));
    temp_uint = data[60] << 8 | data[61];
    FaultInfo->append(QString::number(temp_uint));
    short temp_int = data[62] << 8 | data[63];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[64] << 8 | data[65];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[66] << 8 | data[67];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[68] << 8 | data[69];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[70] << 8 | data[71];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[72] << 8 | data[73];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[74] << 8 | data[75];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[76] << 8 | data[77];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[78] << 8 | data[79];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[80] << 8 | data[81];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[82] << 8 | data[83];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[84] << 8 | data[85];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[86] << 8 | data[87];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[88] << 8 | data[89];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[90] << 8 | data[91];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[92] << 8 | data[93];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[94] << 8 | data[95];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[96] << 8 | data[97];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[98] << 8 | data[99];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[100] << 8 | data[101];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[102] << 8 | data[103];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[104] << 8 | data[105];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[106] << 8 | data[107];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[108] << 8 | data[109];
    FaultInfo->append(QString::number(temp_int));
    temp_int = data[110] << 8 | data[111];
    FaultInfo->append(QString::number(temp_int));

    map.insert(FaultGroup,*FaultInfo);
    setFaultSeires(FaultGroup, FaultInfo);
}

void MainWindow::display(const quint8 *data)//display
{   bool temp_bool = false;
    temp_bool = data[FAULT_OFFSET] & 0x1;   currentFaultDisplay(temp_bool,1);
    temp_bool = data[FAULT_OFFSET] >> 1 & 0x1; currentFaultDisplay(temp_bool,2);
    temp_bool = data[FAULT_OFFSET] >> 2 & 0x1; currentFaultDisplay(temp_bool,3);
    temp_bool = data[FAULT_OFFSET] >> 3 & 0x1; currentFaultDisplay(temp_bool,4);
    temp_bool = data[FAULT_OFFSET] >> 4 & 0x1; currentFaultDisplay(temp_bool,5);
    temp_bool = data[FAULT_OFFSET] >> 5 & 0x1; currentFaultDisplay(temp_bool,6);
    temp_bool = data[FAULT_OFFSET] >> 6 & 0x1; currentFaultDisplay(temp_bool,7);
    temp_bool = data[FAULT_OFFSET] >> 7 & 0x1; currentFaultDisplay(temp_bool,8);

    temp_bool = data[FAULT_OFFSET + 1] & 0x1; currentFaultDisplay(temp_bool,9);
    temp_bool = data[FAULT_OFFSET + 1] >> 1 & 0x1; currentFaultDisplay(temp_bool,10);
    temp_bool = data[FAULT_OFFSET + 1] >> 2 & 0x1; currentFaultDisplay(temp_bool,11);
    temp_bool = data[FAULT_OFFSET + 1] >> 3 & 0x1; currentFaultDisplay(temp_bool,12);
    temp_bool = data[FAULT_OFFSET + 1] >> 4 & 0x1; currentFaultDisplay(temp_bool,13);
    temp_bool = data[FAULT_OFFSET + 1] >> 5 & 0x1; currentFaultDisplay(temp_bool,14);
    temp_bool = data[FAULT_OFFSET + 1] >> 6 & 0x1; currentFaultDisplay(temp_bool,15);
    temp_bool = data[FAULT_OFFSET + 1] >> 7 & 0x1; currentFaultDisplay(temp_bool,16);

    temp_bool = data[FAULT_OFFSET + 2] & 0x1; currentFaultDisplay(temp_bool,17);
    temp_bool = data[FAULT_OFFSET + 2] >> 1 & 0x1; currentFaultDisplay(temp_bool,18);
    temp_bool = data[FAULT_OFFSET + 2] >> 2 & 0x1; currentFaultDisplay(temp_bool,19);
    temp_bool = data[FAULT_OFFSET + 2] >> 3 & 0x1; currentFaultDisplay(temp_bool,20);
    temp_bool = data[FAULT_OFFSET + 2] >> 4 & 0x1; currentFaultDisplay(temp_bool,21);
    temp_bool = data[FAULT_OFFSET + 2] >> 5 & 0x1; currentFaultDisplay(temp_bool,22);
    temp_bool = data[FAULT_OFFSET + 2] >> 6 & 0x1; currentFaultDisplay(temp_bool,23);
    temp_bool = data[FAULT_OFFSET + 2] >> 7 & 0x1; currentFaultDisplay(temp_bool,24);

    temp_bool = data[FAULT_OFFSET + 3] & 0x1; currentFaultDisplay(temp_bool,25);
    temp_bool = data[FAULT_OFFSET + 3] >> 1 & 0x1; currentFaultDisplay(temp_bool,26);
    temp_bool = data[FAULT_OFFSET + 3] >> 2 & 0x1; currentFaultDisplay(temp_bool,27);
    temp_bool = data[FAULT_OFFSET + 3] >> 3 & 0x1; currentFaultDisplay(temp_bool,28);
    temp_bool = data[FAULT_OFFSET + 3] >> 4 & 0x1; currentFaultDisplay(temp_bool,29);
    temp_bool = data[FAULT_OFFSET + 3] >> 5 & 0x1; currentFaultDisplay(temp_bool,30);
    temp_bool = data[FAULT_OFFSET + 3] >> 6 & 0x1; currentFaultDisplay(temp_bool,31);
    temp_bool = data[FAULT_OFFSET + 3] >> 7 & 0x1; currentFaultDisplay(temp_bool,32);

    temp_bool = data[FAULT_OFFSET + 4] & 0x1; currentFaultDisplay(temp_bool,33);
    temp_bool = data[FAULT_OFFSET + 4] >> 1 & 0x1; currentFaultDisplay(temp_bool,34);
    temp_bool = data[FAULT_OFFSET + 4] >> 2 & 0x1; currentFaultDisplay(temp_bool,35);
    temp_bool = data[FAULT_OFFSET + 4] >> 3 & 0x1; currentFaultDisplay(temp_bool,36);
    temp_bool = data[FAULT_OFFSET + 4] >> 4 & 0x1; currentFaultDisplay(temp_bool,37);
    temp_bool = data[FAULT_OFFSET + 4] >> 5 & 0x1; currentFaultDisplay(temp_bool,38);
    temp_bool = data[FAULT_OFFSET + 4] >> 6 & 0x1; currentFaultDisplay(temp_bool,39);
    temp_bool = data[FAULT_OFFSET + 4] >> 7 & 0x1; currentFaultDisplay(temp_bool,40);

    temp_bool = data[FAULT_OFFSET + 5] & 0x1; currentFaultDisplay(temp_bool,41);
    temp_bool = data[FAULT_OFFSET + 5] >> 1 & 0x1; currentFaultDisplay(temp_bool,42);
    temp_bool = data[FAULT_OFFSET + 5] >> 2 & 0x1; currentFaultDisplay(temp_bool,43);
    temp_bool = data[FAULT_OFFSET + 5] >> 3 & 0x1; currentFaultDisplay(temp_bool,44);
    temp_bool = data[FAULT_OFFSET + 5] >> 4 & 0x1; currentFaultDisplay(temp_bool,45);
    temp_bool = data[FAULT_OFFSET + 5] >> 5 & 0x1; currentFaultDisplay(temp_bool,46);
    temp_bool = data[FAULT_OFFSET + 5] >> 6 & 0x1; currentFaultDisplay(temp_bool,47);
    temp_bool = data[FAULT_OFFSET + 5] >> 7 & 0x1; currentFaultDisplay(temp_bool,48);

    temp_bool = data[FAULT_OFFSET + 6] & 0x1; currentFaultDisplay(temp_bool,49);
    temp_bool = data[FAULT_OFFSET + 6] >> 1 & 0x1; currentFaultDisplay(temp_bool,50);
    temp_bool = data[FAULT_OFFSET + 6] >> 2 & 0x1; currentFaultDisplay(temp_bool,51);
    temp_bool = data[FAULT_OFFSET + 6] >> 3 & 0x1; currentFaultDisplay(temp_bool,52);
    temp_bool = data[FAULT_OFFSET + 6] >> 4 & 0x1; currentFaultDisplay(temp_bool,53);
    temp_bool = data[FAULT_OFFSET + 6] >> 5 & 0x1; currentFaultDisplay(temp_bool,54);
    temp_bool = data[FAULT_OFFSET + 6] >> 6 & 0x1; currentFaultDisplay(temp_bool,55);
    temp_bool = data[FAULT_OFFSET + 6] >> 7 & 0x1; currentFaultDisplay(temp_bool,56);

    temp_bool = data[FAULT_OFFSET + 7] & 0x1; currentFaultDisplay(temp_bool,57);
    temp_bool = data[FAULT_OFFSET + 7] >> 1 & 0x1; currentFaultDisplay(temp_bool,58);
    temp_bool = data[FAULT_OFFSET + 7] >> 2 & 0x1; currentFaultDisplay(temp_bool,59);
    temp_bool = data[FAULT_OFFSET + 7] >> 3 & 0x1; currentFaultDisplay(temp_bool,60);
    temp_bool = data[FAULT_OFFSET + 7] >> 4 & 0x1; currentFaultDisplay(temp_bool,61);
    temp_bool = data[FAULT_OFFSET + 7] >> 5 & 0x1; currentFaultDisplay(temp_bool,62);
    temp_bool = data[FAULT_OFFSET + 7] >> 6 & 0x1; currentFaultDisplay(temp_bool,63);
    temp_bool = data[FAULT_OFFSET + 7] >> 7 & 0x1; currentFaultDisplay(temp_bool,64);

    temp_bool = data[FAULT_OFFSET + 8] & 0x1; currentFaultDisplay(temp_bool,65);
    temp_bool = data[FAULT_OFFSET + 8] >> 1 & 0x1; currentFaultDisplay(temp_bool,66);
    temp_bool = data[FAULT_OFFSET + 8] >> 2 & 0x1; currentFaultDisplay(temp_bool,67);
    temp_bool = data[FAULT_OFFSET + 8] >> 3 & 0x1; currentFaultDisplay(temp_bool,68);
    temp_bool = data[FAULT_OFFSET + 8] >> 4 & 0x1; currentFaultDisplay(temp_bool,69);
    temp_bool = data[FAULT_OFFSET + 8] >> 5 & 0x1; currentFaultDisplay(temp_bool,70);
    temp_bool = data[FAULT_OFFSET + 8] >> 6 & 0x1; currentFaultDisplay(temp_bool,71);
    temp_bool = data[FAULT_OFFSET + 8] >> 7 & 0x1; currentFaultDisplay(temp_bool,72);

    temp_bool = data[FAULT_OFFSET + 9] & 0x1; currentFaultDisplay(temp_bool,73);
    temp_bool = data[FAULT_OFFSET + 9] >> 1 & 0x1; currentFaultDisplay(temp_bool,74);
    temp_bool = data[FAULT_OFFSET + 9] >> 2 & 0x1; currentFaultDisplay(temp_bool,75);
    temp_bool = data[FAULT_OFFSET + 9] >> 3 & 0x1; currentFaultDisplay(temp_bool,76);
    temp_bool = data[FAULT_OFFSET + 9] >> 4 & 0x1; currentFaultDisplay(temp_bool,77);
    temp_bool = data[FAULT_OFFSET + 9] >> 5 & 0x1; currentFaultDisplay(temp_bool,78);
    temp_bool = data[FAULT_OFFSET + 9] >> 6 & 0x1; currentFaultDisplay(temp_bool,79);
    temp_bool = data[FAULT_OFFSET + 9] >> 7 & 0x1; currentFaultDisplay(temp_bool,80);

    temp_bool = data[FAULT_OFFSET + 10] & 0x1; currentFaultDisplay(temp_bool,81);
    temp_bool = data[FAULT_OFFSET + 10] >> 1 & 0x1; currentFaultDisplay(temp_bool,82);

    checkBool(data[BOOL_OFFSET] & 0x1,ui->boolLabel_0);//bool data
    checkBool(data[BOOL_OFFSET] >> 1 & 0x1,ui->boolLabel_1);
    checkBool(data[BOOL_OFFSET] >> 2 & 0x1,ui->boolLabel_2);
    checkBool(data[BOOL_OFFSET] >> 3 & 0x1,ui->boolLabel_3);
    checkBool(data[BOOL_OFFSET] >> 4 & 0x1,ui->boolLabel_4);
    checkBool(data[BOOL_OFFSET] >> 5 & 0x1,ui->boolLabel_5);
    checkBool(data[BOOL_OFFSET] >> 6 & 0x1,ui->boolLabel_6);
    checkBool(data[BOOL_OFFSET] >> 7 & 0x1,ui->boolLabel_7);

    checkBool(data[BOOL_OFFSET + 1] & 0x1,ui->boolLabel_8);
    checkBool(data[BOOL_OFFSET + 1] >> 1 & 0x1,ui->boolLabel_9);
    checkBool(data[BOOL_OFFSET + 1] >> 2 & 0x1,ui->boolLabel_10);
    checkBool(data[BOOL_OFFSET + 1] >> 3 & 0x1,ui->boolLabel_11);
    checkBool(data[BOOL_OFFSET + 1] >> 4 & 0x1,ui->boolLabel_12);
    checkBool(data[BOOL_OFFSET + 1] >> 5 & 0x1,ui->boolLabel_13);
    checkBool(data[BOOL_OFFSET + 1] >> 6 & 0x1,ui->boolLabel_14);
    checkBool(data[BOOL_OFFSET + 1] >> 7 & 0x1,ui->boolLabel_15);

    checkBool(data[BOOL_OFFSET + 2] & 0x1,ui->boolLabel_16);
    checkBool(data[BOOL_OFFSET + 2] >> 1 & 0x1,ui->boolLabel_17);
    checkBool(data[BOOL_OFFSET + 2] >> 2 & 0x1,ui->boolLabel_18);
    checkBool(data[BOOL_OFFSET + 2] >> 3 & 0x1,ui->boolLabel_19);
    checkBool(data[BOOL_OFFSET + 3] & 0x1,ui->boolLabel_20);
    checkBool(data[BOOL_OFFSET + 3] >> 1 & 0x1,ui->boolLabel_21);
    checkBool(data[BOOL_OFFSET + 3] >> 2 & 0x1,ui->boolLabel_22);
    checkBool(data[BOOL_OFFSET + 3] >> 3 & 0x1,ui->boolLabel_23);
    checkBool(data[BOOL_OFFSET + 3] >> 4 & 0x1,ui->boolLabel_24);
    checkBool(data[BOOL_OFFSET + 3] >> 5 & 0x1,ui->boolLabel_25);
    checkBool(data[BOOL_OFFSET + 3] >> 6 & 0x1,ui->boolLabel_26);

    ui->wordValueLabel_0->setText(QString::number((data[DATA_OFFSET] << 8 | data[DATA_OFFSET + 1]) - 100) + "%");
    double temp = data[DATA_OFFSET + 2] << 8 | data[DATA_OFFSET + 3];
    ui->wordValueLabel_1->setText(QString::number(temp/100) + "kN");
    temp = data[DATA_OFFSET + 4] << 8 | data[DATA_OFFSET + 5];
    ui->wordValueLabel_2->setText(QString::number(temp/100) + "kN");
    temp = data[DATA_OFFSET + 6] << 8 | data[DATA_OFFSET + 7];
    ui->wordValueLabel_3->setText(QString::number(temp/100) + "kN");
    temp = data[DATA_OFFSET + 8] << 8 | data[DATA_OFFSET + 9];
    ui->wordValueLabel_4->setText(QString::number(temp/100) + "kN");
    double temp2 = data[DATA_OFFSET + 10] << 8 | data[DATA_OFFSET + 11];
    ui->wordValueLabel_5->setText(QString::number(temp2/2) + "km/h");
    temp2 = data[DATA_OFFSET + 12] << 8 | data[DATA_OFFSET + 13];
    ui->wordValueLabel_6->setText(QString::number(temp2/2) + "km/h");
    temp2 = data[DATA_OFFSET + 14] << 8 | data[DATA_OFFSET + 15];
    ui->wordValueLabel_7->setText(QString::number(temp2/2) + "km/h");
    temp2 = data[DATA_OFFSET + 16] << 8 | data[DATA_OFFSET + 17];
    ui->wordValueLabel_8->setText(QString::number(temp2/2) + "km/h");
    quint16 temp_uint = data[DATA_OFFSET + 18] << 8 | data[DATA_OFFSET + 19];
    ui->wordValueLabel_9->setText(QString::number(temp_uint) + "A");
    temp_uint = data[DATA_OFFSET + 20] << 8 | data[DATA_OFFSET + 21];
    ui->wordValueLabel_10->setText(QString::number(temp_uint) + "A");
    temp_uint = data[DATA_OFFSET + 22] << 8 | data[DATA_OFFSET + 23];
    ui->wordValueLabel_11->setText(QString::number(temp_uint) + "V");
    temp_uint = data[DATA_OFFSET + 24] << 8 | data[DATA_OFFSET + 25];
    ui->wordValueLabel_12->setText(QString::number(temp_uint) + "V");
    temp_uint = data[DATA_OFFSET + 26] << 8 | data[DATA_OFFSET + 27];
    ui->wordValueLabel_13->setText(QString::number(temp_uint) + "V");
    temp_uint = data[DATA_OFFSET + 28] << 8 | data[DATA_OFFSET + 29];
    ui->wordValueLabel_14->setText(QString::number(temp_uint) + "A");
    temp_uint = data[DATA_OFFSET + 30] << 8 | data[DATA_OFFSET + 31];
    ui->wordValueLabel_15->setText(QString::number(temp_uint) + "A");

    short temp_short = data[DATA_OFFSET + 32] << 8 | data[DATA_OFFSET + 33];
    ui->wordValueLabel_16->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 34] << 8 | data[DATA_OFFSET + 35];
    ui->wordValueLabel_17->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 36] << 8 | data[DATA_OFFSET + 37];
    ui->wordValueLabel_18->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 38] << 8 | data[DATA_OFFSET + 39];
    ui->wordValueLabel_19->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 40] << 8 | data[DATA_OFFSET + 41];
    ui->wordValueLabel_20->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 42] << 8 | data[DATA_OFFSET + 43];
    ui->wordValueLabel_21->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 44] << 8 | data[DATA_OFFSET + 45];
    ui->wordValueLabel_22->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 46] << 8 | data[DATA_OFFSET + 47];
    ui->wordValueLabel_23->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 48] << 8 | data[DATA_OFFSET + 49];
    ui->wordValueLabel_24->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 50] << 8 | data[DATA_OFFSET + 51];
    ui->wordValueLabel_25->setText(QString::number(temp_short) + "A");
    temp_short = data[DATA_OFFSET + 52] << 8 | data[DATA_OFFSET + 53];
    ui->wordValueLabel_26->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 54] << 8 | data[DATA_OFFSET + 55];
    ui->wordValueLabel_27->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 56] << 8 | data[DATA_OFFSET + 57];
    ui->wordValueLabel_28->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 58] << 8 | data[DATA_OFFSET + 59];
    ui->wordValueLabel_29->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 60] << 8 | data[DATA_OFFSET + 61];
    ui->wordValueLabel_30->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 62] << 8 | data[DATA_OFFSET + 63];
    ui->wordValueLabel_31->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 64] << 8 | data[DATA_OFFSET + 65];
    ui->wordValueLabel_32->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 66] << 8 | data[DATA_OFFSET + 67];
    ui->wordValueLabel_33->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 68] << 8 | data[DATA_OFFSET + 69];
    ui->wordValueLabel_34->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 70] << 8 | data[DATA_OFFSET + 71];
    ui->wordValueLabel_35->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 72] << 8 | data[DATA_OFFSET + 73];
    ui->wordValueLabel_36->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 74] << 8 | data[DATA_OFFSET + 75];
    ui->wordValueLabel_37->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 76] << 8 | data[DATA_OFFSET + 77];
    ui->wordValueLabel_38->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 78] << 8 | data[DATA_OFFSET + 79];
    ui->wordValueLabel_39->setText(QString::number(temp_short) + "℃");
    temp_short = data[DATA_OFFSET + 80] << 8 | data[DATA_OFFSET + 81];
    ui->wordValueLabel_40->setText(QString::number(temp_short) + "℃");

    temp_uint = data[DATA_OFFSET + 92] << 8 | data[DATA_OFFSET + 93];//观测通道数组赋值
    ui->chEditValue_0->setText(QString::number(temp_uint));
    temp_uint = data[DATA_OFFSET + 94] << 8 | data[DATA_OFFSET + 95];
    ui->chEditValue_1->setText(QString::number(temp_uint));
    temp_uint = data[DATA_OFFSET + 96] << 8 | data[DATA_OFFSET + 97];
    ui->chEditValue_2->setText(QString::number(temp_uint));
    temp_uint = data[DATA_OFFSET + 98] << 8 | data[DATA_OFFSET + 99];
    ui->chEditValue_3->setText(QString::number(temp_uint));
    temp_uint = data[DATA_OFFSET + 100] << 8 | data[DATA_OFFSET + 101];
    ui->chEditValue_4->setText(QString::number(temp_uint));
    temp_short = data[DATA_OFFSET + 102] << 8 | data[DATA_OFFSET + 103];
    ui->chEditValue_5->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 104] << 8 | data[DATA_OFFSET + 105];
    ui->chEditValue_6->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 106] << 8 | data[DATA_OFFSET + 107];
    ui->chEditValue_7->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 108] << 8 | data[DATA_OFFSET + 109];
    ui->chEditValue_8->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 110] << 8 | data[DATA_OFFSET + 111];
    ui->chEditValue_9->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 112] << 8 | data[DATA_OFFSET + 113];
    ui->chEditValue_10->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 114] << 8 | data[DATA_OFFSET + 115];
    ui->chEditValue_11->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 116] << 8 | data[DATA_OFFSET + 117];
    ui->chEditValue_12->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 118] << 8 | data[DATA_OFFSET + 119];
    ui->chEditValue_13->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 120] << 8 | data[DATA_OFFSET + 121];
    ui->chEditValue_14->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 122] << 8 | data[DATA_OFFSET + 123];
    ui->chEditValue_15->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 124] << 8 | data[DATA_OFFSET + 125];
    ui->chEditValue_16->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 126] << 8 | data[DATA_OFFSET + 127];
    ui->chEditValue_17->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 128] << 8 | data[DATA_OFFSET + 129];
    ui->chEditValue_18->setText(QString::number(temp_short));
    temp_short = data[DATA_OFFSET + 130] << 8 | data[DATA_OFFSET + 131];
    ui->chEditValue_38->setText(QString::number(temp_short));
}

void MainWindow::currentFaultDisplay(const bool faultStatus, quint8 faultCode)
{
    //将当前故障位状态与历史故障位状态比较输出
    if(faultStatus == faultDisplay[faultCode-1])
        return;
    else if (faultStatus == 0) {
        faultDisplay[faultCode-1] = false;
    }   else {
        faultDisplay[faultCode-1] = true;
        ui->FaultListWidget->addItem(QDateTime::currentDateTime().time().toString() +"\t" + FindFaultCode(faultCode));
    }
}

void MainWindow::showFaultList(const quint8 *data)
{
    ui->faultListTableWidget->insertRow(ui->faultListTableWidget->rowCount());
    ui->faultListTableWidget->setItem(ui->faultListTableWidget->rowCount()-1,0,new QTableWidgetItem(QString::number(data[13])));
    ui->faultListTableWidget->item(ui->faultListTableWidget->rowCount()-1,0)->setTextAlignment(Qt::AlignCenter);
    ui->faultListTableWidget->setItem(ui->faultListTableWidget->rowCount()-1,1,new QTableWidgetItem("20" + QString::number(data[6]) + "年" + QString::number(data[7]) + "月" + QString::number(data[8]) + "日" + QString::number(data[9]) + "时" +QString::number(data[10]) + "分" + QString::number(data[11]) + "秒"));
    ui->faultListTableWidget->item(ui->faultListTableWidget->rowCount()-1,1)->setTextAlignment(Qt::AlignCenter);
    ui->faultListTableWidget->setItem(ui->faultListTableWidget->rowCount()-1,2,new QTableWidgetItem(FindFaultCode(data[13])));
    ui->faultListTableWidget->item(ui->faultListTableWidget->rowCount()-1,2)->setTextAlignment(Qt::AlignCenter);

}

void MainWindow::mapInitial()
{
    map.clear();
}

QString MainWindow::FindFaultCode(const quint8 code)
{
    switch (code) {
    case 1:
        return "系统主电路正极接地";
    case 2:
        return "系统主电路负极接地";
    case 3:
        return "（牵引）电网电压过高";
    case 4:
        return "（牵引）电网电压过低";
    case 5:
        return "（牵引MC1）中间电压过高";
    case 6:
        return "（牵引MC1）中间电压过低";
    case 7:
        return "（牵引MC1）线路接触器卡分";
    case 8:
        return "（牵引MC1）线路接触器卡合";
    case 9:
        return "（牵引MC1）充电接触器卡分";
    case 10:
        return "（牵引MC1）充电接触器卡合";
    case 11:
        return "（牵引MC1）直流输入过流";
    case 12:
        return "（牵引MC1）预充电超时";
    case 13:
        return "（牵引MC1）VLU开路";
    case 14:
        return "（牵引MC1）逆变模块温度高";
    case 15:
        return "（牵引MC1）逆变模块温度过高";
    case 16:
        return "（牵引MC1）线路滤波器温度高";
    case 17:
        return "（牵引MC1）线路滤波器温度过高";
    case 18:
        return "（牵引MC2）中间电压过高";
    case 19:
        return "（牵引MC2）中间电压过低";
    case 20:
        return "（牵引MC2）线路接触器卡分";
    case 21:
        return "（牵引MC2）线路接触器卡合";
    case 22:
        return "（牵引MC2）充电接触器卡分";
    case 23:
        return "（牵引MC2）充电接触器卡合";
    case 24:
        return "（牵引MC2）直流输入过流";
    case 25:
        return "（牵引MC2）预充电超时";
    case 26:
        return "（牵引MC2）DC-link短路";
    case 27:
        return "（牵引MC2）VLU开路";
    case 28:
        return "（牵引MC2）逆变模块温度高";
    case 29:
        return "（牵引MC2）逆变模块温度过高";
    case 30:
        return "（牵引MC2）线路滤波器温度高";
    case 31:
        return "（牵引MC2）线路滤波器温度过高";
    case 32:
        return "（牵引MC1）定子频率过高";
    case 33:
        return "（牵引MC1）电机电流不平衡";
    case 34:
        return "（牵引MC1）电机1温度高";
    case 35:
        return "（牵引MC1）电机1温度过高";
    case 36:
        return "（牵引MC1）电机2温度高";
    case 37:
        return "（牵引MC1）电机2温度过高";
    case 38:
        return "（牵引MC1）IGBT-故障";
    case 39:
        return "（牵引MC1）电机过流";
    case 40:
        return "（牵引MC1）空转/滑行";
    case 41:
        return "（牵引MC2）定子频率过高";
    case 42:
        return "（牵引MC2）电机电流不平衡";
    case 43:
        return "（牵引MC2）电机3温度高";
    case 44:
        return "（牵引MC2）电机3温度过高";
    case 45:
        return "（牵引MC2）电机4温度高";
    case 46:
        return "（牵引MC2）电机4温度过高";
    case 47:
        return "（牵引MC2）光纤故障";
    case 48:
        return "（牵引MC2）IGBT-故障";
    case 49:
        return "（牵引MC2）电机过流";
    case 50:
        return "（牵引MC2）空转/滑行";
    case 51:
        return "（牵引）冷却液液位低";
    case 52:
        return "（牵引）冷却液液位过低";
    case 53:
        return "（牵引）冷却液不流通或流量故障";
    case 54:
        return "（牵引）水管温度过高";
    case 55:
        return "（牵引）冷却系统供电断路器断开";
    case 56:
        return "（牵引）CANopen通讯故障";
    case 57:
        return "车辆牵引使能丢失";
    case 58:
        return "（牵引MC1）单个速度与列车参考速度差距太大";
    case 59:
        return "（牵引MC2）单个速度与列车参考速度差距太大";
    case 60:
        return "M1速度传感器故障";
    case 61:
        return "M2速度传感器故障";
    case 62:
        return "M3速度传感器故障";
    case 63:
        return "M4速度传感器故障";
    case 64:
        return "所有速度传感器故障";
    case 65:
        return "轮径值超范围";
    case 66:
        return "向前向后指令同时有效";
    case 67:
        return "牵引制动指令同时存在";
    case 68:
        return "列车超速保护";
    case 69:
        return "（牵引MC1）U相故障";
    case 70:
        return "（牵引MC1）V相故障";
    case 71:
        return "（牵引MC1）W相故障";
    case 72:
        return "（牵引MC2）U相故障";
    case 73:
        return "（牵引MC2）V相故障";
    case 74:
        return "（牵引MC2）W相故障";
    case 75:
        return "（牵引MC1）U相过流";
    case 76:
        return "（牵引MC1）V相过流";
    case 77:
        return "（牵引MC1）W相过流";
    case 78:
        return "（牵引MC2）U相过流";
    case 79:
        return "（牵引MC2）V相过流";
    case 80:
        return "（牵引MC2）W相过流";
    case 81:
        return "（牵引MC1）硬件或驱动故障";
    case 82:
        return "（牵引MC2）硬件或驱动故障";
    default:
        return "未知故障代码";
    }
}

void MainWindow::on_readPushButton_clicked()
{
    quint8 *d = m_sendPacket.appData();
    d[0] = 0x55;
    d[1] = 0xAA;
    d[2] = 0xAA;
    d[3] = 0x55;
    d[4] = 0;
    d[5] = 111;
    d[6] = 0;
    d[7] = 0;
    d[8] = 0;
    d[9] = 0;
    d[10] = 0;
    d[11] = 0;
    d[12] = 0;
    d[13] = 0;
    m_ClientSocket->write(m_sendPacket);
    ReadFlag = 1;
}

void MainWindow::on_downloadPushButton_clicked()
{
    if(ui->faultListTableWidget->selectedRanges().count() == 0) {
        ui->resultLabel->setText("未选择故障！");
        return;
    }
    QList<QTableWidgetSelectionRange> selectedRange = ui->faultListTableWidget->selectedRanges();
    quint16 topRow = selectedRange.begin()->topRow() + 1;
    quint16 bottomRow = selectedRange.begin()->bottomRow() + 1;

    quint8 *d = m_sendPacket.appData();
    d[0] = 0x55;
    d[1] = 0xAA;
    d[2] = 0xAA;
    d[3] = 0x55;
    d[4] = 0;
    d[5] = 222;
    d[6] = topRow >> 8;
    d[7] = topRow & 0xff;
    d[8] = bottomRow >> 8;
    d[9] = bottomRow & 0xff;
    d[10] = 0;
    d[11] = 0;
    d[12] = 0;
    d[13] = 0;
    m_ClientSocket->write(m_sendPacket);
    mapInitial();
    initialFaultSeries();
    ReadFlag = 2;
}

void MainWindow::SyncTimeCycle()
{
    quint8 *d = m_timePacket.appData();
    d[0] = 0x55;
    d[1] = 0xAA;
    d[2] = 0xAA;
    d[3] = 0x55;
    d[4] = 1;
    d[5] = 77;
    int clear_number = 6;
    for(;clear_number<102;clear_number++) {
        d[clear_number] = 0;
    }
    d[14] = QDateTime::currentDateTime().date().year()%100;
    d[15] = QDateTime::currentDateTime().date().month();
    d[16] = QDateTime::currentDateTime().date().day();
    d[17] = QDateTime::currentDateTime().time().hour();
    d[18] = QDateTime::currentDateTime().time().minute();
    d[19] = QDateTime::currentDateTime().time().second();

    m_ClientSocket->write(m_timePacket);

}

void MainWindow::on_outPushButton_clicked()
{
    QString outFileName = QFileDialog::getSaveFileName(this,
                                                       tr("导出至"),
                                                       "",
                                                       tr("csv File(*.csv)"));
    if(outFileName.isNull())
        return;
    QFile outFile;
    outFile.setFileName(outFileName);
    if(!outFile.open(QIODevice::WriteOnly|QIODevice::Text))
        ui->resultLabel->setText(tr("导出文件失败!"));
    else
        ui->resultLabel->setText(tr("导出文件成功!"));
    QTextStream out(&outFile);
    out.setCodec("UTF-8");
    out<< QChar(QChar::ByteOrderMark);          //为CSV文件添加BOM标识

    out<<tr("故障组数")<<","<<tr("故障发生时间")<<","<<tr("触发故障")<<","<<tr("存在故障")<<",";
    out<<tr("向前指令激活")<<","<<tr("向后指令激活")<<","<<tr("牵引指令激活")<<","<<tr("制动指令激活")<<","<<tr("惰行指令激活")<<","<<tr("复位TCU指令激活")<<",";
    out<<tr("新轮径设置激活")<<","<<tr("紧急制动指令")<<","<<tr("切除电制动")<<","<<tr("车辆限速有效")<<","<<tr("库内测试模式")<<","<<tr("库内充电模式")<<",";
    out<<tr("低恒速模式激活")<<","<<tr("牵引允许")<<","<<tr("切除架1牵引")<<","<<tr("切除架2牵引")<<","<<tr("MC1牵引使能状态")<<","<<tr("MC2牵引使能状态")<<",";
    out<<tr("MC1电制动状态")<<","<<tr("MC2电制动状态")<<","<<tr("MC1线路接触器状态")<<","<<tr("MC1预充电接触器状态")<<","<<tr("MC2线路接触器状态")<<",";
    out<<tr("MC2预充电接触器状态")<<","<<tr("风机断路器状态")<<","<<tr("MC1隔离状态")<<","<<tr("MC2隔离状态")<<",";
    out<<tr("牵引/制动力级位")<<","<<tr("MC1牵引力实际值(kN)")<<","<<tr("MC2牵引力实际值(kN)")<<","<<tr("MC1电制动力实际值(kN)")<<","<<tr("MC2电制动力实际值(kN)")<<",";
    out<<tr("电机1速度(km/h)")<<","<<tr("电机2速度(km/h)")<<","<<tr("电机3速度(km/h)")<<","<<tr("电机4速度(km/h)")<<","<<tr("动力电池最大允许放电电流(A)")<<",";
    out<<tr("动力电池最大允许充电电流(A)")<<","<<tr("网侧电压(V)")<<","<<tr("逆变器1直流电压(V)")<<","<<tr("逆变器2直流电压(V)")<<","<<tr("MC1直流母线电流(A)")<<",";
    out<<tr("MC2直流母线电流(A)")<<","<<tr("MC1电机u相电流(A)")<<","<<tr("MC1电机v相电流(A)")<<","<<tr("MC1电机w相电流(A)")<<","<<tr("MC2电机u相电流(A)")<<","<<tr("MC2电机v相电流(A)")<<",";
    out<<tr("MC2电机w相电流(A)")<<","<<tr("MC1电机d轴电流(A)")<<","<<tr("MC1电机q轴电流(A)")<<","<<tr("MC2电机d轴电流(A)")<<","<<tr("MC2电机q轴电流(A)")<<","<<tr("电机1温度(℃)")<<",";
    out<<tr("电机2温度(℃)")<<","<<tr("电机3温度(℃)")<<","<<tr("电机4温度(℃)")<<","<<tr("母线电感温度(℃)")<<","<<tr("模块1温度点1(℃)")<<","<<tr("模块1温度点2(℃)")<<","<<tr("模块2温度点1(℃)")<<",";
    out<<tr("模块2温度点2(℃)")<<","<<tr("电感温度(℃)")<<","<<tr("电机1转子温度(℃)")<<","<<tr("电机2转子温度(℃)")<<","<<tr("电机3转子温度(℃)")<<","<<tr("电机4转子温度(℃)")<<","<<tr("冷却液温度(℃)")<<"\n";


    for(int i = 1; i <= map.count(); i++)
    {
        for(int j = 0; j < map.value(i).count(); j++)
        {
            if(j != (map.value(i).count() - 1))
                out<<map.value(i).at(j)<<",";
            else
                out<<map.value(i).at(j)<<"\n";
        }
    }



    outFile.close();
}

void MainWindow::checkBool(bool status, QLabel *label)
{
    if(status) {
        label->setStyleSheet("background: rgb(0, 255, 0);");
    }
    else {
        label->setStyleSheet("background: rgb(255, 255, 255);");
    }
}

void MainWindow::SocketStatusChanged(QAbstractSocket::SocketState socketState)
{
    switch (socketState) {
    case QAbstractSocket::HostLookupState:
        //        ui->SocketStatusLabel->setText("寻找中");
        break;
    case QAbstractSocket::ConnectingState:
        //        ui->SocketStatusLabel->setText("连接中");
        break;
    case QAbstractSocket::ConnectedState:
        //        ui->SocketStatusLabel->setText("已连接");
        //        ui->SocketStatusLabel->setStyleSheet("background: rgb(0, 255, 0);");
        ui->readPushButton->setEnabled(true);
        WatchData_textChanged("on");
        ui->recordOutPushButton->setEnabled(true);
        ui->stackedWidget->setCurrentIndex(0);
        break;
    case QAbstractSocket::ClosingState:
        //        ui->SocketStatusLabel->setText("断开中");
        //        ui->SocketStatusLabel->setStyleSheet("background: rgb(255, 255, 255);");
        break;
    case QAbstractSocket::UnconnectedState:
        //        ui->SocketStatusLabel->setText("未连接");
        ui->readPushButton->setEnabled(false);
        ui->downloadPushButton->setEnabled(false);
        ui->outPushButton->setEnabled(false);
        ui->recordOutPushButton->setEnabled(false);
        break;
    default:
        break;
    }
}

void MainWindow::ConfigLoad()
{
    QFile BoolFile("BOOL.txt");
    QFile WordFile("WORD.txt");
    QFile WatchFile("WatchCH.csv");
    QFile DownloadFile("DownloadCH.csv");
    if((BoolFile.open(QIODevice::ReadOnly | QIODevice::Text)) == false || WordFile.open(QIODevice::ReadOnly | QIODevice::Text) == false || DownloadFile.open(QIODevice::ReadOnly | QIODevice::Text) == false || WatchFile.open(QIODevice::ReadOnly | QIODevice::Text) == false)
    {
        qDebug()<<"Open File fail"<<endl;
        return;
    }
    QTextStream boolText(&BoolFile);
    QTextStream wordText(&WordFile);
    QTextStream WatchText(&WatchFile);
    QTextStream DownloadText(&DownloadFile);
    BoolLabelName = boolText.readAll().split("\n");
    WordLabelName = wordText.readAll().split("\n");
    WatchCHName = WatchText.readAll().split("\n");
    WatchCHName.insert(0,"");
    DownloadCHName = DownloadText.readAll().split("\n");
    DownloadCHName.insert(0,"");
    //    qDebug()<<BoolLabelName.count();
    //    qDebug()<<WordLabelName.count();
    //    qDebug()<<WatchCHName.count();
    //    qDebug()<<WatchCHName.at(0);
    //    qDebug()<<WatchCHName.at(1);
    //    qDebug()<<WatchCHName.at(26);
    //    qDebug()<<DownloadCHName.count();
}

void MainWindow::labelNameUpdate()
{
    if(BoolLabelName.count() != 27) //检查大小，防止调用不存在的索引导致程序崩溃
        return;
    if(WordLabelName.count() != 41) //检查大小，防止调用不存在的索引导致程序崩溃
        return;
    ui->boolLabel_0->setText(BoolLabelName.at(0));//bool label
    ui->boolLabel_1->setText(BoolLabelName.at(1));
    ui->boolLabel_2->setText(BoolLabelName.at(2));
    ui->boolLabel_3->setText(BoolLabelName.at(3));
    ui->boolLabel_4->setText(BoolLabelName.at(4));
    ui->boolLabel_5->setText(BoolLabelName.at(5));
    ui->boolLabel_6->setText(BoolLabelName.at(6));
    ui->boolLabel_7->setText(BoolLabelName.at(7));
    ui->boolLabel_8->setText(BoolLabelName.at(8));
    ui->boolLabel_9->setText(BoolLabelName.at(9));
    ui->boolLabel_10->setText(BoolLabelName.at(10));
    ui->boolLabel_11->setText(BoolLabelName.at(11));
    ui->boolLabel_12->setText(BoolLabelName.at(12));
    ui->boolLabel_13->setText(BoolLabelName.at(13));
    ui->boolLabel_14->setText(BoolLabelName.at(14));
    ui->boolLabel_15->setText(BoolLabelName.at(15));
    ui->boolLabel_16->setText(BoolLabelName.at(16));
    ui->boolLabel_17->setText(BoolLabelName.at(17));
    ui->boolLabel_18->setText(BoolLabelName.at(18));
    ui->boolLabel_19->setText(BoolLabelName.at(19));
    ui->boolLabel_20->setText(BoolLabelName.at(20));
    ui->boolLabel_21->setText(BoolLabelName.at(21));
    ui->boolLabel_22->setText(BoolLabelName.at(22));
    ui->boolLabel_23->setText(BoolLabelName.at(23));
    ui->boolLabel_24->setText(BoolLabelName.at(24));
    ui->boolLabel_25->setText(BoolLabelName.at(25));
    ui->boolLabel_26->setText(BoolLabelName.at(26));

    ui->wordLabel_0->setText(WordLabelName.at(0));//word label
    ui->wordLabel_1->setText(WordLabelName.at(1));
    ui->wordLabel_2->setText(WordLabelName.at(2));
    ui->wordLabel_3->setText(WordLabelName.at(3));
    ui->wordLabel_4->setText(WordLabelName.at(4));
    ui->wordLabel_5->setText(WordLabelName.at(5));
    ui->wordLabel_6->setText(WordLabelName.at(6));
    ui->wordLabel_7->setText(WordLabelName.at(7));
    ui->wordLabel_8->setText(WordLabelName.at(8));
    ui->wordLabel_9->setText(WordLabelName.at(9));
    ui->wordLabel_10->setText(WordLabelName.at(10));
    ui->wordLabel_11->setText(WordLabelName.at(11));
    ui->wordLabel_12->setText(WordLabelName.at(12));
    ui->wordLabel_13->setText(WordLabelName.at(13));
    ui->wordLabel_14->setText(WordLabelName.at(14));
    ui->wordLabel_15->setText(WordLabelName.at(15));
    ui->wordLabel_16->setText(WordLabelName.at(16));
    ui->wordLabel_17->setText(WordLabelName.at(17));
    ui->wordLabel_18->setText(WordLabelName.at(18));
    ui->wordLabel_19->setText(WordLabelName.at(19));
    ui->wordLabel_20->setText(WordLabelName.at(20));
    ui->wordLabel_21->setText(WordLabelName.at(21));
    ui->wordLabel_22->setText(WordLabelName.at(22));
    ui->wordLabel_23->setText(WordLabelName.at(23));
    ui->wordLabel_24->setText(WordLabelName.at(24));
    ui->wordLabel_25->setText(WordLabelName.at(25));
    ui->wordLabel_26->setText(WordLabelName.at(26));
    ui->wordLabel_27->setText(WordLabelName.at(27));
    ui->wordLabel_28->setText(WordLabelName.at(28));
    ui->wordLabel_29->setText(WordLabelName.at(29));
    ui->wordLabel_30->setText(WordLabelName.at(30));
    ui->wordLabel_31->setText(WordLabelName.at(31));
    ui->wordLabel_32->setText(WordLabelName.at(32));
    ui->wordLabel_33->setText(WordLabelName.at(33));
    ui->wordLabel_34->setText(WordLabelName.at(34));
    ui->wordLabel_35->setText(WordLabelName.at(35));
    ui->wordLabel_36->setText(WordLabelName.at(36));
    ui->wordLabel_37->setText(WordLabelName.at(37));
    ui->wordLabel_38->setText(WordLabelName.at(38));
    ui->wordLabel_39->setText(WordLabelName.at(39));
    ui->wordLabel_40->setText(WordLabelName.at(40));
}

void MainWindow::WatchData_textChanged(const QString data)
{
    QLineEdit *lineEdit = qobject_cast<QLineEdit *>(sender());
    if(data.toInt() >= WatchCHName.count())  {
        lineEdit->setText(QString::number(WatchCHName.count()-1));
    }

    //通道名称更新
    ui->chEditName_0->setText(WatchCHName.at(ui->chEdit_0->text().toInt()));
    ui->chEditName_1->setText(WatchCHName.at(ui->chEdit_1->text().toInt()));
    ui->chEditName_2->setText(WatchCHName.at(ui->chEdit_2->text().toInt()));
    ui->chEditName_3->setText(WatchCHName.at(ui->chEdit_3->text().toInt()));
    ui->chEditName_4->setText(WatchCHName.at(ui->chEdit_4->text().toInt()));
    ui->chEditName_5->setText(WatchCHName.at(ui->chEdit_5->text().toInt()));
    ui->chEditName_6->setText(WatchCHName.at(ui->chEdit_6->text().toInt()));
    ui->chEditName_7->setText(WatchCHName.at(ui->chEdit_7->text().toInt()));
    ui->chEditName_8->setText(WatchCHName.at(ui->chEdit_8->text().toInt()));
    ui->chEditName_9->setText(WatchCHName.at(ui->chEdit_9->text().toInt()));
    ui->chEditName_10->setText(WatchCHName.at(ui->chEdit_10->text().toInt()));
    ui->chEditName_11->setText(WatchCHName.at(ui->chEdit_11->text().toInt()));
    ui->chEditName_12->setText(WatchCHName.at(ui->chEdit_12->text().toInt()));
    ui->chEditName_13->setText(WatchCHName.at(ui->chEdit_13->text().toInt()));
    ui->chEditName_14->setText(WatchCHName.at(ui->chEdit_14->text().toInt()));
    ui->chEditName_15->setText(WatchCHName.at(ui->chEdit_15->text().toInt()));
    ui->chEditName_16->setText(WatchCHName.at(ui->chEdit_16->text().toInt()));
    ui->chEditName_17->setText(WatchCHName.at(ui->chEdit_17->text().toInt()));
    ui->chEditName_18->setText(WatchCHName.at(ui->chEdit_18->text().toInt()));
    ui->chEditName_19->setText(WatchCHName.at(ui->chEdit_19->text().toInt()));
}

void MainWindow::DownloadData_textChanged(const QString data)
{
    QLineEdit *lineEdit = qobject_cast<QLineEdit *>(sender());
    if(data.toInt() >= DownloadCHName.count())  {//判断输入值是否超出范围
        lineEdit->setText(QString::number(DownloadCHName.count()-1));
    }

    //通道名称更新
    ui->downoloadCH_Name_1->setText(DownloadCHName.at(ui->downoloadCH_1->text().toInt()));
    ui->downoloadCH_Name_2->setText(DownloadCHName.at(ui->downoloadCH_2->text().toInt()));
    ui->downoloadCH_Name_3->setText(DownloadCHName.at(ui->downoloadCH_3->text().toInt()));
    ui->downoloadCH_Name_4->setText(DownloadCHName.at(ui->downoloadCH_4->text().toInt()));
    ui->downoloadCH_Name_5->setText(DownloadCHName.at(ui->downoloadCH_5->text().toInt()));
    ui->downoloadCH_Name_6->setText(DownloadCHName.at(ui->downoloadCH_6->text().toInt()));
    ui->downoloadCH_Name_7->setText(DownloadCHName.at(ui->downoloadCH_7->text().toInt()));
    ui->downoloadCH_Name_8->setText(DownloadCHName.at(ui->downoloadCH_8->text().toInt()));
    ui->downoloadCH_Name_9->setText(DownloadCHName.at(ui->downoloadCH_9->text().toInt()));
    ui->downoloadCH_Name_10->setText(DownloadCHName.at(ui->downoloadCH_10->text().toInt()));
}

void MainWindow::on_sendPushButton_clicked()
{
    quint8 *d = m_timePacket.appData();
    d[0] = 0x55;
    d[1] = 0xAA;
    d[2] = 0xAA;
    d[3] = 0x55;
    d[4] = 1;
    d[5] = 0xBC;
    int clear_number = 6;
    for(;clear_number<102;clear_number++) {
        d[clear_number] = 0;
    }

    d[22] = upperSplit(ui->downoloadCH_1->text().toInt());
    d[23] = lowerSplit(ui->downoloadCH_1->text().toInt());
    d[24] = upperSplit(ui->downoloadCH_2->text().toInt());
    d[25] = lowerSplit(ui->downoloadCH_2->text().toInt());
    d[26] = upperSplit(ui->downoloadCH_3->text().toInt());
    d[27] = lowerSplit(ui->downoloadCH_3->text().toInt());
    d[28] = upperSplit(ui->downoloadCH_4->text().toInt());
    d[29] = lowerSplit(ui->downoloadCH_4->text().toInt());
    d[30] = upperSplit(ui->downoloadCH_5->text().toInt());
    d[31] = lowerSplit(ui->downoloadCH_5->text().toInt());
    d[32] = upperSplit(ui->downoloadCH_6->text().toInt());
    d[33] = lowerSplit(ui->downoloadCH_6->text().toInt());
    d[34] = upperSplit(ui->downoloadCH_7->text().toInt());
    d[35] = lowerSplit(ui->downoloadCH_7->text().toInt());
    d[36] = upperSplit(ui->downoloadCH_8->text().toInt());
    d[37] = lowerSplit(ui->downoloadCH_8->text().toInt());
    d[38] = upperSplit(ui->downoloadCH_9->text().toInt());
    d[39] = lowerSplit(ui->downoloadCH_9->text().toInt());
    d[40] = upperSplit(ui->downoloadCH_10->text().toInt());
    d[41] = lowerSplit(ui->downoloadCH_10->text().toInt());

    d[42] = upperSplit(ui->downoloadCH_Edit_1->text().toInt());
    d[43] = lowerSplit(ui->downoloadCH_Edit_1->text().toInt());
    d[44] = upperSplit(ui->downoloadCH_Edit_2->text().toInt());
    d[45] = lowerSplit(ui->downoloadCH_Edit_2->text().toInt());
    d[46] = upperSplit(ui->downoloadCH_Edit_3->text().toInt());
    d[47] = lowerSplit(ui->downoloadCH_Edit_3->text().toInt());
    d[48] = upperSplit(ui->downoloadCH_Edit_4->text().toInt());
    d[49] = lowerSplit(ui->downoloadCH_Edit_4->text().toInt());
    d[50] = upperSplit(ui->downoloadCH_Edit_5->text().toInt());
    d[51] = lowerSplit(ui->downoloadCH_Edit_5->text().toInt());
    d[52] = upperSplit(ui->downoloadCH_Edit_6->text().toInt());
    d[53] = lowerSplit(ui->downoloadCH_Edit_6->text().toInt());
    d[54] = upperSplit(ui->downoloadCH_Edit_7->text().toInt());
    d[55] = lowerSplit(ui->downoloadCH_Edit_7->text().toInt());
    d[56] = upperSplit(ui->downoloadCH_Edit_8->text().toInt());
    d[57] = lowerSplit(ui->downoloadCH_Edit_8->text().toInt());
    d[58] = upperSplit(ui->downoloadCH_Edit_9->text().toInt());
    d[59] = lowerSplit(ui->downoloadCH_Edit_9->text().toInt());
    d[60] = upperSplit(ui->downoloadCH_Edit_10->text().toInt());
    d[61] = lowerSplit(ui->downoloadCH_Edit_10->text().toInt());

    d[63] = ui->chEdit_0->text().toInt();
    d[65] = ui->chEdit_1->text().toInt();
    d[67] = ui->chEdit_2->text().toInt();
    d[69] = ui->chEdit_3->text().toInt();
    d[71] = ui->chEdit_4->text().toInt();
    d[73] = ui->chEdit_5->text().toInt();
    d[75] = ui->chEdit_6->text().toInt();
    d[77] = ui->chEdit_7->text().toInt();
    d[79] = ui->chEdit_8->text().toInt();
    d[81] = ui->chEdit_9->text().toInt();
    d[83] = ui->chEdit_10->text().toInt();
    d[85] = ui->chEdit_11->text().toInt();
    d[87] = ui->chEdit_12->text().toInt();
    d[89] = ui->chEdit_13->text().toInt();
    d[91] = ui->chEdit_14->text().toInt();
    d[93] = ui->chEdit_15->text().toInt();
    d[95] = ui->chEdit_16->text().toInt();
    d[97] = ui->chEdit_17->text().toInt();
    d[99] = ui->chEdit_18->text().toInt();
    d[101] = ui->chEdit_19->text().toInt();

    m_ClientSocket->write(m_timePacket);
}

void MainWindow::lineEditInitial()
{
    ui->chEdit_0->setValidator(new QIntValidator(ui->chEdit_0));//可选通道限制整数输入
    ui->chEdit_1->setValidator(new QIntValidator(ui->chEdit_1));
    ui->chEdit_2->setValidator(new QIntValidator(ui->chEdit_2));
    ui->chEdit_3->setValidator(new QIntValidator(ui->chEdit_3));
    ui->chEdit_4->setValidator(new QIntValidator(ui->chEdit_4));
    ui->chEdit_5->setValidator(new QIntValidator(ui->chEdit_5));
    ui->chEdit_6->setValidator(new QIntValidator(ui->chEdit_6));
    ui->chEdit_7->setValidator(new QIntValidator(ui->chEdit_7));
    ui->chEdit_8->setValidator(new QIntValidator(ui->chEdit_8));
    ui->chEdit_9->setValidator(new QIntValidator(ui->chEdit_9));
    ui->chEdit_10->setValidator(new QIntValidator(ui->chEdit_10));
    ui->chEdit_11->setValidator(new QIntValidator(ui->chEdit_11));
    ui->chEdit_12->setValidator(new QIntValidator(ui->chEdit_12));
    ui->chEdit_13->setValidator(new QIntValidator(ui->chEdit_13));
    ui->chEdit_14->setValidator(new QIntValidator(ui->chEdit_14));
    ui->chEdit_15->setValidator(new QIntValidator(ui->chEdit_15));
    ui->chEdit_16->setValidator(new QIntValidator(ui->chEdit_16));
    ui->chEdit_17->setValidator(new QIntValidator(ui->chEdit_17));
    ui->chEdit_18->setValidator(new QIntValidator(ui->chEdit_18));
    ui->chEdit_19->setValidator(new QIntValidator(ui->chEdit_19));
    ui->downoloadCH_1->setValidator(new QIntValidator(ui->downoloadCH_1));//下发通道限制整数输入
    ui->downoloadCH_2->setValidator(new QIntValidator(ui->downoloadCH_2));
    ui->downoloadCH_3->setValidator(new QIntValidator(ui->downoloadCH_3));
    ui->downoloadCH_4->setValidator(new QIntValidator(ui->downoloadCH_4));
    ui->downoloadCH_5->setValidator(new QIntValidator(ui->downoloadCH_5));
    ui->downoloadCH_6->setValidator(new QIntValidator(ui->downoloadCH_6));
    ui->downoloadCH_7->setValidator(new QIntValidator(ui->downoloadCH_7));
    ui->downoloadCH_8->setValidator(new QIntValidator(ui->downoloadCH_8));
    ui->downoloadCH_9->setValidator(new QIntValidator(ui->downoloadCH_9));
    ui->downoloadCH_10->setValidator(new QIntValidator(ui->downoloadCH_10));
    ui->downoloadCH_Edit_1->setValidator(new QIntValidator(ui->downoloadCH_Edit_1));//下发通道限制整数输入
    ui->downoloadCH_Edit_2->setValidator(new QIntValidator(ui->downoloadCH_Edit_2));
    ui->downoloadCH_Edit_3->setValidator(new QIntValidator(ui->downoloadCH_Edit_3));
    ui->downoloadCH_Edit_4->setValidator(new QIntValidator(ui->downoloadCH_Edit_4));
    ui->downoloadCH_Edit_5->setValidator(new QIntValidator(ui->downoloadCH_Edit_5));
    ui->downoloadCH_Edit_6->setValidator(new QIntValidator(ui->downoloadCH_Edit_6));
    ui->downoloadCH_Edit_7->setValidator(new QIntValidator(ui->downoloadCH_Edit_7));
    ui->downoloadCH_Edit_8->setValidator(new QIntValidator(ui->downoloadCH_Edit_8));
    ui->downoloadCH_Edit_9->setValidator(new QIntValidator(ui->downoloadCH_Edit_9));
    ui->downoloadCH_Edit_10->setValidator(new QIntValidator(ui->downoloadCH_Edit_10));

    connect(ui->chEdit_0,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);//可选通道输入改变
    connect(ui->chEdit_1,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_2,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_3,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_4,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_5,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_6,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_7,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_8,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_9,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_10,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_11,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_12,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_13,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_14,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_15,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_16,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_17,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_18,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);
    connect(ui->chEdit_19,&QLineEdit::textEdited,this,&MainWindow::WatchData_textChanged);

    connect(ui->downoloadCH_1,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);//下发通道输入改变
    connect(ui->downoloadCH_2,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_3,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_4,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_5,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_6,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_7,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_8,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_9,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
    connect(ui->downoloadCH_10,&QLineEdit::textEdited,this,&MainWindow::DownloadData_textChanged);
}

quint8 MainWindow::upperSplit(int word)
{
    if(word < 256)
        return  0;

    return word/256;
}

quint8 MainWindow::lowerSplit(int word)
{
    return word%256;
}

void MainWindow::recordData(const quint8 *data)
{
    QStringList *RealTimeInfo = new QStringList;
    QString dateTime = QDateTime::currentDateTime().toString("hh:mm:ss.zzz yyyy-MM-dd");
    RealTimeInfo->append(dateTime);
    QString existFault = "";
    if((data[15] & 0x1) == 1) existFault += tr("系统主电路正极接地 ");
    if((data[15] >> 1 & 0x1) == 1) existFault += tr("系统主电路负极接地 ");
    if((data[15] >> 2 & 0x1) == 1) existFault += tr("（牵引）电网电压过高 ");
    if((data[15] >> 3 & 0x1) == 1) existFault += tr("（牵引）电网电压过低 ");
    if((data[15] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）中间电压过高 ");
    if((data[15] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）中间电压过低 ");
    if((data[15] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）线路接触器卡分 ");
    if((data[15] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）线路接触器卡合 ");

    if((data[16] & 0x1) == 1) existFault += tr("（牵引MC1）充电接触器卡分 ");
    if((data[16] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）充电接触器卡合 ");
    if((data[16] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1） 直流输入过流 ");
    if((data[16] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1） 预充电超时 ");
    if((data[16] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1） VLU开路 ");
    if((data[16] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）逆变模块温度高 ");
    if((data[16] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）逆变模块温度过高 ");
    if((data[16] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1） 线路滤波器温度高 ");

    if((data[17] & 0x1) == 1) existFault += tr("（牵引MC1） 线路滤波器温度过高 ");
    if((data[17] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）中间电压过高 ");
    if((data[17] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）中间电压过低 ");
    if((data[17] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2）线路接触器卡分 ");
    if((data[17] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2）线路接触器卡合 ");
    if((data[17] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）充电接触器卡分 ");
    if((data[17] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）充电接触器卡合 ");
    if((data[17] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2） 直流输入过流 ");

    if((data[18] & 0x1) == 1) existFault += tr("（牵引MC2） 预充电超时 ");
    if((data[18] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2） DC-link短路 ");
    if((data[18] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2） VLU开路 ");
    if((data[18] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2） 逆变模块温度高 ");
    if((data[18] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2） 逆变模块温度过高 ");
    if((data[18] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2） 线路滤波器温度高 ");
    if((data[18] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2） 线路滤波器温度过高 ");
    if((data[18] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）定子频率过高 ");

    if((data[19] & 0x1) == 1) existFault += tr("（牵引MC1）电机电流不平衡 ");
    if((data[19] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）电机1温度高 ");
    if((data[19] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1）电机1温度过高 ");
    if((data[19] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1）电机2温度高 ");
    if((data[19] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）电机2温度过高 ");
    if((data[19] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）IGBT-故障 ");
    if((data[19] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）电机过流 ");
    if((data[19] >> 8 & 0x1) == 1) existFault += tr("（牵引MC1）空转/滑行 ");

    if((data[20] & 0x1) == 1) existFault += tr("（牵引MC2）定子频率过高 ");
    if((data[20] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）电机电流不平衡 ");
    if((data[20] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）电机3温度高 ");
    if((data[20] >> 3 & 0x1) == 1) existFault += tr("（牵引MC2）电机3温度过高 ");
    if((data[20] >> 4 & 0x1) == 1) existFault += tr("（牵引MC2）电机4温度高 ");
    if((data[20] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）电机4温度过高 ");
    if((data[20] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）光纤故障 ");
    if((data[20] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）IGBT-故障 ");

    if((data[21] & 0x1) == 1) existFault += tr("（牵引MC2）电机过流 ");
    if((data[21] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）空转/滑行 ");
    if((data[21] >> 2 & 0x1) == 1) existFault += tr("（牵引）冷却液液位低 ");
    if((data[21] >> 3 & 0x1) == 1) existFault += tr("（牵引）冷却液液位过低 ");
    if((data[21] >> 4 & 0x1) == 1) existFault += tr("（牵引）冷却液不流通或流量故障 ");
    if((data[21] >> 5 & 0x1) == 1) existFault += tr("（牵引）水管温度过高 ");
    if((data[21] >> 6 & 0x1) == 1) existFault += tr("（牵引）冷却系统供电断路器断开 ");
    if((data[21] >> 8 & 0x1) == 1) existFault += tr("（牵引）CANopen通讯故障 ");

    if((data[22] & 0x1) == 1) existFault += tr("车辆牵引使能丢失 ");
    if((data[22] >> 1 & 0x1) == 1) existFault += tr("（牵引MC1）单个速度与列车参考速度差距太大 ");
    if((data[22] >> 2 & 0x1) == 1) existFault += tr("（牵引MC2）单个速度与列车参考速度差距太大 ");
    if((data[22] >> 3 & 0x1) == 1) existFault += tr("M1速度传感器故障 ");
    if((data[22] >> 4 & 0x1) == 1) existFault += tr("M2速度传感器故障 ");
    if((data[22] >> 5 & 0x1) == 1) existFault += tr("M3速度传感器故障 ");
    if((data[22] >> 6 & 0x1) == 1) existFault += tr("M4速度传感器故障 ");
    if((data[22] >> 8 & 0x1) == 1) existFault += tr("所有速度传感器故障 ");

    if((data[23] & 0x1) == 1) existFault += tr("轮径值超范围 ");
    if((data[23] >> 1 & 0x1) == 1) existFault += tr("向前向后指令同时有效 ");
    if((data[23] >> 2 & 0x1) == 1) existFault += tr("牵引制动指令同时存在 ");
    if((data[23] >> 3 & 0x1) == 1) existFault += tr("列车超速保护 ");
    if((data[23] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）U相故障 ");
    if((data[23] >> 5 & 0x1) == 1) existFault += tr("（牵引MC1）V相故障 ");
    if((data[23] >> 6 & 0x1) == 1) existFault += tr("（牵引MC1）W相故障 ");
    if((data[23] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）U相故障 ");

    if((data[24] & 0x1) == 1) existFault += tr("（牵引MC2）V相故障 ");
    if((data[24] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）W相故障 ");
    if((data[24] >> 2 & 0x1) == 1) existFault += tr("（牵引MC1）U相过流 ");
    if((data[24] >> 3 & 0x1) == 1) existFault += tr("（牵引MC1）V相过流 ");
    if((data[24] >> 4 & 0x1) == 1) existFault += tr("（牵引MC1）W相过流 ");
    if((data[24] >> 5 & 0x1) == 1) existFault += tr("（牵引MC2）U相过流 ");
    if((data[24] >> 6 & 0x1) == 1) existFault += tr("（牵引MC2）V相过流 ");
    if((data[24] >> 8 & 0x1) == 1) existFault += tr("（牵引MC2）W相过流 ");

    if((data[25] & 0x1) == 1) existFault += tr("（牵引MC1）硬件或驱动故障 ");
    if((data[25] >> 1 & 0x1) == 1) existFault += tr("（牵引MC2）硬件或驱动故障 ");
    RealTimeInfo->append(existFault);


    RealTimeInfo->append(QString::number(data[26] & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 1 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 2 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 3 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 4 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 5 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 6 & 0x1));
    RealTimeInfo->append(QString::number(data[26] >> 7 & 0x1));

    RealTimeInfo->append(QString::number(data[27] & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 1 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 2 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 3 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 4 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 5 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 6 & 0x1));
    RealTimeInfo->append(QString::number(data[27] >> 7 & 0x1));

    RealTimeInfo->append(QString::number(data[28] & 0x1));
    RealTimeInfo->append(QString::number(data[28] >> 1 & 0x1));
    RealTimeInfo->append(QString::number(data[28] >> 2 & 0x1));
    RealTimeInfo->append(QString::number(data[28] >> 3 & 0x1));

    RealTimeInfo->append(QString::number(data[29] & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 1 & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 2 & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 3 & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 4 & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 5 & 0x1));
    RealTimeInfo->append(QString::number(data[29] >> 6 & 0x1));

    RealTimeInfo->append(QString::number((data[DATA_OFFSET] << 8 | data[DATA_OFFSET + 1]) - 100));
    double temp = data[DATA_OFFSET + 2] << 8 | data[DATA_OFFSET + 3];
    RealTimeInfo->append(QString::number(temp/100));
    temp = data[34] << 8 | data[35];
    RealTimeInfo->append(QString::number(temp/100));
    temp = data[36] << 8 | data[37];
    RealTimeInfo->append(QString::number(temp/100));
    temp = data[38] << 8 | data[39];
    RealTimeInfo->append(QString::number(temp/100));
    temp = data[40] << 8 | data[41];
    RealTimeInfo->append(QString::number(temp/2));
    temp = data[42] << 8 | data[43];
    RealTimeInfo->append(QString::number(temp/2));
    temp = data[44] << 8 | data[45];
    RealTimeInfo->append(QString::number(temp/2));
    temp = data[46]<< 8 | data[47];
    RealTimeInfo->append(QString::number(temp/2));
    quint16 temp_uint = data[48] << 8 | data[49];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[50] << 8 | data[51];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[52] << 8 | data[53];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[54] << 8 | data[55];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[56] << 8 | data[57];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[58] << 8 | data[59];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[60] << 8 | data[61];
    RealTimeInfo->append(QString::number(temp_uint));
    short temp_int = data[62] << 8 | data[63];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[64] << 8 | data[65];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[66] << 8 | data[67];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[68] << 8 | data[69];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[70] << 8 | data[71];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[72] << 8 | data[73];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[74] << 8 | data[75];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[76] << 8 | data[77];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[78] << 8 | data[79];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[80] << 8 | data[81];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[82] << 8 | data[83];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[84] << 8 | data[85];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[86] << 8 | data[87];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[88] << 8 | data[89];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[90] << 8 | data[91];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[92] << 8 | data[93];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[94] << 8 | data[95];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[96] << 8 | data[97];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[98] << 8 | data[99];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[100] << 8 | data[101];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[102] << 8 | data[103];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[104] << 8 | data[105];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[106] << 8 | data[107];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[108] << 8 | data[109];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[110] << 8 | data[111];
    RealTimeInfo->append(QString::number(temp_int));

    temp_uint = data[122] << 8 | data[123];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[124] << 8 | data[125];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[126] << 8 | data[127];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[128] << 8 | data[129];
    RealTimeInfo->append(QString::number(temp_uint));
    temp_uint = data[130] << 8 | data[131];
    RealTimeInfo->append(QString::number(temp_uint));

    temp_int = data[132] << 8 | data[133];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[134] << 8 | data[135];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[136] << 8 | data[137];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[138] << 8 | data[139];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[140] << 8 | data[141];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[142] << 8 | data[143];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[144] << 8 | data[145];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[146] << 8 | data[147];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[148] << 8 | data[149];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[150] << 8 | data[151];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[152] << 8 | data[153];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[154] << 8 | data[155];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[156] << 8 | data[157];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[158] << 8 | data[159];
    RealTimeInfo->append(QString::number(temp_int));
    temp_int = data[160] << 8 | data[161];
    RealTimeInfo->append(QString::number(temp_int));

    realList.append(*RealTimeInfo);
    drawChart(RealTimeInfo);
}

void MainWindow::on_recordOutPushButton_clicked()
{
    QString outFileName = QFileDialog::getSaveFileName(this,
                                                       tr("导出至"),
                                                       "",
                                                       tr("csv File(*.csv)"));
    if(outFileName.isNull())
        return;
    QFile outFile;
    outFile.setFileName(outFileName);
    if(!outFile.open(QIODevice::WriteOnly|QIODevice::Text))
        QMessageBox::warning(this,"Warning","文件打开失败");
    QTextStream out(&outFile);
    out.setCodec("UTF-8");
    out<< QChar(QChar::ByteOrderMark);          //为CSV文件添加BOM标识

    out<<tr("时间")<<","<<tr("存在故障")<<",";
    out<<tr("向前指令激活")<<","<<tr("向后指令激活")<<","<<tr("牵引指令激活")<<","<<tr("制动指令激活")<<","<<tr("惰行指令激活")<<","<<tr("复位TCU指令激活")<<",";
    out<<tr("新轮径设置激活")<<","<<tr("紧急制动指令")<<","<<tr("切除电制动")<<","<<tr("车辆限速有效")<<","<<tr("库内测试模式")<<","<<tr("库内充电模式")<<",";
    out<<tr("低恒速模式激活")<<","<<tr("牵引允许")<<","<<tr("切除架1牵引")<<","<<tr("切除架2牵引")<<","<<tr("MC1牵引使能状态")<<","<<tr("MC2牵引使能状态")<<",";
    out<<tr("MC1电制动状态")<<","<<tr("MC2电制动状态")<<","<<tr("MC1线路接触器状态")<<","<<tr("MC1预充电接触器状态")<<","<<tr("MC2线路接触器状态")<<",";
    out<<tr("MC2预充电接触器状态")<<","<<tr("风机断路器状态")<<","<<tr("MC1隔离状态")<<","<<tr("MC2隔离状态")<<",";
    out<<tr("牵引/制动力级位")<<","<<tr("MC1牵引力实际值(kN)")<<","<<tr("MC2牵引力实际值(kN)")<<","<<tr("MC1电制动力实际值(kN)")<<","<<tr("MC2电制动力实际值(kN)")<<",";
    out<<tr("电机1速度(km/h)")<<","<<tr("电机2速度(km/h)")<<","<<tr("电机3速度(km/h)")<<","<<tr("电机4速度(km/h)")<<","<<tr("动力电池最大允许放电电流(A)")<<",";
    out<<tr("动力电池最大允许充电电流(A)")<<","<<tr("网侧电压(V)")<<","<<tr("逆变器1直流电压(V)")<<","<<tr("逆变器2直流电压(V)")<<","<<tr("MC1直流母线电流(A)")<<",";
    out<<tr("MC2直流母线电流(A)")<<","<<tr("MC1电机u相电流(A)")<<","<<tr("MC1电机v相电流(A)")<<","<<tr("MC1电机w相电流(A)")<<","<<tr("MC2电机u相电流(A)")<<","<<tr("MC2电机v相电流(A)")<<",";
    out<<tr("MC2电机w相电流(A)")<<","<<tr("MC1电机d轴电流(A)")<<","<<tr("MC1电机q轴电流(A)")<<","<<tr("MC2电机d轴电流(A)")<<","<<tr("MC2电机q轴电流(A)")<<","<<tr("电机1温度(℃)")<<",";
    out<<tr("电机2温度(℃)")<<","<<tr("电机3温度(℃)")<<","<<tr("电机4温度(℃)")<<","<<tr("母线电感温度(℃)")<<","<<tr("模块1温度点1(℃)")<<","<<tr("模块1温度点2(℃)")<<","<<tr("模块2温度点1(℃)")<<",";
    out<<tr("模块2温度点2(℃)")<<","<<tr("电感温度(℃)")<<","<<tr("电机1转子温度(℃)")<<","<<tr("电机2转子温度(℃)")<<","<<tr("电机3转子温度(℃)")<<","<<tr("电机4转子温度(℃)")<<","<<tr("冷却液温度(℃)")<<",";
    out<<tr("可选观测数据1")<<","<<tr("可选观测数据2")<<","<<tr("可选观测数据3")<<","<<tr("可选观测数据4")<<","<<tr("可选观测数据5")<<","<<tr("可选观测数据6")<<","<<tr("可选观测数据7")<<","<<tr("可选观测数据8")<<",";
    out<<tr("可选观测数据9")<<","<<tr("可选观测数据10")<<","<<tr("可选观测数据11")<<","<<tr("可选观测数据12")<<","<<tr("可选观测数据13")<<","<<tr("可选观测数据14")<<","<<tr("可选观测数据15")<<","<<tr("可选观测数据16")<<",";
    out<<tr("可选观测数据17")<<","<<tr("可选观测数据18")<<","<<tr("可选观测数据19")<<","<<tr("可选观测数据20")<<"\n";


    for(int i = 0; i <= realList.count(); i++)
    {
        for(int j = 0; j < realList.value(i).count(); j++)
        {
            if(j != (realList.value(i).count() - 1))
                out<<realList.value(i).at(j)<<",";
            else
                out<<realList.value(i).at(j)<<"\n";
        }
    }
    outFile.close();
    realList.clear();
    QMessageBox::information(this,"","导出成功！");
}

void MainWindow::readWatchList()//加载当前界面布尔量和word量的标签名，用于监控列表选择变量
{
    WatchList.append(ui->boolLabel_0->text());
    WatchList.append(ui->boolLabel_1->text());
    WatchList.append(ui->boolLabel_2->text());
    WatchList.append(ui->boolLabel_3->text());
    WatchList.append(ui->boolLabel_4->text());
    WatchList.append(ui->boolLabel_5->text());
    WatchList.append(ui->boolLabel_6->text());
    WatchList.append(ui->boolLabel_7->text());
    WatchList.append(ui->boolLabel_8->text());
    WatchList.append(ui->boolLabel_9->text());
    WatchList.append(ui->boolLabel_10->text());
    WatchList.append(ui->boolLabel_11->text());
    WatchList.append(ui->boolLabel_12->text());
    WatchList.append(ui->boolLabel_13->text());
    WatchList.append(ui->boolLabel_14->text());
    WatchList.append(ui->boolLabel_15->text());
    WatchList.append(ui->boolLabel_16->text());
    WatchList.append(ui->boolLabel_17->text());
    WatchList.append(ui->boolLabel_18->text());
    WatchList.append(ui->boolLabel_19->text());
    WatchList.append(ui->boolLabel_20->text());
    WatchList.append(ui->boolLabel_21->text());
    WatchList.append(ui->boolLabel_22->text());
    WatchList.append(ui->boolLabel_23->text());
    WatchList.append(ui->boolLabel_24->text());
    WatchList.append(ui->boolLabel_25->text());
    WatchList.append(ui->boolLabel_26->text());
    WatchList.append(ui->wordLabel_0->text());
    WatchList.append(ui->wordLabel_1->text());
    WatchList.append(ui->wordLabel_2->text());
    WatchList.append(ui->wordLabel_3->text());
    WatchList.append(ui->wordLabel_4->text());
    WatchList.append(ui->wordLabel_5->text());
    WatchList.append(ui->wordLabel_6->text());
    WatchList.append(ui->wordLabel_7->text());
    WatchList.append(ui->wordLabel_8->text());
    WatchList.append(ui->wordLabel_9->text());
    WatchList.append(ui->wordLabel_10->text());
    WatchList.append(ui->wordLabel_11->text());
    WatchList.append(ui->wordLabel_12->text());
    WatchList.append(ui->wordLabel_13->text());
    WatchList.append(ui->wordLabel_14->text());
    WatchList.append(ui->wordLabel_15->text());
    WatchList.append(ui->wordLabel_16->text());
    WatchList.append(ui->wordLabel_17->text());
    WatchList.append(ui->wordLabel_18->text());
    WatchList.append(ui->wordLabel_19->text());
    WatchList.append(ui->wordLabel_20->text());
    WatchList.append(ui->wordLabel_21->text());
    WatchList.append(ui->wordLabel_22->text());
    WatchList.append(ui->wordLabel_23->text());
    WatchList.append(ui->wordLabel_24->text());
    WatchList.append(ui->wordLabel_25->text());
    WatchList.append(ui->wordLabel_26->text());
    WatchList.append(ui->wordLabel_27->text());
    WatchList.append(ui->wordLabel_28->text());
    WatchList.append(ui->wordLabel_29->text());
    WatchList.append(ui->wordLabel_30->text());
    WatchList.append(ui->wordLabel_31->text());
    WatchList.append(ui->wordLabel_32->text());
    WatchList.append(ui->wordLabel_33->text());
    WatchList.append(ui->wordLabel_34->text());
    WatchList.append(ui->wordLabel_35->text());
    WatchList.append(ui->wordLabel_36->text());
    WatchList.append(ui->wordLabel_37->text());
    WatchList.append(ui->wordLabel_38->text());
    WatchList.append(ui->wordLabel_39->text());
    WatchList.append(ui->wordLabel_40->text());

    int j = 0;
    for(QString &temp : WatchList)
    {
        ui->seletceListWidget->addItem(temp);
        ui->faultSelectTableWidget->setItem(j,1,new QTableWidgetItem(temp));
        ui->faultSelectTableWidget->setItem(j,0,new QTableWidgetItem(""));
        j++;
    }
}

void MainWindow::displayVersion()
{
    QString version = APP_VERSION;
    //qDebug()<<version;
    ui->NoConnectListWidget->addItem("当前软件版本：" + version);
    ui->NoConnectListWidget->addItem("请连接后再操作");
}

void MainWindow::on_selectPushButton_toggled(bool checked)
{
    if(checked)
    {
        ui->selectPushButton->setText("Stop");
        ui->seletceListWidget->setEnabled(false);
        isRecordAlive = true;
        recordIndex = ui->seletceListWidget->currentRow();
//        qDebug()<<recordIndex;
        initialChart();
    }   else
    {
        ui->selectPushButton->setText("Start");
        isRecordAlive = false;
        ui->seletceListWidget->setEnabled(true);
    }
}

void MainWindow::drawChart(const QStringList *data)
{
    qint64 time_temp = QDateTime::currentMSecsSinceEpoch();
    m_series->append(time_temp,data->at(recordIndex + 2).toInt());
    axisX->setMax(QDateTime::fromMSecsSinceEpoch(time_temp));
    axisX->setMin(QDateTime::fromMSecsSinceEpoch(time_temp-10000));
    if(recordIndex > 26)//如果不是布尔量
    {
        if(data->at(recordIndex + 2).toInt() > maxValue)
            maxValue = data->at(recordIndex + 2).toInt() + 100;
        if(data->at(recordIndex + 2).toInt() < minValue)
            minValue = data->at(recordIndex + 2).toInt() - 100;
        axisY->setMax(maxValue);
        axisY->setMin(minValue);
        axisY->setTickCount(10);
    }
//    qDebug()<<m_series->count();
}

void MainWindow::clickedListWidget()
{
    if(ui->selectPushButton->isEnabled())
        return;
    ui->selectPushButton->setEnabled(true);
}

void MainWindow::initialChart()
{
    m_chart->setTitle(ui->seletceListWidget->currentItem()->text());
    m_chart->legend()->hide();
    m_chart->addSeries(m_series);
    axisX->setFormat("hh:mm:ss.zzz");
    axisX->setTickCount(5);
    maxValue = 1;
    minValue = 0;
    if(recordIndex >=0 && recordIndex <= 26)//布尔量y轴范围
    {
        axisY->setRange(0,2);
        axisY->setTickCount(3);
        axisY->setLabelFormat("%d");
    }
    m_chart->addAxis(axisX,Qt::AlignBottom);
    m_chart->addAxis(axisY,Qt::AlignLeft);//轴放至图
    m_series->attachAxis(axisX);//线对应轴
    m_series->attachAxis(axisY);
    m_chartView->setChart(m_chart);
    m_chartView->setRenderHint(QPainter::Antialiasing);

    ui->gridLayout_8->addWidget(m_chartView);
}

void MainWindow::keepCallout()
{
    m_callouts.append(m_tooltip);
    m_tooltip = new Callout(m_chart);
}

void MainWindow::tooltip(QPointF point, bool state)
{
    if (m_tooltip == 0)
        m_tooltip = new Callout(m_chart);

    if (state) {
        m_tooltip->setText("X: "+QDateTime::fromMSecsSinceEpoch(point.x()).toString("hh:mm:ss.zzz")+" \nY: "+ QString::number(point.toPoint().y()));
        m_tooltip->setAnchor(point);
        m_tooltip->setZValue(11);
        m_tooltip->updateGeometry();
        m_tooltip->show();
    } else {
        m_tooltip->hide();
    }
}

void MainWindow::setFltTableIndex(int number)
{
    if(!ui->faultSelectTableWidget->hasFocus())//焦点不是该表，返回
        return;

    QTableWidgetItem *currentGroupNumber = ui->faultSelectTableWidget->item(ui->faultSelectTableWidget->currentRow(),0);
    if(currentGroupNumber->text() == "")
        currentGroupNumber->setText(QString::number(number));
    else if(currentGroupNumber->text().toInt() != number)
        currentGroupNumber->setText(QString::number(number));
        else
            currentGroupNumber->setText("");
}

void MainWindow::initialTableWidget()
{
    //设置表头
    QTableWidgetItem    *headerItem;
    QStringList headerText;
    headerText<<"故障代码"<<"发生故障时间"<<"故障描述";  //表头标题用QStringList来表示
    ui->faultListTableWidget->setColumnCount(headerText.count());//列数设置为与 headerText的行数相等
    for (int i=0;i<ui->faultListTableWidget->columnCount();i++)//列编号从0开始
    {
        headerItem=new QTableWidgetItem(headerText.at(i)); //新建一个QTableWidgetItem， headerText.at(i)获取headerText的i行字符串
        QFont font=headerItem->font();//获取原有字体设置
        font.setBold(true);//设置为粗体
        font.setPointSize(12);//字体大小
        headerItem->setForeground(Qt::black);//字体颜色
        headerItem->setFont(font);//设置字体
        ui->faultListTableWidget->setHorizontalHeaderItem(i,headerItem); //设置表头单元格的Item
    }
    //固定列宽值
    ui->faultListTableWidget->setColumnWidth(0,80);
    ui->faultListTableWidget->setColumnWidth(1,220);
    ui->faultListTableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);//固定行高列宽
    ui->faultListTableWidget->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);

    //故障图标变量选择列表
    //ui->faultSelectTableWidget->clear();
    ui->faultSelectTableWidget->setColumnCount(2);
    ui->faultSelectTableWidget->setRowCount(68);
    headerText.clear();
    headerText <<QStringLiteral("G")<<QStringLiteral("变量名");
    ui->faultSelectTableWidget->setHorizontalHeaderLabels(headerText);
    ui->faultSelectTableWidget->setColumnWidth(0,30);
}

void MainWindow::initialFaultChart()
{
    m_faultChart->addSeries(m_faultSeries);
    m_faultChart->createDefaultAxes();
    m_faultChartView->setChart(m_faultChart);
    m_faultChartView->setRenderHint(QPainter::Antialiasing);
    ui->FltChartGridLayout->addWidget(m_faultChartView);
    m_faultChart->removeSeries(m_faultSeries);
}

void MainWindow::initialFaultSeries()
{
    if(faultSeriesList.count() > 0)
    {
        for(int i = 0; i < 68; i++)
        {
            faultSeriesList.at(i)->clear();
        }
    }
    QLineSeries *newSeries;
    for(int i = 0; i < 68; i++)
    {
        newSeries = new QLineSeries();
        newSeries->setName(ui->faultSelectTableWidget->item(i,1)->text());
        faultSeriesList.append(newSeries);
    }
}

void MainWindow::setFaultSeires(int fltGrp, QStringList *FltInfo)
{
    for(int i = 0; i < 68; i++)
    {
        faultSeriesList.at(i)->append(fltGrp,FltInfo->at(i+4).toInt());
    }
}

void MainWindow::on_addDelPushButton_clicked()
{
    QTableWidgetItem *currentGroupNumber = ui->faultSelectTableWidget->item(ui->faultSelectTableWidget->currentRow(),0);
    m_faultSeries = faultSeriesList.at(ui->faultSelectTableWidget->currentRow());
    if(currentGroupNumber->text() == "")
    {
        currentGroupNumber->setText(QString::number(1));
        m_faultChart->addSeries(m_faultSeries);
    }
    else
    {
        currentGroupNumber->setText("");
        m_faultChart->removeSeries(m_faultSeries);
    }
    m_faultChart->createDefaultAxes();
}

