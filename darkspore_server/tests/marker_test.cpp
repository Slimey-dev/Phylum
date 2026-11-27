
// Include
#include "game/level.h"

#include <doctest/doctest.h>

#include <pugixml.hpp>

#include <string>

namespace {
	// A teleporter marker as recap_parser writes it, with its data under the given node name.
	Game::Marker ReadTeleporterMarker(const std::string& componentNode) {
		const std::string xml =
			"<entry>"
				"<markerName>SecurityTeleporter.Noun-5</markerName>"
				"<markerId>1432124960</markerId>"
				"<nounDef>SecurityTeleporter.Noun</nounDef>"
				"<" + componentNode + ">"
					"<teleporter>"
						"<destinationMarkerId>1104450085</destinationMarkerId>"
						"<deferTriggerCreation>false</deferTriggerCreation>"
					"</teleporter>"
					"<interactable>"
						"<interactableAbility>OpenChest</interactableAbility>"
						"<numUsesAllowed>1</numUsesAllowed>"
					"</interactable>"
				"</" + componentNode + ">"
			"</entry>";

		pugi::xml_document document;
		REQUIRE(document.load_string(xml.c_str()));

		Game::Marker marker;
		marker.Read(document.child("entry"));
		return marker;
	}
}

TEST_CASE("a marker reads its teleporter and interactable from SharedComponentData") {
	const auto marker = ReadTeleporterMarker("SharedComponentData");
	REQUIRE(marker.GetTeleporterData());
	CHECK(marker.GetTeleporterData()->GetDestinationId() == 1104450085);
	CHECK(marker.GetInteractableData());
}

TEST_CASE("a marker still reads the older componentData node") {
	const auto marker = ReadTeleporterMarker("componentData");
	REQUIRE(marker.GetTeleporterData());
	CHECK(marker.GetTeleporterData()->GetDestinationId() == 1104450085);
}

namespace {
	// A director spawn point marker, with the given spawnPointDef body (empty for none).
	Game::Marker ReadSpawnPointMarker(const std::string& spawnPointDef) {
		const std::string xml =
			"<entry>"
				"<markerName>SpawnPoint_DirectorSpike.Noun</markerName>"
				"<markerId>1494824854</markerId>"
				"<nounDef>SpawnPoint_DirectorSpike.Noun</nounDef>"
				"<SharedComponentData>" + spawnPointDef + "</SharedComponentData>"
			"</entry>";

		pugi::xml_document document;
		REQUIRE(document.load_string(xml.c_str()));

		Game::Marker marker;
		marker.Read(document.child("entry"));
		return marker;
	}

	pugi::xml_node LoadNode(pugi::xml_document& document, const std::string& xml, const char* name) {
		REQUIRE(document.load_string(xml.c_str()));
		return document.child(name);
	}
}

TEST_CASE("a marker reads its spawn section and spike flag from spawnPointDef") {
	const auto marker = ReadSpawnPointMarker(
		"<spawnPointDef><sectionType>1</sectionType><activatesSpike>false</activatesSpike></spawnPointDef>");
	CHECK(marker.GetSpawnSection() == 1);
	CHECK_FALSE(marker.ActivatesSpike());

	// A missing field keeps the nouns' default
	const auto sectionOnly = ReadSpawnPointMarker("<spawnPointDef><sectionType>2</sectionType></spawnPointDef>");
	CHECK(sectionOnly.GetSpawnSection() == 2);
	CHECK(sectionOnly.ActivatesSpike());
}

TEST_CASE("a marker without spawnPointDef is in any section and activates spikes") {
	const auto marker = ReadSpawnPointMarker("");
	CHECK(marker.GetSpawnSection() == 3);
	CHECK(marker.ActivatesSpike());

	const auto empty = ReadSpawnPointMarker("<spawnPointDef />");
	CHECK(empty.GetSpawnSection() == 3);
	CHECK(empty.ActivatesSpike());
}

TEST_CASE("a markerset reads the group and weight after its markers") {
	pugi::xml_document document;
	Game::Markerset markerset;
	markerset.Read(LoadNode(document,
		"<markerset><markers /><group>AI_SpikeB2</group><weight>2.00000</weight></markerset>", "markerset"));
	CHECK(markerset.GetGroup() == "AI_SpikeB2");
	CHECK(markerset.GetWeight() == 2.f);

	pugi::xml_document bareDocument;
	Game::Markerset bare;
	bare.Read(LoadNode(bareDocument, "<markerset><markers /></markerset>", "markerset"));
	CHECK(bare.GetGroup().empty());
	CHECK(bare.GetWeight() == 1.f);
}

TEST_CASE("a level's markerset entry has its own group and weight, else the markerset's, else none and 1") {
	pugi::xml_document markersetDocument;
	Game::Markerset markerset;
	markerset.Read(LoadNode(markersetDocument,
		"<markerset><markers /><group>Horde</group><weight>5.00000</weight></markerset>", "markerset"));

	pugi::xml_document own;
	Game::MarkersetEntry entry;
	entry.Read(LoadNode(own,
		"<entry><markersetAsset>zelems_1_Ai_Horde_1.Markerset</markersetAsset><group>Horde</group><weight>2</weight></entry>", "entry"),
		&markerset);
	CHECK(entry.name == "zelems_1_Ai_Horde_1.Markerset");
	CHECK(entry.group == "Horde");
	CHECK(entry.weight == 2.f);

	pugi::xml_document fromMarkerset;
	Game::MarkersetEntry inherited;
	inherited.Read(LoadNode(fromMarkerset, "<entry><markersetAsset>zelems_1_Ai_Horde_4.Markerset</markersetAsset></entry>", "entry"),
		&markerset);
	CHECK(inherited.group == "Horde");
	CHECK(inherited.weight == 5.f);

	pugi::xml_document bareDocument;
	Game::MarkersetEntry bare;
	bare.Read(LoadNode(bareDocument, "<entry><markersetAsset>zelems_1_default.Markerset</markersetAsset></entry>", "entry"));
	CHECK(bare.group.empty());
	CHECK(bare.weight == 1.f);
}
