/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky_vsg/ECS.h>
#include <vsg/nodes/Geometry.h>
#include <vsg/commands/DrawIndexed.h>
#include <vsg/state/BindDescriptorSet.h>
#include <optional>

namespace ROCKY_NAMESPACE
{
    //! Settings when constructing a similar set of line drawables
    //! Note, this structure is mirrored on the GPU so alignment rules apply!
    struct LineStyle
    {
        // if alpha is zero, use the line's per-vertex color instead
        vsg::vec4 color = { 1, 1, 1, 0 };
        float width = 2.0f; // pixels
        int stipple_pattern = 0xffff;
        int stipple_factor = 1;
        float resolution = 100000.0f; // meters
        float depth_offset = 1e-7f; // tbd
    };

    /**
    * Renders a line or linestring geometry.
    */
    class ROCKY_VSG_EXPORT LineStringGeometry : public vsg::Inherit<vsg::Geometry, LineStringGeometry>
    {
    public:
        //! Construct a new line string geometry node
        LineStringGeometry();

        //! Adds a vertex to the end of the line string
        void push_back(const vsg::vec3& vert);

        //! Number of verts comprising this line string
        unsigned numVerts() const;

        //! The first vertex in the line string to render
        void setFirst(unsigned value);

        //! Number of vertices in the line string to render
        void setCount(unsigned value);

        //! Recompile the geometry after making changes.
        //! TODO: just make it dynamic instead
        void compile(vsg::Context&) override;

    protected:
        vsg::vec4 _defaultColor = { 1,1,1,1 };
        std::vector<vsg::vec3> _current;
        std::vector<vsg::vec3> _previous;
        std::vector<vsg::vec3> _next;
        std::vector<vsg::vec4> _colors;
        vsg::ref_ptr<vsg::DrawIndexed> _drawCommand;
    };

    /**
    * Applies a line style.
    */
    class ROCKY_VSG_EXPORT BindLineDescriptors : public vsg::Inherit<vsg::BindDescriptorSet, BindLineDescriptors>
    {
    public:
        //! Construct a line style node
        BindLineDescriptors();

        //! Initialize this command with the associated layout
        void init(vsg::ref_ptr<vsg::PipelineLayout> layout);

        //! Refresh the data buffer contents on the GPU
        void updateStyle(const LineStyle&);

        vsg::ref_ptr<vsg::ubyteArray> _styleData;
    };

    /**
    * MultiLineString component - holds one or more separate line string geometries
    * sharing the same style.
    */
    class ROCKY_VSG_EXPORT MultiLineString : public ECS::NodeComponent
    {
    public:
        //! Construct a new multi-linestring attachment
        MultiLineString();

        std::optional<LineStyle> style;

        bool write_depth = false;

        //! Pushes a new sub-geometry along with its range of points.
        //! @param begin Iterator of first point to add to the new sub-geometry
        //! @param end Iterator past the final point to add to the new sub-geometry
        template<class VEC3_ITER>
        inline void push(VEC3_ITER begin, VEC3_ITER end);

        //! If using style, call this after changing a style to apply it
        void dirty();

        //! serialize as JSON string
        JSON to_json() const override;

    public: // NodeComponent

        void initializeNode(const ECS::VSG_ComponentParams&) override;

        int featureMask() const override;

    private:
        vsg::ref_ptr<BindLineDescriptors> bindCommand;
        std::vector<vsg::ref_ptr<LineStringGeometry>> geometries;

        friend class LineSystem;
    };

    // inline implementations
    template<class VEC3_ITER> void MultiLineString::push(VEC3_ITER begin, VEC3_ITER end) {
        auto geom = LineStringGeometry::create();
        for (VEC3_ITER i = begin; i != end; ++i)
            geom->push_back({ (float)i->x, (float)i->y, (float)i->z });
        geometries.push_back(geom);
    }
}
