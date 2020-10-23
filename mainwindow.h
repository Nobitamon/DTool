#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

namespace Ui {
class MainWindow;
}

class CommParaSettingDialog;
class QTcpSocket;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_action_P_triggered();

    void on_action_C_triggered();

    void IPPortSet(QString ip, quint16 port);

private:
    Ui::MainWindow *ui;
    CommParaSettingDialog *m_commParameterSettingDialog;
    QTcpSocket *m_ClientSocket;

    QString IPtoConnect;
    quint16 PorttoConnect;
    bool TCPState;
};

#endif // MAINWINDOW_H
