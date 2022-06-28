#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class QTcpServer;
class QTcpSocket;
class QAudioInput;
class QBuffer;
class AVCodecContext;
class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();
	void onAudioReadyRead();
	void onNotyfy();
	void encode(const QByteArray& input, QByteArray& output);

private slots:
	void on_pushButton_clicked();

private:
	Ui::MainWindow *ui;
	QTcpServer* audioserver_;
	QAudioInput* audio_;
	QBuffer* buffer_;
	QByteArray data_;
	int pos_ = 0;
	QList<QTcpSocket*> audioSockets_;
	QList<QByteArray> acache_;
	AVCodecContext* enc_ctx_;

	QByteArray mqqtData;
};
#endif // MAINWINDOW_H
