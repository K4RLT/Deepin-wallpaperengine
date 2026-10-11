#pragma once

#include "../Builders/ColorBuilder.h"
#include "../Utils/TypeCaster.h"
#include "DynamicValue.h"
#include "WallpaperEngine/Data/JSON.h"
#include "WallpaperEngine/Logging/Log.h"

#include <map>
#include <string>
#include <utility>

namespace WallpaperEngine::Data::Model {
using namespace WallpaperEngine::Data::Utils;
using namespace WallpaperEngine::Data::Builders;
using WallpaperEngine::Data::JSON::JSON;

struct PropertyData {
    std::string name;
    std::string text;
};

struct SliderData {
    float min;
    float max;
    float step;
};

struct ComboData {
    std::map<std::string, std::string> values;
};

class Property : public DynamicValue, public TypeCaster, public PropertyData {
public:
    explicit Property (PropertyData data) : DynamicValue (), TypeCaster (), PropertyData (std::move (data)) { }

    using DynamicValue::update;
    virtual void update (const std::string& value, UpdateSource source) = 0;
    [[nodiscard]] virtual std::string dump () const = 0;
    [[nodiscard]] virtual JSON describe () const = 0;

protected:
    [[nodiscard]] JSON description (const std::string& type) const {
	return JSON {
	    { "name", this->name },
	    { "text", this->text },
	    { "type", type },
	    { "value", this->toString () },
	};
    }
};

class PropertySlider final : public Property, SliderData {
public:
    PropertySlider (PropertyData data, SliderData sliderData, const float value) :
	Property (std::move (data)), SliderData (sliderData) {
	this->Property::update (value, UpdateSource::Initialization);
    }

    using Property::update;
    void update (const std::string& value, UpdateSource source) override { this->update (std::stof (value), source); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - slider" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tMin: " << this->min << std::endl
	   << "\tMax: " << this->max << std::endl
	   << "\tStep: " << this->step << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override {
	JSON result = this->description ("slider");
	result["value"] = this->getFloat ();
	result["min"] = this->min;
	result["max"] = this->max;
	result["step"] = this->step;
	return result;
    }
};

class PropertyBoolean final : public Property {
public:
    explicit PropertyBoolean (PropertyData data, const bool value) : Property (std::move (data)) {
	this->Property::update (value, UpdateSource::Initialization);
    }

    using Property::update;
    void update (const std::string& value, UpdateSource source) override {
	this->update (value == "true" || value == "1", source);
    }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - boolean" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override {
	JSON result = this->description ("boolean");
	result["value"] = this->getBool ();
	return result;
    }
};

class PropertyColor final : public Property {
public:
    explicit PropertyColor (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyColor::update (value, UpdateSource::Initialization);
    }

    using Property::update;
    void update (const std::string& value, UpdateSource source) override {
	this->update (ColorBuilder::parse (value), source);
    }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - color" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override { return this->description ("color"); }
};

class PropertyCombo final : public Property, ComboData {
public:
    PropertyCombo (PropertyData data, ComboData comboData, const std::string& value) :
	Property (std::move (data)), ComboData (std::move (comboData)) {
	this->PropertyCombo::update (value, UpdateSource::Initialization);
    }

    using Property::update;
    void update (const std::string& value, UpdateSource source) override {
	if (this->values.contains (value) == false) {
	    sLog.error ("Combo value not found in combo options: ", value);
	    return;
	}

	// search for the value in the combo options or default to the textual value
	this->DynamicValue::update (value, source);
    }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - combo" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl
	   << "Values: " << std::endl;

	for (const auto& [key, value] : this->values) {
	    ss << "\t\t" << key << " = " << value << std::endl;
	}

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override {
	JSON result = this->description ("combo");
	result["options"] = JSON::array ();
	for (const auto& [value, label] : this->values) {
	    result["options"].push_back ({ { "value", value }, { "label", label } });
	}
	return result;
    }
};

class PropertyText final : public Property {
public:
    explicit PropertyText (PropertyData data) : Property (std::move (data)) { }

    using Property::update;
    void update (const std::string& value, UpdateSource source) override { this->DynamicValue::update (value, source); }

    [[nodiscard]] std::string toString () const override { return this->text; }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - text" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override { return this->description ("text"); }
};

class PropertySceneTexture final : public Property {
public:
    explicit PropertySceneTexture (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertySceneTexture::update (value, UpdateSource::Initialization);
    }

    void update (const std::string& value, UpdateSource source) override { this->DynamicValue::update (value, source); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - scene texture" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override { return this->description ("scenetexture"); }
};

class PropertyFile final : public Property {
public:
    explicit PropertyFile (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyFile::update (value, UpdateSource::Initialization);
    }

    void update (const std::string& value, UpdateSource source) override { this->DynamicValue::update (value, source); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - file" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override { return this->description ("file"); }
};

class PropertyTextInput final : public Property {
public:
    explicit PropertyTextInput (PropertyData data, const std::string& value) : Property (std::move (data)) {
	this->PropertyTextInput::update (value, UpdateSource::Initialization);
    }

    void update (const std::string& value, UpdateSource source) override { this->DynamicValue::update (value, source); }

    [[nodiscard]] std::string dump () const override {
	std::stringstream ss;

	ss << this->name << " - textinput" << std::endl
	   << "\tText: " << this->text << std::endl
	   << "\tValue: " << this->toString () << std::endl;

	return ss.str ();
    }

    [[nodiscard]] JSON describe () const override { return this->description ("textinput"); }
};
}