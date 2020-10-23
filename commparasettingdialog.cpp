#include "commparasettingdialog.h"
#include "ui_commparasettingdialog.h"

#include <QHostAddress>
#include <QNetworkInterface>

CommParaSettingDialog::CommParaSettingDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::CommParaSettingDialog)
{
    ui->setupUi(this);

}

CommParaSettingDialog::~CommParaSettingDialog()
{
    delete ui;
}

void CommParaSettingDialog::on_SetPushButton_clicked()
{
    //push set button, transfer the ip and port
    QString ip = ui->destinationIPComboBox->currentText();
    quint16 port = ui->destinationPortComboBox->currentText().toUShort();
    emit setIPPort(ip, port);
    this->close();
}

void CommParaSettingDialog::on_cancelPushButton_clicked()
{
    this->close();
}

