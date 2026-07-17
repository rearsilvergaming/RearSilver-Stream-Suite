#include "music_hub.hpp"

#include <algorithm>
#include <random>
#include <sstream>

void MusicHubModel::replaceFallback(std::vector<HubTrack> tracks, std::string label, std::string sourceUrl)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	for (HubTrack &track : tracks) track.request = false;
	m_fallback = std::move(tracks);
	m_fallbackLabel = std::move(label);
	m_fallbackUrl = std::move(sourceUrl);
	m_cursor = 0;
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
	m_cursor = 0;
}

bool MusicHubModel::takeNext(HubTrack &track)
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (!m_replayNext.empty()) { track = m_replayNext.front(); m_replayNext.pop_front(); return true; }
	if (!m_requests.empty()) { track = m_requests.front(); m_requests.pop_front(); return true; }
	if (m_fallback.empty()) return false;
	if (m_cursor >= m_fallback.size()) m_cursor = 0;
	track = m_fallback[m_cursor++];
	if (m_cursor >= m_fallback.size()) m_cursor = 0;
	return true;
}

bool MusicHubModel::trackAt(size_t playbackIndex, HubTrack &track) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	if (playbackIndex < m_requests.size()) { track = m_requests[playbackIndex]; return true; }
	const size_t fallbackOffset = playbackIndex - m_requests.size();
	if (m_fallback.empty() || fallbackOffset >= m_fallback.size()) return false;
	const size_t selected = (m_cursor + fallbackOffset) % m_fallback.size();
	track = m_fallback[selected];
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
	std::vector<HubTrack> order(m_replayNext.begin(), m_replayNext.end());
	order.insert(order.end(), m_requests.begin(), m_requests.end());
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
		<< ",\"title\":" << json(track.title) << ",\"artist\":" << json(track.artist)
		<< ",\"artworkUrl\":" << json(track.artworkUrl) << ",\"requestedBy\":" << json(track.requestedBy)
		<< ",\"durationSeconds\":" << track.durationSeconds << ",\"request\":" << (track.request ? "true" : "false") << "}";
	return out.str();
}

std::string MusicHubModel::snapshotJson(const std::string &status, int64_t positionMs, int64_t durationMs) const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	std::ostringstream out;
	out << "{\"status\":" << json(status) << ",\"positionMs\":" << positionMs << ",\"durationMs\":" << durationMs
		<< ",\"fallbackLabel\":" << json(m_fallbackLabel) << ",\"fallbackUrl\":" << json(m_fallbackUrl)
		<< ",\"current\":" << (m_hasCurrent ? trackJson(m_current) : "null") << ",\"queue\":[";
	bool first = true;
	for (const HubTrack &track : m_replayNext) { if (!first) out << ','; first = false; out << trackJson(track); }
	for (const HubTrack &track : m_requests) { if (!first) out << ','; first = false; out << trackJson(track); }
	for (size_t offset = 0; offset < m_fallback.size(); ++offset) {
		if (!first) out << ','; first = false; out << trackJson(m_fallback[(m_cursor + offset) % m_fallback.size()]);
	}
	out << "]}"; return out.str();
}
