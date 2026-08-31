#include "music_hub.hpp"

#include <algorithm>
#include <random>
#include <sstream>

std::vector<HubTrack> &MusicHubModel::activeRotationLocked()
{
	return m_activeSource == "local" ? m_localRotation : m_youtubeRotation;
}

const std::vector<HubTrack> &MusicHubModel::activeRotationLocked() const
{
	return m_activeSource == "local" ? m_localRotation : m_youtubeRotation;
}

size_t &MusicHubModel::activeCursorLocked()
{
	return m_activeSource == "local" ? m_localCursor : m_youtubeCursor;
}

size_t MusicHubModel::activeCursorLocked() const
{
	return m_activeSource == "local" ? m_localCursor : m_youtubeCursor;
}

bool &MusicHubModel::activeShuffledLocked()
{
	return m_activeSource == "local" ? m_localShuffled : m_youtubeShuffled;
}

bool MusicHubModel::activeShuffledLocked() const
{
	return m_activeSource == "local" ? m_localShuffled : m_youtubeShuffled;
}

const std::vector<HubTrack> &MusicHubModel::activeCanonicalLocked() const
{
	return m_activeSource == "local" ? m_localLibrary : m_youtubeFallback;
}

const HubTrack *MusicHubModel::mostRecentFallbackLocked() const
{
	if (m_hasCurrent && !m_current.request && m_current.provider == m_activeSource)
		return &m_current;
	for (const HubTrack &track : m_history)
		if (!track.request && track.provider == m_activeSource)
			return &track;
	return nullptr;
}

void MusicHubModel::positionAfterTrackLocked(const HubTrack *track)
{
	auto &rotation = activeRotationLocked();
	auto &cursor = activeCursorLocked();
	if (rotation.empty()) {
		cursor = 0;
		return;
	}
	if (!track) {
		cursor = 0;
		return;
	}
	const auto found = std::find_if(rotation.begin(), rotation.end(), [&](const HubTrack &candidate) {
		return (!track->id.empty() && candidate.id == track->id) ||
			(!track->providerId.empty() && candidate.providerId == track->providerId);
	});
	cursor = found == rotation.end() ? 0 :
		(static_cast<size_t>(std::distance(rotation.begin(), found)) + 1) % rotation.size();
}

void MusicHubModel::adoptTrackSourceLocked(const HubTrack &track)
{
	m_activeSource = track.provider == "local" ? "local" :
		(track.provider == "external" || track.provider == "spotify" ? "external" : "youtube");
}

void MusicHubModel::replaceFallback(std::vector<HubTrack> tracks, std::string label, std::string sourceUrl)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (HubTrack &track : tracks) { track.request = false; track.requestedBy = m_nonRequestLabel; }
	for (HubTrack &track : tracks) track.provider = "youtube";
	m_youtubeFallback = std::move(tracks);
	m_youtubeRotation = m_youtubeFallback;
	m_youtubeCursor = 0;
	m_youtubeShuffled = false;
	m_fallbackLabel = std::move(label);
	m_fallbackUrl = std::move(sourceUrl);
}

void MusicHubModel::replaceLocalLibrary(std::vector<HubTrack> tracks)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (HubTrack &track : tracks) { track.request = false; track.provider = "local"; track.requestedBy = m_nonRequestLabel; }
	m_localLibrary = std::move(tracks);
	m_localRotation = m_localLibrary;
	m_localCursor = 0;
	m_localShuffled = false;
}

void MusicHubModel::clearLocalLibrary()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_localLibrary.clear();
	m_localRotation.clear();
	m_localCursor = 0;
	m_localShuffled = false;
}

void MusicHubModel::activateSource(std::string source)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (source != "local" && source != "external") source = "youtube";
	m_activeSource = std::move(source);
	m_hasCurrent = false;
	m_current = {};
}

void MusicHubModel::setNonRequestLabel(std::string label)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (label.empty()) label = "Stream DJ";
	m_nonRequestLabel = std::move(label);
	auto apply = [this](auto &tracks) {
		for (HubTrack &track : tracks)
			if (!track.request) track.requestedBy = m_nonRequestLabel;
	};
	apply(m_youtubeFallback);
	apply(m_localLibrary);
	apply(m_youtubeRotation);
	apply(m_localRotation);
	apply(m_forwardHistory);
	apply(m_history);
	if (m_hasCurrent && !m_current.request) m_current.requestedBy = m_nonRequestLabel;
}

void MusicHubModel::enqueueRequest(HubTrack track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	track.request = true;
	m_requests.push_back(std::move(track));
}

void MusicHubModel::replaceRequests(std::vector<HubTrack> tracks)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_requests.clear();
	for (HubTrack &track : tracks) {
		track.request = true;
		m_requests.push_back(std::move(track));
	}
}

bool MusicHubModel::removeRequest(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto found = std::find_if(m_requests.begin(), m_requests.end(), [&](const HubTrack &track) { return track.id == id; });
	if (found == m_requests.end()) return false;
	m_requests.erase(found); return true;
}

bool MusicHubModel::cancelRequest(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto found = std::find_if(m_requests.begin(), m_requests.end(), [&](const HubTrack &track) { return track.id == id; });
	if (found == m_requests.end()) return false;
	found->cancelled = true;
	return true;
}

void MusicHubModel::clearRequests() { std::lock_guard<std::mutex> lock(m_mutex); m_requests.clear(); }

void MusicHubModel::shuffleFallback()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto &rotation = activeRotationLocked();
	rotation = activeCanonicalLocked();
	if (rotation.size() < 2) {
		activeShuffledLocked() = false;
		activeCursorLocked() = 0;
		return;
	}
	const HubTrack *recent = mostRecentFallbackLocked();
	std::mt19937 generator(std::random_device{}());
	std::shuffle(rotation.begin(), rotation.end(), generator);
	activeShuffledLocked() = true;
	positionAfterTrackLocked(recent);
}

void MusicHubModel::restoreFallbackOrder()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const HubTrack *recent = mostRecentFallbackLocked();
	activeRotationLocked() = activeCanonicalLocked();
	activeShuffledLocked() = false;
	positionAfterTrackLocked(recent);
}

void MusicHubModel::prepareFallbackForLaunch(bool shuffle, const HubTrack *continuation)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto &rotation = activeRotationLocked();
	rotation = activeCanonicalLocked();
	activeShuffledLocked() = false;
	activeCursorLocked() = 0;
	if (shuffle && rotation.size() > 1) {
		std::mt19937 generator(std::random_device{}());
		std::shuffle(rotation.begin(), rotation.end(), generator);
		activeShuffledLocked() = true;
	}
	positionAfterTrackLocked(continuation);
}

bool MusicHubModel::restartFallback(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// Starting the fallback from its beginning is an explicit interruption, not
	// a cancellation of viewer requests. A request that is currently playing or
	// waiting in forward history must return to the front of the waiting queue.
	std::deque<HubTrack> retainedRequests;
	auto retainRequest = [&](const HubTrack &request) {
		if (!request.request) return;
		const auto duplicate = std::find_if(retainedRequests.begin(), retainedRequests.end(), [&](const HubTrack &candidate) {
			return (!request.id.empty() && candidate.id == request.id) ||
				(request.id.empty() && candidate.providerId == request.providerId);
		});
		if (duplicate == retainedRequests.end()) retainedRequests.push_back(request);
	};
	if (m_hasCurrent) retainRequest(m_current);
	for (const HubTrack &forward : m_forwardHistory) retainRequest(forward);
	for (const HubTrack &waiting : m_requests) retainRequest(waiting);
	m_requests = std::move(retainedRequests);

	auto &rotation = activeRotationLocked();
	rotation = activeCanonicalLocked();
	activeShuffledLocked() = false;
	auto &cursor = activeCursorLocked();
	cursor = 0;
	if (rotation.empty()) return false;
	track = rotation.front();
	cursor = rotation.size() == 1 ? 0 : 1;
	m_forwardHistory.clear();
	return true;
}

bool MusicHubModel::fallbackShuffled() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return activeShuffledLocked();
}

bool MusicHubModel::takeNext(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// Viewer requests are always the highest-priority upcoming tracks.  A
	// track interrupted by Previous is retained, but resumes only after the
	// request queue has drained and before ordinary fallback playback.
	if (m_activeSource == "youtube" && !m_requests.empty()) { track = m_requests.front(); m_requests.pop_front(); return true; }
	if (!m_forwardHistory.empty()) {
		track = m_forwardHistory.front();
		m_forwardHistory.pop_front();
		adoptTrackSourceLocked(track);
		return true;
	}
	auto &rotation = activeRotationLocked();
	auto &cursor = activeCursorLocked();
	if (rotation.empty()) return false;
	if (cursor >= rotation.size()) cursor = 0;
	track = rotation[cursor++];
	if (cursor >= rotation.size()) cursor = 0;
	return true;
}

bool MusicHubModel::trackAt(size_t playbackIndex, HubTrack &track) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t requestCount = m_activeSource == "youtube" ? m_requests.size() : 0;
	if (playbackIndex < requestCount) { track = m_requests[playbackIndex]; return true; }
	playbackIndex -= requestCount;
	if (playbackIndex < m_forwardHistory.size()) { track = m_forwardHistory[playbackIndex]; return true; }
	const size_t fallbackOffset = playbackIndex - m_forwardHistory.size();
	const auto &rotation = activeRotationLocked();
	const size_t cursor = activeCursorLocked();
	if (rotation.empty() || fallbackOffset >= rotation.size()) return false;
	const size_t selected = (cursor + fallbackOffset) % rotation.size();
	track = rotation[selected];
	return true;
}

bool MusicHubModel::selectAt(size_t playbackIndex, HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t requestCount = m_activeSource == "youtube" ? m_requests.size() : 0;
	if (playbackIndex < requestCount) {
		track = m_requests[playbackIndex];
		m_requests.erase(m_requests.begin() + static_cast<std::ptrdiff_t>(playbackIndex));
		return true;
	}
	playbackIndex -= requestCount;
	const size_t replayCount = m_forwardHistory.size();
	if (playbackIndex < replayCount) {
		track = m_forwardHistory[playbackIndex];
		m_forwardHistory.erase(m_forwardHistory.begin() + static_cast<std::ptrdiff_t>(playbackIndex));
		return true;
	}
	playbackIndex -= replayCount;
	const size_t fallbackOffset = playbackIndex;
	auto &rotation = activeRotationLocked();
	auto &cursor = activeCursorLocked();
	if (rotation.empty() || fallbackOffset >= rotation.size()) return false;
	const size_t selected = (cursor + fallbackOffset) % rotation.size();
	track = rotation[selected];
	cursor = (selected + 1) % rotation.size();
	return true;
}

bool MusicHubModel::takePrevious(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_hasCurrent) {
		if (m_history.empty()) return false;
		track = m_history.front();
		adoptTrackSourceLocked(track);
		m_current = track; m_hasCurrent = true; return true;
	}
	if (m_history.size() < 2) return false;
	m_forwardHistory.push_front(m_current);
	m_history.pop_front(); track = m_history.front();
	adoptTrackSourceLocked(track);
	m_current = track; m_hasCurrent = true; return true;
}

void MusicHubModel::recordStarted(const HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	adoptTrackSourceLocked(track);
	m_current = track; m_hasCurrent = true;
	if (m_history.empty() || m_history.front().id != track.id) m_history.push_front(track);
	while (m_history.size() > 50) m_history.pop_back();
}

void MusicHubModel::restoreCurrent(const HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	adoptTrackSourceLocked(track);
	m_current = track; m_hasCurrent = true;
	if (m_history.empty() || m_history.front().id != track.id) m_history.push_front(track);
	while (m_history.size() > 50) m_history.pop_back();

	// A persisted fallback track resumes with its successor in the active
	// rotation. Session requests are never restored by the caller.
	if (!track.request && track.provider == m_activeSource)
		positionAfterTrackLocked(&track);
}

void MusicHubModel::restoreFallbackPosition(const HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	auto *rotation = track.provider == "local" ? &m_localRotation : &m_youtubeRotation;
	auto *cursor = track.provider == "local" ? &m_localCursor : &m_youtubeCursor;
	if (rotation->empty()) {
		*cursor = 0;
		return;
	}
	const auto found = std::find_if(rotation->begin(), rotation->end(), [&](const HubTrack &candidate) {
		return (!track.id.empty() && candidate.id == track.id) ||
			(!track.providerId.empty() && candidate.providerId == track.providerId);
	});
	*cursor = found == rotation->end() ? 0 :
		(static_cast<size_t>(std::distance(rotation->begin(), found)) + 1) % rotation->size();
}

void MusicHubModel::clearCurrent() { std::lock_guard<std::mutex> lock(m_mutex); m_current = {}; m_hasCurrent = false; }
bool MusicHubModel::hasCurrent() const { std::lock_guard<std::mutex> lock(m_mutex); return m_hasCurrent; }
HubTrack MusicHubModel::current() const { std::lock_guard<std::mutex> lock(m_mutex); return m_current; }

std::vector<HubTrack> MusicHubModel::playbackOrder() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<HubTrack> order;
	if (m_activeSource == "youtube") order.insert(order.end(), m_requests.begin(), m_requests.end());
	order.insert(order.end(), m_forwardHistory.begin(), m_forwardHistory.end());
	const auto &rotation = activeRotationLocked();
	const size_t cursor = activeCursorLocked();
	order.reserve(order.size() + rotation.size());
	for (size_t offset = 0; offset < rotation.size(); ++offset)
		order.push_back(rotation[(cursor + offset) % rotation.size()]);
	return order;
}

std::vector<HubTrack> MusicHubModel::requests() const { std::lock_guard<std::mutex> lock(m_mutex); return {m_requests.begin(), m_requests.end()}; }
std::vector<HubTrack> MusicHubModel::fallback() const { std::lock_guard<std::mutex> lock(m_mutex); return activeRotationLocked(); }
std::string MusicHubModel::fallbackLabel() const { std::lock_guard<std::mutex> lock(m_mutex); return m_fallbackLabel; }
std::string MusicHubModel::fallbackUrl() const { std::lock_guard<std::mutex> lock(m_mutex); return m_fallbackUrl; }

std::string MusicHubModel::json(const std::string &value)
{
	std::string output = "\"";
	for (unsigned char c : value) {
		if (c == '\\' || c == '"') { output.push_back('\\'); output.push_back(char(c)); }
		else if (c == '\n') output += "\\n";
		else if (c == '\r') output += "\\r";
		else if (c == '\t') output += "\\t";
		else if (c >= 0x20) output.push_back(char(c));
	}
	return output + "\"";
}

std::string MusicHubModel::trackJson(const HubTrack &track)
{
	std::ostringstream out;
	out << "{\"id\":" << json(track.id) << ",\"providerId\":" << json(track.providerId)
		<< ",\"provider\":" << json(track.provider) << ",\"title\":" << json(track.title) << ",\"artist\":" << json(track.artist)
		<< ",\"album\":" << json(track.album)
		<< ",\"artworkUrl\":" << json(track.artworkUrl) << ",\"requestedBy\":" << json(track.requestedBy)
		<< ",\"requesterId\":" << json(track.requesterId) << ",\"requesterLevel\":" << track.requesterLevel
		<< ",\"durationSeconds\":" << track.durationSeconds << ",\"discNumber\":" << track.discNumber
		<< ",\"trackNumber\":" << track.trackNumber << ",\"request\":" << (track.request ? "true" : "false")
		<< ",\"cancelled\":" << (track.cancelled ? "true" : "false") << "}";
	return out.str();
}

std::string MusicHubModel::snapshotJson(const std::string &status, int64_t positionMs, int64_t durationMs,
					bool includeLibraries) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::ostringstream out;
	out << "{\"status\":" << json(status) << ",\"positionMs\":" << positionMs << ",\"durationMs\":" << durationMs
		<< ",\"activeSource\":" << json(m_activeSource)
		<< ",\"fallbackLabel\":" << json(m_fallbackLabel) << ",\"fallbackUrl\":" << json(m_fallbackUrl)
		<< ",\"current\":" << (m_hasCurrent ? trackJson(m_current) : "null") << ",\"queue\":[";
	bool first = true;
	if (m_activeSource == "youtube") for (const HubTrack &track : m_requests) { if (!first) out << ','; first = false; out << trackJson(track); }
	for (const HubTrack &track : m_forwardHistory) { if (!first) out << ','; first = false; out << trackJson(track); }
	const auto &rotation = activeRotationLocked();
	const size_t cursor = activeCursorLocked();
	for (size_t offset = 0; offset < rotation.size(); ++offset) {
		if (!first) out << ','; first = false; out << trackJson(rotation[(cursor + offset) % rotation.size()]);
	}
	if (includeLibraries) {
		out << "],\"youtubeLibrary\":["; first = true;
		for (const HubTrack &track : m_youtubeFallback) { if (!first) out << ','; first = false; out << trackJson(track); }
		out << "],\"localLibrary\":["; first = true;
		for (const HubTrack &track : m_localLibrary) { if (!first) out << ','; first = false; out << trackJson(track); }
	}
	out << "],\"requests\":["; first = true;
	for (const HubTrack &track : m_requests) { if (!first) out << ','; first = false; out << trackJson(track); }
	out << "]}"; return out.str();
}

std::string MusicHubModel::persistentJson() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const HubTrack *youtubeContinuation = nullptr;
	const HubTrack *localContinuation = nullptr;
	auto consider = [&](const HubTrack &track) {
		if (track.request) return;
		if (track.provider == "youtube" && !youtubeContinuation) youtubeContinuation = &track;
		if (track.provider == "local" && !localContinuation) localContinuation = &track;
	};
	if (m_hasCurrent) consider(m_current);
	for (const HubTrack &track : m_history) consider(track);

	std::ostringstream out;
	out << "{\"activeSource\":" << json(m_activeSource)
		<< ",\"fallbackLabel\":" << json(m_fallbackLabel)
		<< ",\"fallbackUrl\":" << json(m_fallbackUrl)
		<< ",\"youtubeContinuation\":" << (youtubeContinuation ? trackJson(*youtubeContinuation) : "null")
		<< ",\"localContinuation\":" << (localContinuation ? trackJson(*localContinuation) : "null")
		<< ",\"youtubeLibrary\":[";
	bool first = true;
	for (const HubTrack &track : m_youtubeFallback) {
		if (!first) out << ',';
		first = false;
		out << trackJson(track);
	}
	out << "],\"localLibrary\":[";
	first = true;
	for (const HubTrack &track : m_localLibrary) {
		if (!first) out << ',';
		first = false;
		out << trackJson(track);
	}
	out << "]}";
	return out.str();
}

std::vector<HubTrack> MusicHubModel::youtubeFallback() const { std::lock_guard<std::mutex> lock(m_mutex); return m_youtubeFallback; }
std::vector<HubTrack> MusicHubModel::localLibrary() const { std::lock_guard<std::mutex> lock(m_mutex); return m_localLibrary; }
std::string MusicHubModel::activeSource() const { std::lock_guard<std::mutex> lock(m_mutex); return m_activeSource; }
