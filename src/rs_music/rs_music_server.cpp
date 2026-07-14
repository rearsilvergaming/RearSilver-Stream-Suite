#include "rs_music_server.hpp"

#include <obs-module.h>

// ----------------------------------------------------------------------------
// DEPRECATED — DO NOT USE
// ----------------------------------------------------------------------------
// This implementation is intentionally inert.
// Any call into this class indicates a bug or legacy code path that
// must be removed.
// ----------------------------------------------------------------------------

RsMusicServer &RsMusicServer::instance()
{
	static RsMusicServer s;
	return s;
}

RsMusicServer::RsMusicServer(QObject *parent) : QObject(parent) {}

bool RsMusicServer::start()
{
	blog(LOG_ERROR, "[RS Music] ERROR: RsMusicServer::start() called — "
			"local server architecture is DEPRECATED and MUST NOT be used.");
	return false;
}

void RsMusicServer::stop()
{
	// no-op
}

unsigned short RsMusicServer::port() const
{
	return 0;
}

void RsMusicServer::setState(const void *)
{
	// no-op
}

void *RsMusicServer::state() const
{
	return nullptr;
}

void RsMusicServer::setPlayerHtml(const QString &)
{
	// no-op
}

QString RsMusicServer::playerHtml() const
{
	return QString();
}
