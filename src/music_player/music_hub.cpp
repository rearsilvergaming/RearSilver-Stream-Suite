#include "music_hub.hpp"

#include <algorithm>
#include <random>
#include <sstream>

void MusicHubModel::replaceFallback(std::vector<HubTrack> tracks, std::string label, std::string sourceUrl)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (HubTrack &track : tracks) { track.request = false; track.requestedBy = m_nonRequestLabel; }
	for (HubTrack &track : tracks) track.provider = "youtube";
	m_youtubeFallback = std::move(tracks);
	if (m_activeSource == "youtube") m_fallback = m_youtubeFallback;
	m_fallbackLabel = std::move(label);
	m_fallbackUrl = std::move(sourceUrl);
	m_cursor = 0;
}

void MusicHubModel::replaceLocalLibrary(std::vector<HubTrack> tracks)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (HubTrack &track : tracks) { track.request = false; track.provider = "local"; track.requestedBy = m_nonRequestLabel; }
	m_localLibrary = std::move(tracks);
	if (m_activeSource == "local") { m_fallback = m_localLibrary; m_cursor = 0; }
}

void MusicHubModel::clearLocalLibrary()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_localLibrary.clear();
	if (m_activeSource == "local") { m_fallback.clear(); m_cursor = 0; }
}

void MusicHubModel::activateSource(std::string source)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (source != "local") source = "youtube";
	m_activeSource = std::move(source);
	m_fallback = m_activeSource == "local" ? m_localLibrary : m_youtubeFallback;
	m_cursor = 0; m_replayNext.clear(); m_history.clear(); m_hasCurrent = false; m_current = {};
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
	apply(m_replayNext);
	apply(m_history);
	if (m_hasCurrent && !m_current.request) m_current.requestedBy = m_nonRequestLabel;
	m_fallback = m_activeSource == "local" ? m_localLibrary : m_youtubeFallback;
}

void MusicHubModel::enqueueRequest(HubTrack track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	track.request = true;
	m_requests.push_back(std::move(track));
}

bool MusicHubModel::removeRequest(const std::string &id)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const auto found = std::find_if(m_requests.begin(), m_requests.end(), [&](const HubTrack &track) { return track.id == id; });
	if (found == m_requests.end()) return false;
	m_requests.erase(found); return true;
}

void MusicHubModel::clearRequests() { std::lock_guard<std::mutex> lock(m_mutex); m_requests.clear(); }

void MusicHubModel::shuffleFallback()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_fallback.size() < 2) return;
	std::mt19937 generator(std::random_device{}());
	std::shuffle(m_fallback.begin(), m_fallback.end(), generator);
	if (m_activeSource == "local") m_localLibrary = m_fallback;
	else m_youtubeFallback = m_fallback;
	m_cursor = 0;
}

bool MusicHubModel::takeNext(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	// Viewer requests are always the highest-priority upcoming tracks.  A
	// track interrupted by Previous is retained, but resumes only after the
	// request queue has drained and before ordinary fallback playback.
	if (m_activeSource == "youtube" && !m_requests.empty()) { track = m_requests.front(); m_requests.pop_front(); return true; }
	if (!m_replayNext.empty()) { track = m_replayNext.front(); m_replayNext.pop_front(); return true; }
	if (m_fallback.empty()) return false;
	if (m_cursor >= m_fallback.size()) m_cursor = 0;
	track = m_fallback[m_cursor++];
	if (m_cursor >= m_fallback.size()) m_cursor = 0;
	return true;
}

bool MusicHubModel::trackAt(size_t playbackIndex, HubTrack &track) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	const size_t requestCount = m_activeSource == "youtube" ? m_requests.size() : 0;
	if (playbackIndex < requestCount) { track = m_requests[playbackIndex]; return true; }
	playbackIndex -= requestCount;
	if (playbackIndex < m_replayNext.size()) { track = m_replayNext[playbackIndex]; return true; }
	const size_t fallbackOffset = playbackIndex - m_replayNext.size();
	if (m_fallback.empty() || fallbackOffset >= m_fallback.size()) return false;
	const size_t selected = (m_cursor + fallbackOffset) % m_fallback.size();
	track = m_fallback[selected];
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
	const size_t replayCount = m_replayNext.size();
	if (playbackIndex < replayCount) {
		track = m_replayNext[playbackIndex];
		m_replayNext.erase(m_replayNext.begin() + static_cast<std::ptrdiff_t>(playbackIndex));
		return true;
	}
	playbackIndex -= replayCount;
	const size_t fallbackOffset = playbackIndex;
	if (m_fallback.empty() || fallbackOffset >= m_fallback.size()) return false;
	const size_t selected = (m_cursor + fallbackOffset) % m_fallback.size();
	track = m_fallback[selected];
	m_cursor = (selected + 1) % m_fallback.size();
	return true;
}

bool MusicHubModel::takePrevious(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (m_history.size() < 2) return false;
	if (m_hasCurrent) m_replayNext.push_front(m_current);
	m_history.pop_front(); track = m_history.front();
	m_current = track; m_hasCurrent = true; return true;
}

void MusicHubModel::recordStarted(const HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_current = track; m_hasCurrent = true;
	if (m_history.empty() || m_history.front().id != track.id) m_history.push_front(track);
	while (m_history.size() > 50) m_history.pop_back();
}

void MusicHubModel::clearCurrent() { std::lock_guard<std::mutex> lock(m_mutex); m_current = {}; m_hasCurrent = false; }
bool MusicHubModel::hasCurrent() const { std::lock_guard<std::mutex> lock(m_mutex); return m_hasCurrent; }
HubTrack MusicHubModel::current() const { std::lock_guard<std::mutex> lock(m_mutex); return m_current; }

std::vector<HubTrack> MusicHubModel::playbackOrder() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::vector<HubTrack> order;
	if (m_activeSource == "youtube") order.insert(order.end(), m_requests.begin(), m_requests.end());
	order.insert(order.end(), m_replayNext.begin(), m_replayNext.end());
	order.reserve(order.size() + m_fallback.size());
	for (size_t offset = 0; offset < m_fallback.size(); ++offset)
		order.push_back(m_fallback[(m_cursor + offset) % m_fallback.size()]);
	return order;
}

std::vector<HubTrack> MusicHubModel::requests() const { std::lock_guard<std::mutex> lock(m_mutex); return {m_requests.begin(), m_requests.end()}; }
std::vector<HubTrack> MusicHubModel::fallback() const { std::lock_guard<std::mutex> lock(m_mutex); return m_fallback; }
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
		<< ",\"durationSeconds\":" << track.durationSeconds << ",\"request\":" << (track.request ? "true" : "false") << "}";
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
	for (const HubTrack &track : m_replayNext) { if (!first) out << ','; first = false; out << trackJson(track); }
	for (size_t offset = 0; offset < m_fallback.size(); ++offset) {
		if (!first) out << ','; first = false; out << trackJson(m_fallback[(m_cursor + offset) % m_fallback.size()]);
	}
	if (includeLibraries) {
		out << "],\"youtubeLibrary\":["; first = true;
		for (const HubTrack &track : m_youtubeFallback) { if (!first) out << ','; first = false; out << trackJson(track); }
		out << "],\"localLibrary\":["; first = true;
		for (const HubTrack &track : m_localLibrary) { if (!first) out << ','; first = false; out << trackJson(track); }
	}
	out << "]}"; return out.str();
}

std::vector<HubTrack> MusicHubModel::youtubeFallback() const { std::lock_guard<std::mutex> lock(m_mutex); return m_youtubeFallback; }
std::vector<HubTrack> MusicHubModel::localLibrary() const { std::lock_guard<std::mutex> lock(m_mutex); return m_localLibrary; }
std::string MusicHubModel::activeSource() const { std::lock_guard<std::mutex> lock(m_mutex); return m_activeSource; }
