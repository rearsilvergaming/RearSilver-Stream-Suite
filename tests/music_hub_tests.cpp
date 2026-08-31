#include "music_hub.hpp"
#include "local_order.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

HubTrack track(std::string id, std::string provider = "youtube", bool request = false)
{
	HubTrack result;
	result.id = id;
	result.providerId = id;
	result.provider = std::move(provider);
	result.title = id;
	result.request = request;
	return result;
}

void require(bool condition, const char *message)
{
	if (condition)
		return;
	std::cerr << "FAILED: " << message << '\n';
	std::exit(1);
}

void requireIds(const std::vector<HubTrack> &tracks, std::initializer_list<const char *> expected,
		const char *message)
{
	require(tracks.size() == expected.size(), message);
	size_t index = 0;
	for (const char *id : expected)
		require(tracks[index++].id == id, message);
}

void requestsOutrankHistoryAndFallback()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	HubTrack next;
	require(hub.takeNext(next) && next.id == "A", "fallback starts with A");
	hub.recordStarted(next);
	require(hub.takeNext(next) && next.id == "B", "fallback advances to B");
	hub.recordStarted(next);
	HubTrack previous;
	require(hub.takePrevious(previous) && previous.id == "A", "Previous returns to A");
	hub.enqueueRequest(track("R1", "youtube", true));
	require(hub.takeNext(next) && next.id == "R1", "waiting request outranks forward history");
	require(hub.requests().empty(), "started request leaves waiting requests");
	hub.recordStarted(next);
	require(hub.takeNext(next) && next.id == "B", "forward history resumes after requests");
}

void shufflePreservesCanonicalOrder()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B"), track("C")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	hub.shuffleFallback();
	require(hub.fallbackShuffled(), "shuffle state is reported");
	requireIds(hub.youtubeFallback(), {"A", "B", "C"}, "shuffle never mutates canonical YouTube order");
	hub.restoreFallbackOrder();
	require(!hub.fallbackShuffled(), "restore order clears shuffle state");
	requireIds(hub.fallback(), {"A", "B", "C"}, "restore order rebuilds canonical rotation");
}

void restartBeginsCanonicalRotation()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B"), track("C")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	hub.shuffleFallback();
	HubTrack first;
	require(hub.restartFallback(first) && first.id == "A", "restart returns canonical first track");
	hub.recordStarted(first);
	HubTrack next;
	require(hub.takeNext(next) && next.id == "B", "restart continues with canonical second track");
}

void restoreOrderDoesNotInterruptCurrentTrack()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B"), track("C")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	HubTrack current;
	require(hub.takeNext(current) && current.id == "A", "restore-order test starts with A");
	hub.recordStarted(current);
	require(hub.takeNext(current) && current.id == "B", "restore-order test advances to B");
	hub.recordStarted(current);
	hub.shuffleFallback();
	hub.restoreFallbackOrder();
	require(hub.current().id == "B", "restore order does not interrupt the current track");
	HubTrack next;
	require(hub.takeNext(next) && next.id == "C", "restore order continues with the canonical successor");
}

void restartRetainsWaitingRequests()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	hub.enqueueRequest(track("R1", "youtube", true));
	HubTrack first;
	require(hub.restartFallback(first) && first.id == "A", "play from beginning immediately selects fallback track one");
	hub.recordStarted(first);
	HubTrack next;
	require(hub.takeNext(next) && next.id == "R1", "waiting requests survive play from beginning and regain priority");
	hub.recordStarted(next);
	require(hub.takeNext(next) && next.id == "B", "fallback resumes after retained request");
}

void restartRequeuesInterruptedAndForwardRequests()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	hub.enqueueRequest(track("R1", "youtube", true));
	HubTrack current;
	require(hub.takeNext(current) && current.id == "R1", "request becomes the current track");
	hub.recordStarted(current);
	hub.enqueueRequest(track("R2", "youtube", true));
	HubTrack first;
	require(hub.restartFallback(first) && first.id == "A", "play from beginning selects fallback track one");
	hub.recordStarted(first);
	HubTrack next;
	require(hub.takeNext(next) && next.id == "R1", "interrupted current request returns to the front of the queue");
	hub.recordStarted(next);
	require(hub.takeNext(next) && next.id == "R2", "existing waiting requests remain behind the interrupted request");

	MusicHubModel historyHub;
	historyHub.replaceFallback({track("A"), track("B")}, "Fallback", "playlist");
	historyHub.activateSource("youtube");
	historyHub.enqueueRequest(track("R3", "youtube", true));
	require(historyHub.takeNext(current) && current.id == "R3", "forward-history test starts its request");
	historyHub.recordStarted(current);
	require(historyHub.takeNext(current) && current.id == "A", "forward-history test advances to fallback");
	historyHub.recordStarted(current);
	HubTrack previous;
	require(historyHub.takePrevious(previous) && previous.id == "R3", "request enters forward history after Previous");
	require(historyHub.restartFallback(first), "forward-history test restarts fallback");
	historyHub.recordStarted(first);
	require(historyHub.takeNext(next) && next.id == "R3", "interrupted historical request is queued before fallback rotation");
}

void sourcesKeepIndependentPositions()
{
	MusicHubModel hub;
	hub.replaceFallback({track("Y1"), track("Y2")}, "Fallback", "playlist");
	hub.replaceLocalLibrary({track("L1", "local"), track("L2", "local")});
	hub.activateSource("youtube");
	HubTrack next;
	require(hub.takeNext(next) && next.id == "Y1", "YouTube starts at Y1");
	hub.recordStarted(next);
	hub.activateSource("local");
	require(hub.takeNext(next) && next.id == "L1", "Local starts at L1");
	hub.recordStarted(next);
	hub.activateSource("youtube");
	require(hub.takeNext(next) && next.id == "Y2", "YouTube cursor survives provider switching");
}

void historyTraversesAcrossSources()
{
	MusicHubModel hub;
	hub.replaceFallback({track("Y1"), track("Y2")}, "Fallback", "playlist");
	hub.replaceLocalLibrary({track("L1", "local"), track("L2", "local")});
	hub.activateSource("youtube");
	HubTrack next;
	require(hub.takeNext(next) && next.id == "Y1", "YouTube starts at Y1");
	hub.recordStarted(next);
	hub.activateSource("local");
	require(hub.takeNext(next) && next.id == "L1", "Local starts at L1");
	hub.recordStarted(next);
	HubTrack previous;
	require(hub.takePrevious(previous) && previous.id == "Y1", "history survives provider switching");
	require(hub.activeSource() == "youtube", "Previous adopts the historical YouTube source");
	require(hub.takeNext(next) && next.id == "L1", "Next traverses forward history before fallback");
	require(hub.activeSource() == "local", "forward history adopts the historical Local source");
}

void continuationMarkersRestoreIndependentPositions()
{
	MusicHubModel hub;
	hub.replaceFallback({track("Y1"), track("Y2"), track("Y3")}, "Fallback", "playlist");
	hub.replaceLocalLibrary({track("L1", "local"), track("L2", "local"), track("L3", "local")});
	hub.restoreFallbackPosition(track("Y2"));
	hub.restoreFallbackPosition(track("L1", "local"));
	hub.activateSource("youtube");
	HubTrack next;
	require(hub.takeNext(next) && next.id == "Y3", "YouTube continuation restores its successor");
	hub.activateSource("local");
	require(hub.takeNext(next) && next.id == "L2", "Local continuation restores its successor");
}

void startupPreferencesPrepareFallbackRotation()
{
	MusicHubModel hub;
	hub.replaceFallback({track("A"), track("B"), track("C")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	const HubTrack continuation = track("B");
	hub.prepareFallbackForLaunch(false, &continuation);
	HubTrack next;
	require(hub.takeNext(next) && next.id == "C", "ordered continue resumes with the canonical successor");

	hub.prepareFallbackForLaunch(false, nullptr);
	require(hub.takeNext(next) && next.id == "A", "ordered start-from-beginning selects canonical track one");

	hub.prepareFallbackForLaunch(true, nullptr);
	require(hub.fallbackShuffled(), "shuffle-on-launch marks only the active rotation shuffled");
	requireIds(hub.youtubeFallback(), {"A", "B", "C"}, "shuffle-on-launch preserves canonical library order");

	hub.prepareFallbackForLaunch(false, nullptr);
	require(!hub.fallbackShuffled(), "ordered launch restores the active rotation state");
	require(hub.takeNext(next) && next.id == "A", "ordered launch remains deterministic after shuffled launch preparation");
}

void persistenceExcludesSessionQueues()
{
	MusicHubModel hub;
	hub.replaceFallback({track("Y1"), track("Y2")}, "Fallback", "playlist");
	hub.activateSource("youtube");
	HubTrack current;
	require(hub.takeNext(current), "fallback track is available for persistence test");
	hub.recordStarted(current);
	hub.enqueueRequest(track("R99", "youtube", true));
	const std::string json = hub.persistentJson();
	require(json.find("R99") == std::string::npos, "persistent state excludes waiting requests");
	require(json.find("\"requests\"") == std::string::npos, "persistent state has no request collection");
	require(json.find("\"queue\"") == std::string::npos, "persistent state has no session queue projection");
	require(json.find("\"youtubeContinuation\"") != std::string::npos, "persistent state includes YouTube continuation");
}

void localPathsUseNaturalAlbumAndDiscOrder()
{
	std::vector<std::filesystem::path> album{
		L"10. You're My Best Friend.mp3", L"02. We Are The Champions.mp3", L"01. We Will Rock You.mp3"
	};
	std::sort(album.begin(), album.end(), naturalLocalPathLess);
	require(album[0].filename() == L"01. We Will Rock You.mp3", "local album starts with track 01");
	require(album[1].filename() == L"02. We Are The Champions.mp3", "local album continues with track 02");
	require(album[2].filename() == L"10. You're My Best Friend.mp3", "natural ordering places track 10 after track 02");

	std::vector<std::filesystem::path> discs{
		L"Disc Two/01 - Sound And Vision.mp3", L"Disc One/02 - The Man Who Sold The World.mp3",
		L"Disc One/01 - Space Oddity.mp3"
	};
	std::sort(discs.begin(), discs.end(), naturalLocalPathLess);
	require(discs[0] == std::filesystem::path(L"Disc One/01 - Space Oddity.mp3"), "disc one track one is first");
	require(discs[1] == std::filesystem::path(L"Disc One/02 - The Man Who Sold The World.mp3"), "disc one remains together");
	require(discs[2] == std::filesystem::path(L"Disc Two/01 - Sound And Vision.mp3"), "disc two follows disc one");
}

} // namespace

int main()
{
	requestsOutrankHistoryAndFallback();
	shufflePreservesCanonicalOrder();
	restartBeginsCanonicalRotation();
	restoreOrderDoesNotInterruptCurrentTrack();
	restartRetainsWaitingRequests();
	restartRequeuesInterruptedAndForwardRequests();
	sourcesKeepIndependentPositions();
	historyTraversesAcrossSources();
	continuationMarkersRestoreIndependentPositions();
	startupPreferencesPrepareFallbackRotation();
	persistenceExcludesSessionQueues();
	localPathsUseNaturalAlbumAndDiscOrder();
	std::cout << "MusicHubModel tests passed\n";
	return 0;
}
