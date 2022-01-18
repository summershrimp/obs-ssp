/*
obs-ssp
 Copyright (C) 2019-2020 Yibai Zhang

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; If not, see <https://www.gnu.org/licenses/>
*/

#ifndef OBS_SSP_SSP_CLIENT_ISO_H
#define OBS_SSP_SSP_CLIENT_ISO_H
#include <QObject>
#include <QProcess>
#include <mutex>

#include <imf/ISspClient.h>
extern "C" {
#include <util/pipe.h>
}
#include <ssp_connector_proto.h>

#ifdef _WIN64
#define SSP_CONNECTOR "../../obs-plugins/" OBS_SSP_BITSTR "/ssp-connector.exe"
#else
#define SSP_CONNECTOR "ssp-connector"
#endif

class SSPClientIso : public QObject {
	Q_OBJECT

public:
	SSPClientIso(const std::string &ip, uint32_t bufferSize);

	virtual void
	setOnRecvBufferFullCallback(const imf::OnRecvBufferFullCallback &cb);
	virtual void setOnH264DataCallback(const imf::OnH264DataCallback &cb);
	virtual void setOnAudioDataCallback(const imf::OnAudioDataCallback &cb);
	virtual void setOnMetaCallback(const imf::OnMetaCallback &cb);
	virtual void
	setOnDisconnectedCallback(const imf::OnDisconnectedCallback &cb);
	virtual void setOnConnectionConnectedCallback(
		const imf::OnConnectionConnectedCallback &cb);
	virtual void setOnExceptionCallback(const imf::OnExceptionCallback &cb);
	void Stop();
	void Restart();
	static void *ReceiveThread(void *arg);
signals:
	void Start();

private slots:
	void doStart();

private:
	virtual void OnRecvBufferFull();
	virtual void OnH264Data(VideoData *video);
	virtual void OnAudioData(AudioData *audio);
	virtual void OnMetadata(Metadata *meta);
	virtual void OnDisconnected();
	virtual void OnConnectionConnected();
	virtual void OnException(Message *exception);

	std::mutex implLock;
	std::mutex loopLock;
	bool running;
	std::string ip;
	uint32_t bufferSize;
	QString ssp_connector_path;

	os_process_pipe_t *pipe;

	imf::OnRecvBufferFullCallback bufferFullCallback;
	imf::OnH264DataCallback h264DataCallback;
	imf::OnAudioDataCallback audioDataCallback;
	imf::OnConnectionConnectedCallback connectedCallback;
	imf::OnDisconnectedCallback disconnectedCallback;
	imf::OnMetaCallback metaCallback;
	imf::OnExceptionCallback exceptionCallback;
};

#endif //OBS_SSP_SSP_CLIENT_ISO_H
