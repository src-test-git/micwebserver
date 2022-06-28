#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QTcpServer>
#include <QTcpSocket>
#include <QDateTime>
#include <QBuffer>
#include <QAudioInput>
#include <QFile>
#include <QProcess>

extern "C"{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libavdevice/avdevice.h>
	#include <libavfilter/avfilter.h>
}

#define BITRATE 44100

MainWindow::MainWindow(QWidget *parent): QMainWindow(parent), ui(new Ui::MainWindow)
{
	ui->setupUi(this);
	ui->label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	ui->label->setSelection(0, ui->label->text().size());
	QAction* aExit = new QAction(this);	aExit->setShortcut(QKeySequence(QStringLiteral("Alt+X"))); connect(aExit, &QAction::triggered, this, &MainWindow::close); addAction(aExit);

	av_register_all();
	AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_MP3);
	enc_ctx_ = avcodec_alloc_context3(enc);
	enc_ctx_->bit_rate = BITRATE;
	enc_ctx_->sample_fmt = AV_SAMPLE_FMT_S16;
	enc_ctx_->sample_rate = BITRATE;
	enc_ctx_->channels = 1;
	enc_ctx_->channel_layout = AV_CH_LAYOUT_MONO;
	enc_ctx_->profile = FF_PROFILE_AAC_MAIN;
	enc_ctx_->time_base = (AVRational){1, BITRATE};
	enc_ctx_->codec_type = AVMEDIA_TYPE_AUDIO;
	enc_ctx_->frame_size = 2;
	if (avcodec_open2(enc_ctx_, enc, NULL) != 0) { qDebug() << "avcodec_open2 ERROR"; return; }

	audioserver_ = new QTcpServer(this);
	audioserver_->listen(QHostAddress::AnyIPv4, 60080);
	connect(audioserver_, &QTcpServer::newConnection, this, [this] {
		connect(audioserver_->nextPendingConnection(), &QTcpSocket::readyRead, this, &MainWindow::onAudioReadyRead);
	});

	QAudioFormat formatPcm;
	formatPcm.setSampleRate(BITRATE);
	formatPcm.setChannelCount(1);
	formatPcm.setSampleSize(16);
	formatPcm.setCodec("audio/pcm");
	formatPcm.setByteOrder(QAudioFormat::LittleEndian);
	formatPcm.setSampleType(QAudioFormat::SignedInt);

	audio_ = new QAudioInput(QAudioDeviceInfo::defaultInputDevice(), formatPcm, this);
	audio_->setNotifyInterval(100);
	data_.clear();

	buffer_ = new QBuffer(&data_, this);
	buffer_->open(QIODevice::WriteOnly);
	audio_->start(buffer_);
	connect(audio_, &QAudioInput::notify, this, &MainWindow::onNotyfy);


	QFile file ("/Work/Kiosk/Devel/Vzor/1frame.raw");
	file.open(QIODevice::ReadOnly);
	QByteArray data = file.readAll();
	QImage pix(800, 600, QImage::Format_RGB888);
	for (int i = 0; i < 800; ++i)
		for (int y = 0; y < qAbs(600); ++y) {
			quint8 val = data[y*800+i];
			pix.setPixel(i,y, QColor::fromRgb(val,val,val).rgb());
		}
	//ui->lMorda->setPixmap(QPixmap::fromImage(pix));

	QProcess* p = new QProcess(this);
	p->start("/bin/bash", QStringList() << "-c" << "mosquitto_sub -h 172.16.137.228 -t 'vzor/2/23/stream'");
	connect(p, &QProcess::readyReadStandardOutput, this, [p, this] {
		mqqtData.append(p->readAllStandardOutput());
		const int S = 800*600;
		if (mqqtData.size() >= S) {
			qDebug() << "MSIZE" << mqqtData.size();
			if (mqqtData.size() > S) qDebug() << QString(mqqtData.mid(S).toHex());
			QByteArray frame = mqqtData.mid(0, S);
			mqqtData = mqqtData.mid(S);
			if (mqqtData.size() == 1) mqqtData.clear();

			QImage pix(800, 600, QImage::Format_RGB888);
			for (int i = 0; i < 800; ++i)
				for (int y = 0; y < qAbs(600); ++y) {
					quint8 val = frame[y*800+i];
					pix.setPixel(i,y, QColor::fromRgb(val,val,val).rgb());
				}
			ui->lMorda->setPixmap(QPixmap::fromImage(pix));
		}
	});
}

MainWindow::~MainWindow()
{
	delete ui;
}

void MainWindow::onNotyfy()
{
	QByteArray data = data_.mid(pos_);
	pos_ = data_.size();
	QByteArray cdata;
	encode(data, cdata);

	if (cdata.size() > 0) {
		for (auto socket: audioSockets_)
			socket->write(cdata);
		acache_.append(cdata);
		acache_.removeFirst();
	}
}

void MainWindow::encode(const QByteArray &input, QByteArray &output)
{
	AVPacket enc_pkt;
	AVFrame *in_frame;
	int got_output = 0;

	in_frame = av_frame_alloc();
	if (!in_frame) { qDebug() << "av_frame_alloc"; return; }

	in_frame->nb_samples = 1;
	in_frame->format = enc_ctx_->sample_fmt;
	in_frame->channel_layout = enc_ctx_->channel_layout;
	if (av_frame_get_buffer(in_frame, 32) != 0) { qDebug() << "av_frame_get_buffer ERROR"; return; }

	int FRAMESIZE = 2;
	int NUMBER_OF_FRAMES = input.size() / FRAMESIZE;
	for (int i = 0; i < NUMBER_OF_FRAMES && input.size() > (i+1)*FRAMESIZE; i++) {
		av_init_packet(&enc_pkt);
		enc_pkt.data = NULL;
		enc_pkt.size = 0;
		in_frame->data[0] = (uint8_t*)input.data()+i*FRAMESIZE;
		in_frame->linesize[0] = 4;
		auto result = avcodec_encode_audio2(enc_ctx_, &enc_pkt, in_frame, &got_output);
		if (result < 0) { qDebug() << "avcodec_encode_audio2 ERROR"; return; }
		output.append(QByteArray((const char *)enc_pkt.data, enc_pkt.size));
	}
}

void MainWindow::onAudioReadyRead()
{
	auto socket = static_cast<QTcpSocket*>(sender());
	QByteArray qdata = socket->readAll();
	qDebug() << QString(qdata);
	if (!qdata.contains("GET /audio.mp3 HTTP/1."))	return;
	QByteArray ContentType = QString("HTTP/1.1 200 OK\r\n"
		"Server: Streamer/0.2\r\n"
		"Connection: close\r\n"
		"Date: %1\r\n"
		"Content-Type: audio/mpeg\r\n"
		"Cache-Control: no-cache, no-store\r\n"
		"Expires: Mon, 26 Jul 1997 05:00:00 GMT\r\n"
		"Accept-Ranges: none\r\n"
		"Cache-Control: max-age=0, no-cache, no-store, must-revalidate\r\n"
		"X-Frame-Options: sameorigin\r\n"
		"Pragma: no-cache\r\n"
		"Access-Control-Allow-Origin: *\r\n"
		"\r\n\r\n").arg(QDateTime::currentDateTime().toString(Qt::DateFormat::DefaultLocaleLongDate)).toLocal8Bit();

	socket->write(ContentType);
	for (auto c: acache_)
		socket->write(c);

	if (!audioSockets_.contains(socket))
		audioSockets_.append(socket);
}

void MainWindow::on_pushButton_clicked()
{
	mqqtData.clear();
}
