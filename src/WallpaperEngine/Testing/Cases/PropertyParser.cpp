#include <catch2/catch_test_macros.hpp>

#include "WallpaperEngine/Data/Model/Property.h"
#include "WallpaperEngine/Data/Parsers/PropertyParser.h"

using WallpaperEngine::Data::JSON::JSON;
using WallpaperEngine::Data::Model::DynamicValue;
using WallpaperEngine::Data::Parsers::PropertyParser;

TEST_CASE ("Bool properties without a value default to false") {
    const JSON propertyData = {
	{ "type", "bool" },
	{ "text", "Enabled" },
    };

    const auto property = PropertyParser::parse (propertyData, "enabled");

    REQUIRE (property != nullptr);
    CHECK (property->getType () == DynamicValue::Boolean);
    CHECK_FALSE (property->getBool ());
}

TEST_CASE ("Directory properties are parsed as file-like properties") {
    const JSON propertyData = {
	{ "type", "directory" },
	{ "text", "Folder" },
    };

    const auto property = PropertyParser::parse (propertyData, "folder");

    REQUIRE (property != nullptr);
    CHECK (property->dump ().find ("folder - file") != std::string::npos);
    CHECK (property->describe ()["type"] == "file");
    CHECK (property->describe ()["value"] == "");
}

TEST_CASE ("Text input properties preserve their editable string value") {
    const JSON propertyData = {
	{ "type", "textinput" },
	{ "text", "Message" },
	{ "value", "Hello, wallpaper" },
    };

    const auto property = PropertyParser::parse (propertyData, "message");

    REQUIRE (property != nullptr);
    CHECK (property->toString () == "Hello, wallpaper");
    CHECK (property->describe ()["value"] == "Hello, wallpaper");
}

TEST_CASE ("Slider property descriptions include editable bounds") {
    const JSON propertyData = {
	{ "type", "slider" },
	{ "text", "Brightness" },
	{ "min", 0.0 },
	{ "max", 2.0 },
	{ "step", 0.25 },
	{ "value", 1.0 },
    };

    const auto property = PropertyParser::parse (propertyData, "brightness");

    REQUIRE (property != nullptr);
    const auto description = property->describe ();
    CHECK (description["type"] == "slider");
    CHECK (description["value"] == 1.0);
    CHECK (description["min"] == 0.0);
    CHECK (description["max"] == 2.0);
    CHECK (description["step"] == 0.25);
}