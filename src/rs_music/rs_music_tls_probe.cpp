#include <QSslSocket>
#include <obs-module.h>

extern "C" void rs_music_tls_probe()
{
	blog(LOG_INFO, "[RS Music] TLS probe:");
	blog(LOG_INFO, "[RS Music] supportsSsl = %s", QSslSocket::supportsSsl() ? "true" : "false");
	blog(LOG_INFO, "[RS Music] available backends = %s",
	     QSslSocket::availableBackends().join(", ").toUtf8().constData());
}
