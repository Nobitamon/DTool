#ifndef COMMPARASETTINGDIALOG_H
#define COMMPARASETTINGDIALOG_H

#include <QDialog>

namespace Ui {
class CommParaSettingDialog;
}

class CommParaSettingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit CommParaSettingDialog(QWidget *parent = nullptr);
    ~CommParaSettingDialog();

signals:
    void setIPPort(QString ip, quint16 port);

private slots:
    void on_SetPushButton_clicked();

    void on_cancelPushButton_clicked();

private:
    Ui::CommParaSettingDialog *ui;
};

#endif // COMMPARASETTINGDIALOG_H
