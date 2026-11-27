
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
