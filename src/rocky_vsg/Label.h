/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky_vsg/ECS.h>
#include <vsg/text/Text.h>
#include <vsg/text/Font.h>
#include <vsg/text/StandardLayout.h>

namespace ROCKY_NAMESPACE
{
    /**
    * Text label MapObject attachment
    */
    class ROCKY_VSG_EXPORT Label : public ECS::NodeComponent
    {
    public:
        Label();

        std::string text;

        vsg::ref_ptr<vsg::Font> font;

        void dirty();

        //! serialize as JSON string
        JSON to_json() const override;

        void initializeNode(const ECS::VSG_ComponentParams&) override;

    protected:
        vsg::ref_ptr<vsg::Text> textNode;
        vsg::ref_ptr<vsg::stringValue> valueBuffer;
        vsg::ref_ptr<vsg::StandardLayout> layout;
        vsg::ref_ptr<vsg::Options> options;
    };
}
