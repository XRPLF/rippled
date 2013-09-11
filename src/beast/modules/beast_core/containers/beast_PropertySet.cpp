//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

    Portions of this file are from JUCE.
    Copyright (c) 2013 - Raw Material Software Ltd.
    Please visit http://www.juce.com

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

PropertySet::PropertySet (const bool ignoreCaseOfKeyNames)
    : properties (ignoreCaseOfKeyNames),
      fallbackProperties (nullptr),
      ignoreCaseOfKeys (ignoreCaseOfKeyNames)
{
}

PropertySet::PropertySet (const PropertySet& other)
    : properties (other.properties),
      fallbackProperties (other.fallbackProperties),
      ignoreCaseOfKeys (other.ignoreCaseOfKeys)
{
}

PropertySet& PropertySet::operator= (const PropertySet& other)
{
    properties = other.properties;
    fallbackProperties = other.fallbackProperties;
    ignoreCaseOfKeys = other.ignoreCaseOfKeys;

    propertyChanged();
    return *this;
}

PropertySet::~PropertySet()
{
}

void PropertySet::clear()
{
    const ScopedLock sl (lock);

    if (properties.size() > 0)
    {
        properties.clear();
        propertyChanged();
    }
}

String PropertySet::getValue (const String& keyName,
                              const String& defaultValue) const noexcept
{
    const ScopedLock sl (lock);

    const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

    if (index >= 0)
        return properties.getAllValues() [index];

    return fallbackProperties != nullptr ? fallbackProperties->getValue (keyName, defaultValue)
                                         : defaultValue;
}

int PropertySet::getIntValue (const String& keyName,
                              const int defaultValue) const noexcept
{
    const ScopedLock sl (lock);
    const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

    if (index >= 0)
        return properties.getAllValues() [index].getIntValue();

    return fallbackProperties != nullptr ? fallbackProperties->getIntValue (keyName, defaultValue)
                                         : defaultValue;
}

double PropertySet::getDoubleValue (const String& keyName,
                                    const double defaultValue) const noexcept
{
    const ScopedLock sl (lock);
    const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

    if (index >= 0)
        return properties.getAllValues()[index].getDoubleValue();

    return fallbackProperties != nullptr ? fallbackProperties->getDoubleValue (keyName, defaultValue)
                                         : defaultValue;
}

bool PropertySet::getBoolValue (const String& keyName,
                                const bool defaultValue) const noexcept
{
    const ScopedLock sl (lock);
    const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

    if (index >= 0)
        return properties.getAllValues() [index].getIntValue() != 0;

    return fallbackProperties != nullptr ? fallbackProperties->getBoolValue (keyName, defaultValue)
                                         : defaultValue;
}

XmlElement* PropertySet::getXmlValue (const String& keyName) const
{
    return XmlDocument::parse (getValue (keyName));
}

void PropertySet::setValue (const String& keyName, const var& v)
{
    bassert (keyName.isNotEmpty()); // shouldn't use an empty key name!

    if (keyName.isNotEmpty())
    {
        const String value (v.toString());
        const ScopedLock sl (lock);

        const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

        if (index < 0 || properties.getAllValues() [index] != value)
        {
            properties.set (keyName, value);
            propertyChanged();
        }
    }
}

void PropertySet::removeValue (const String& keyName)
{
    if (keyName.isNotEmpty())
    {
        const ScopedLock sl (lock);
        const int index = properties.getAllKeys().indexOf (keyName, ignoreCaseOfKeys);

        if (index >= 0)
        {
            properties.remove (keyName);
            propertyChanged();
        }
    }
}

void PropertySet::setValue (const String& keyName, const XmlElement* const xml)
{
    setValue (keyName, xml == nullptr ? var::null
                                      : var (xml->createDocument (String::empty, true)));
}

bool PropertySet::containsKey (const String& keyName) const noexcept
{
    const ScopedLock sl (lock);
    return properties.getAllKeys().contains (keyName, ignoreCaseOfKeys);
}

void PropertySet::addAllPropertiesFrom (const PropertySet& source)
{
    const ScopedLock sl (source.getLock());

    for (int i = 0; i < source.properties.size(); ++i)
        setValue (source.properties.getAllKeys() [i],
                  source.properties.getAllValues() [i]);
}

void PropertySet::setFallbackPropertySet (PropertySet* fallbackProperties_) noexcept
{
    const ScopedLock sl (lock);
    fallbackProperties = fallbackProperties_;
}

XmlElement* PropertySet::createXml (const String& nodeName) const
{
    const ScopedLock sl (lock);
    XmlElement* const xml = new XmlElement (nodeName);

    for (int i = 0; i < properties.getAllKeys().size(); ++i)
    {
        XmlElement* const e = xml->createNewChildElement ("VALUE");
        e->setAttribute ("name", properties.getAllKeys()[i]);
        e->setAttribute ("val", properties.getAllValues()[i]);
    }

    return xml;
}

void PropertySet::restoreFromXml (const XmlElement& xml)
{
    const ScopedLock sl (lock);
    clear();

    forEachXmlChildElementWithTagName (xml, e, "VALUE")
    {
        if (e->hasAttribute ("name")
             && e->hasAttribute ("val"))
        {
            properties.set (e->getStringAttribute ("name"),
                            e->getStringAttribute ("val"));
        }
    }

    if (properties.size() > 0)
        propertyChanged();
}

void PropertySet::propertyChanged()
{
}
