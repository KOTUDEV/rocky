/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky/Image.h>
#include <rocky_vsg/ECS.h>
#include <optional>

namespace ROCKY_NAMESPACE
{
    /**
     * Render settings for an icon.
     */
    struct IconStyle
    {
        float size_pixels = 256.0f;
        float rotation_radians = 0.0f;
        float padding[2];
    };

    /**
    * Applies a style.
    */
    class ROCKY_VSG_EXPORT BindIconStyle : public vsg::Inherit<vsg::BindDescriptorSet, BindIconStyle>
    {
    public:
        //! Construct a default styling command
        BindIconStyle();

        //! Initialize this command with the associated layout
        void init(vsg::ref_ptr<vsg::PipelineLayout> layout);

        //! Refresh the data buffer contents on the GPU
        void updateStyle(const IconStyle&);

        //! Image to render to the icon
        //void setImage(std::shared_ptr<Image> image);
        //std::shared_ptr<Image> image() const;

        std::shared_ptr<Image> _image;
        vsg::ref_ptr<vsg::ubyteArray> _styleData;
        vsg::ref_ptr<vsg::Data> _imageData;
    };

    /**
    * Renders an icon geometry.
    */
    class ROCKY_VSG_EXPORT IconGeometry : public vsg::Inherit<vsg::Geometry, IconGeometry>
    {
    public:
        //! Construct a new line string geometry node
        IconGeometry();

        //! Recompile the geometry after making changes.
        //! TODO: just make it dynamic instead
        void compile(vsg::Context&) override;

    protected:
        vsg::ref_ptr<vsg::Draw> _drawCommand;
    };

    /**
    * Icon attachment
    */
    class ROCKY_VSG_EXPORT Icon : public ECS::NodeComponent
    {
    public:
        //! Construct
        Icon();

        IconStyle style;

        std::shared_ptr<Image> image;

        //! serialize as JSON string
        JSON to_json() const override;

        //! Call after changing the style or image
        void dirty();

    public: // NodeComponent

        void initializeNode(const ECS::VSG_ComponentParams&) override;

        int featureMask() const override;

    private:
        vsg::ref_ptr<BindIconStyle> bindCommand;
        vsg::ref_ptr<IconGeometry> geometry;
        friend class IconSystem;
    };
}
