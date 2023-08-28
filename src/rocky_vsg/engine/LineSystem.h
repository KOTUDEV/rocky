/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#pragma once
#include <rocky_vsg/LineString.h>
#include <rocky_vsg/ECS.h>

namespace ROCKY_NAMESPACE
{
    class Runtime;

    /**
     * Creates commands for rendering line primitives and holds the singleton pipeline
     * configurator for line drawing state.
     */
    class ROCKY_VSG_EXPORT LineSystem :  public vsg::Inherit<ECS::SystemNode, LineSystem>
    {
    public:
        //! Construct the line renderer
        LineSystem(entt::registry&);

        enum Features
        {
            DEFAULT = 0x0,
            WRITE_DEPTH = 1 << 0,
            NUM_PIPELINES = 2
        };

        static int featureMask(const MultiLineString&);

        void initialize(Runtime&) override;


        ROCKY_VSG_SYSTEM_HELPER(MultiLineString, helper);
    };
}
