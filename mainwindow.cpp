#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "commparasettingdialog.h"
#include <QHostAddress>
#include <QTcpSocket>
#include <QMessageBox>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    m_commParameterSettingDialog(new CommParaSettingDialog(this)),
    m_ClientSocket(new QTcpSocket(this)),
//    IPtoConnect("192.168.1.12"),
//    PorttoConnect(1028)
    IPtoConnect("127.0.0.1"),//local host test
    PorttoConnect(8091),
    TCPState(false)
{
    ui->setupUi(this);

    connect(m_commParameterSettingDialog, &CommParaSettingDialog::setIPPort, this, &MainWindow::IPPortSet);
}

MainWindow::~MainWindow()
{
    delete ui;
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
        }
    }
    else
    {
        m_ClientSocket->disconnectFromHost();
        ui->action_C->setIcon(QIcon(":/Prefix/internet-earth-globe-pngrepo-com.png"));
        TCPState = false;
    }
}

void MainWindow::IPPortSet(QString ip, quint16 port)
{
    IPtoConnect = ip;
    PorttoConnect = port;
}
