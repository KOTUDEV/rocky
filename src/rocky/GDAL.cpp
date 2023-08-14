/**
 * rocky c++
 * Copyright 2023 Pelican Mapping
 * MIT License
 */
#include "GDAL.h"
#include "ElevationLayer.h" // for NO_DATA_VALUE
#include <gdal.h>
#include <gdalwarper.h>
#include <ogr_spatialref.h>
#include <filesystem>

using namespace ROCKY_NAMESPACE;
using namespace ROCKY_NAMESPACE::GDAL;

#undef LC
#define LC "[GDAL] \"" << getName() << "\" "

#define INDENT ""

#if (GDAL_VERSION_MAJOR > 1 || (GDAL_VERSION_MAJOR >= 1 && GDAL_VERSION_MINOR >= 5))
#  define GDAL_VERSION_1_5_OR_NEWER 1
#endif

#if (GDAL_VERSION_MAJOR > 1 || (GDAL_VERSION_MAJOR >= 1 && GDAL_VERSION_MINOR >= 6))
#  define GDAL_VERSION_1_6_OR_NEWER 1
#endif

#ifndef GDAL_VERSION_1_5_OR_NEWER
#  error "**** GDAL 1.5 or newer required ****"
#endif

//GDAL proxy is only available after GDAL 1.6
#if GDAL_VERSION_1_6_OR_NEWER
#  include <gdal_proxy.h>
#endif

#if (GDAL_VERSION_MAJOR >= 2)
#  define GDAL_VERSION_2_0_OR_NEWER 1
#endif

#include <cpl_string.h>

//GDAL VRT api is only available after 1.5.0
#include <gdal_vrt.h>

#define GEOTRSFRM_TOPLEFT_X            0
#define GEOTRSFRM_WE_RES               1
#define GEOTRSFRM_ROTATION_PARAM1      2
#define GEOTRSFRM_TOPLEFT_Y            3
#define GEOTRSFRM_ROTATION_PARAM2      4
#define GEOTRSFRM_NS_RES               5

namespace ROCKY_NAMESPACE
{
    namespace GDAL
    {
        // From easyrgb.com
        float Hue_2_RGB(float v1, float v2, float vH)
        {
            if (vH < 0.0f) vH += 1.0f;
            if (vH > 1.0f) vH -= 1.0f;
            if ((6.0f * vH) < 1.0f) return (v1 + (v2 - v1) * 6.0f * vH);
            if ((2.0f * vH) < 1.0f) return (v2);
            if ((3.0f * vH) < 2.0f) return (v1 + (v2 - v1) * ((2.0f / 3.0f) - vH) * 6.0f);
            return (v1);
        }

#ifndef GDAL_VERSION_2_0_OR_NEWER
        // RasterIO was substantially improved in 2.0
        // See https://trac.osgeo.org/gdal/wiki/rfc51_rasterio_resampling_progress
        typedef int GSpacing;
#endif

        typedef enum
        {
            LOWEST_RESOLUTION,
            HIGHEST_RESOLUTION,
            AVERAGE_RESOLUTION
        } ResolutionStrategy;

        typedef struct
        {
            int    isFileOK;
            int    nRasterXSize;
            int    nRasterYSize;
            double adfGeoTransform[6];
            int    nBlockXSize;
            int    nBlockYSize;
        } DatasetProperty;

        typedef struct
        {
            GDALColorInterp        colorInterpretation;
            GDALDataType           dataType;
            GDALColorTableH        colorTable;
            int                    bHasNoData;
            double                 noDataValue;
        } BandProperty;

        // This is simply the method GDALAutoCreateWarpedVRT() with the GDALSuggestedWarpOutput
        // logic replaced with something that will work properly for polar projections.
        // see: http://www.mail-archive.com/gdal-dev@lists.osgeo.org/msg01491.html
        GDALDatasetH GDALAutoCreateWarpedVRTforPolarStereographic(
            GDALDatasetH hSrcDS,
            const char *pszSrcWKT,
            const char *pszDstWKT,
            GDALResampleAlg eResampleAlg,
            double dfMaxError,
            const GDALWarpOptions *psOptionsIn)
        {
            GDALWarpOptions *psWO;
            int i;

            VALIDATE_POINTER1(hSrcDS, "GDALAutoCreateWarpedVRTForPolarStereographic", NULL);

            /* -------------------------------------------------------------------- */
            /*      Populate the warp options.                                      */
            /* -------------------------------------------------------------------- */
            if (psOptionsIn != NULL)
                psWO = GDALCloneWarpOptions(psOptionsIn);
            else
                psWO = GDALCreateWarpOptions();

            psWO->eResampleAlg = eResampleAlg;

            psWO->hSrcDS = hSrcDS;

            psWO->nBandCount = GDALGetRasterCount(hSrcDS);
            psWO->panSrcBands = (int *)CPLMalloc(sizeof(int) * psWO->nBandCount);
            psWO->panDstBands = (int *)CPLMalloc(sizeof(int) * psWO->nBandCount);

            for (i = 0; i < psWO->nBandCount; i++)
            {
                psWO->panSrcBands[i] = i + 1;
                psWO->panDstBands[i] = i + 1;
            }

            /* TODO: should fill in no data where available */

            /* -------------------------------------------------------------------- */
            /*      Create the transformer.                                         */
            /* -------------------------------------------------------------------- */
            psWO->pfnTransformer = GDALGenImgProjTransform;
            psWO->pTransformerArg =
                GDALCreateGenImgProjTransformer(psWO->hSrcDS, pszSrcWKT,
                    NULL, pszDstWKT,
                    TRUE, 1.0, 0);

            if (psWO->pTransformerArg == NULL)
            {
                GDALDestroyWarpOptions(psWO);
                return NULL;
            }

            /* -------------------------------------------------------------------- */
            /*      Figure out the desired output bounds and resolution.            */
            /* -------------------------------------------------------------------- */
            double adfDstGeoTransform[6];
            int    nDstPixels, nDstLines;
            CPLErr eErr;

            eErr =
                GDALSuggestedWarpOutput(hSrcDS, psWO->pfnTransformer,
                    psWO->pTransformerArg,
                    adfDstGeoTransform, &nDstPixels, &nDstLines);

            // override the suggestions:
            nDstPixels = GDALGetRasterXSize(hSrcDS) * 4;
            nDstLines = GDALGetRasterYSize(hSrcDS) / 2;
            adfDstGeoTransform[0] = -180.0;
            adfDstGeoTransform[1] = 360.0 / (double)nDstPixels;
            //adfDstGeoTransform[2] = 0.0;
            //adfDstGeoTransform[4] = 0.0;
            //adfDstGeoTransform[5] = (-90 -adfDstGeoTransform[3])/(double)nDstLines;

            /* -------------------------------------------------------------------- */
            /*      Update the transformer to include an output geotransform        */
            /*      back to pixel/line coordinates.                                 */
            /*                                                                      */
            /* -------------------------------------------------------------------- */
            GDALSetGenImgProjTransformerDstGeoTransform(
                psWO->pTransformerArg, adfDstGeoTransform);

            /* -------------------------------------------------------------------- */
            /*      Do we want to apply an approximating transformation?            */
            /* -------------------------------------------------------------------- */
            if (dfMaxError > 0.0)
            {
                psWO->pTransformerArg =
                    GDALCreateApproxTransformer(psWO->pfnTransformer,
                        psWO->pTransformerArg,
                        dfMaxError);
                psWO->pfnTransformer = GDALApproxTransform;
            }

            /* -------------------------------------------------------------------- */
            /*      Create the VRT file.                                            */
            /* -------------------------------------------------------------------- */
            GDALDatasetH hDstDS;

            hDstDS = GDALCreateWarpedVRT(hSrcDS, nDstPixels, nDstLines,
                adfDstGeoTransform, psWO);

            GDALDestroyWarpOptions(psWO);

            if (pszDstWKT != NULL)
                GDALSetProjection(hDstDS, pszDstWKT);
            else if (pszSrcWKT != NULL)
                GDALSetProjection(hDstDS, pszDstWKT);
            else if (GDALGetGCPCount(hSrcDS) > 0)
                GDALSetProjection(hDstDS, GDALGetGCPProjection(hSrcDS));
            else
                GDALSetProjection(hDstDS, GDALGetProjectionRef(hSrcDS));

            return hDstDS;
        }

        /**
         * Gets the GeoExtent of the given filename.
         */
        GeoExtent getGeoExtent(std::string& filename)
        {
            GDALDataset* ds = (GDALDataset*)GDALOpen(filename.c_str(), GA_ReadOnly);
            if (!ds)
            {
                return GeoExtent::INVALID;
            }

            // Get the geotransforms
            double geotransform[6];
            ds->GetGeoTransform(geotransform);

            double minX, minY, maxX, maxY;

            GDALApplyGeoTransform(geotransform, 0.0, ds->GetRasterYSize(), &minX, &minY);
            GDALApplyGeoTransform(geotransform, ds->GetRasterXSize(), 0.0, &maxX, &maxY);

            std::string srsString = ds->GetProjectionRef();
            SRS srs(srsString);

            GDALClose(ds);

            GeoExtent ext(srs, minX, minY, maxX, maxY);
            return ext;
        }
        /**
        * Finds a raster band based on color interpretation
        */
        GDALRasterBand* findBandByColorInterp(GDALDataset *ds, GDALColorInterp colorInterp)
        {
            for (int i = 1; i <= ds->GetRasterCount(); ++i)
            {
                if (ds->GetRasterBand(i)->GetColorInterpretation() == colorInterp) return ds->GetRasterBand(i);
            }
            return 0;
        }

        GDALRasterBand* findBandByDataType(GDALDataset *ds, GDALDataType dataType)
        {
            for (int i = 1; i <= ds->GetRasterCount(); ++i)
            {
                if (ds->GetRasterBand(i)->GetRasterDataType() == dataType) return ds->GetRasterBand(i);
            }
            return 0;
        }

        bool getPalleteIndexColor(GDALRasterBand* band, int index, glm::u8vec4& color)
        {
            const GDALColorEntry *colorEntry = band->GetColorTable()->GetColorEntry(index);
            GDALPaletteInterp interp = band->GetColorTable()->GetPaletteInterpretation();
            if (!colorEntry)
            {
                //FIXME: What to do here?
                color.r = 255;
                color.g = 0;
                color.b = 0;
                color.a = 1;
                return false;
            }
            else
            {
                if (interp == GPI_RGB)
                {
                    color.r = colorEntry->c1;
                    color.g = colorEntry->c2;
                    color.b = colorEntry->c3;
                    color.a = colorEntry->c4;
                }
                else if (interp == GPI_CMYK)
                {
                    // from wikipedia.org
                    short C = colorEntry->c1;
                    short M = colorEntry->c2;
                    short Y = colorEntry->c3;
                    short K = colorEntry->c4;
                    color.r = 255 - C * (255 - K) - K;
                    color.g = 255 - M * (255 - K) - K;
                    color.b = 255 - Y * (255 - K) - K;
                    color.a = 255;
                }
                else if (interp == GPI_HLS)
                {
                    // from easyrgb.com
                    float H = colorEntry->c1;
                    float S = colorEntry->c3;
                    float L = colorEntry->c2;
                    float R, G, B;
                    if (S == 0)                       //HSL values = 0 - 1
                    {
                        R = L;                      //RGB results = 0 - 1
                        G = L;
                        B = L;
                    }
                    else
                    {
                        float var_2, var_1;
                        if (L < 0.5)
                            var_2 = L * (1 + S);
                        else
                            var_2 = (L + S) - (S * L);

                        var_1 = 2 * L - var_2;

                        R = Hue_2_RGB(var_1, var_2, H + (1.0f / 3.0f));
                        G = Hue_2_RGB(var_1, var_2, H);
                        B = Hue_2_RGB(var_1, var_2, H - (1.0f / 3.0f));
                    }
                    color.r = static_cast<unsigned char>(R*255.0f);
                    color.g = static_cast<unsigned char>(G*255.0f);
                    color.b = static_cast<unsigned char>(B*255.0f);
                    color.a = static_cast<unsigned char>(255.0f);
                }
                else if (interp == GPI_Gray)
                {
                    color.r = static_cast<unsigned char>(colorEntry->c1*255.0f);
                    color.g = static_cast<unsigned char>(colorEntry->c1*255.0f);
                    color.b = static_cast<unsigned char>(colorEntry->c1*255.0f);
                    color.a = static_cast<unsigned char>(255.0f);
                }
                else
                {
                    return false;
                }
                return true;
            }
        }
        
        template<typename T>
        void applyScaleAndOffset(void* data, int count, double scale, double offset)
        {
            T* f = (T*)data;
            for (int i = 0; i < count; ++i)
            {
                double value = static_cast<double>(*f) * scale + offset;
                *f++ = static_cast<T>(value);
            }
        }

        // GDALRasterBand::RasterIO helper method
        bool rasterIO(
            GDALRasterBand *band,
            GDALRWFlag eRWFlag,
            double nXOff,
            double nYOff,
            double nXSize,
            double nYSize,
            void *pData,
            int nBufXSize,
            int nBufYSize,
            GDALDataType eBufType,
            GSpacing nPixelSpace,
            GSpacing nLineSpace,
            Image::Interpolation interpolation = Image::NEAREST
        )
        {
            GDALRasterIOExtraArg psExtraArg;

            // defaults to GRIORA_NearestNeighbour
            INIT_RASTERIO_EXTRA_ARG(psExtraArg);

            switch (interpolation)
            {
            case Image::AVERAGE:
                //psExtraArg.eResampleAlg = GRIORA_Average;
                // for some reason gdal's average resampling produces artifacts occasionally for imagery at higher levels.
                // for now we'll just use bilinear interpolation under the hood until we can understand what is going on.
                psExtraArg.eResampleAlg = GRIORA_Bilinear;
                break;
            case Image::BILINEAR:
                psExtraArg.eResampleAlg = GRIORA_Bilinear;
                break;
            case Image::CUBIC:
                psExtraArg.eResampleAlg = GRIORA_Cubic;
                break;
            case Image::CUBICSPLINE:
                psExtraArg.eResampleAlg = GRIORA_CubicSpline;
                break;
            }

            psExtraArg.bFloatingPointWindowValidity = TRUE;
            psExtraArg.dfXOff = nXOff;
            psExtraArg.dfYOff = nYOff;
            psExtraArg.dfXSize = nXSize;
            psExtraArg.dfYSize = nYSize;

            CPLErr err = band->RasterIO(eRWFlag, floor(nXOff), floor(nYOff), ceil(nXSize), ceil(nYSize), pData, nBufXSize, nBufYSize, eBufType, nPixelSpace, nLineSpace, &psExtraArg);

            if (err != CE_None)
            {
                //ROCKY_WARN << LC << "RasterIO failed.\n";
            }
            else
            {
                double scale = band->GetScale();
                double offset = band->GetOffset();

                if (scale != 1.0 || offset != 0.0)
                {
                    int count = nBufXSize * nBufYSize;

                    if (eBufType == GDT_Float32)
                        applyScaleAndOffset<float>(pData, count, scale, offset);
                    else if (eBufType == GDT_Float64)
                        applyScaleAndOffset<double>(pData, count, scale, offset);
                    else if (eBufType == GDT_Int16 || eBufType == GDT_UInt16)
                        applyScaleAndOffset<short>(pData, count, scale, offset);
                    else if (eBufType == GDT_Int32 || eBufType == GDT_UInt32)
                        applyScaleAndOffset<int>(pData, count, scale, offset);
                    else if (eBufType == GDT_Byte)
                        applyScaleAndOffset<char>(pData, count, scale, offset);
                }
            }

            return (err == CE_None);
        }

        Result<shared_ptr<Image>> readImage(
            unsigned char* data,
            unsigned length,
            const std::string& name)
        {
            shared_ptr<Image> result;

            // generate a unique name for our temporary vsimem file:
            static std::atomic_int rgen(0);
            std::string filename = "/vsimem/temp" + std::to_string(rgen++);

            // populate the "file" from our raw data
            auto memfile = VSIFileFromMemBuffer(filename.c_str(), (GByte*)data, (vsi_l_offset)length, true);
            if (memfile)
            {
                const char* const drivers[] = { name.c_str(), nullptr };

                GDALDataset* ds = (GDALDataset*)GDALOpenEx(
                    filename.c_str(),
                    GA_ReadOnly,
                    drivers,
                    nullptr,
                    nullptr);

                if (ds)
                {
                    int width = ds->GetRasterXSize();
                    int height = ds->GetRasterYSize();

                    GDALRasterBand* R = findBandByColorInterp(ds, GCI_RedBand);
                    GDALRasterBand* G = findBandByColorInterp(ds, GCI_GreenBand);
                    GDALRasterBand* B = findBandByColorInterp(ds, GCI_BlueBand);
                    GDALRasterBand* A = findBandByColorInterp(ds, GCI_AlphaBand);
                    GDALRasterBand* M = findBandByColorInterp(ds, GCI_GrayIndex);

                    Image::PixelFormat format = Image::UNDEFINED;
                    if (M)
                        format = Image::R32_SFLOAT;
                    else if (R && !G && !B && !A)
                        format = Image::R8_UNORM;
                    else if (R && G && !B && !A)
                        format = Image::R8G8B8_UNORM;
                    else if (R && G && B && !A)
                        format = Image::R8G8B8_UNORM;
                    else if (R && G && B && A)
                        format = Image::R8G8B8A8_UNORM;

                    if (format != Image::UNDEFINED)
                    {
                        result = Image::create(format, width, height);
                        auto data = result->data<unsigned char>();
                        GSpacing spacing = result->numComponents();
                        
                        int offset = 0;

                        if (M)
                        {
                            auto value_scale = M->GetScale();
                            auto value_offset = M->GetOffset();

                            M->RasterIO(GF_Read, 0, 0, width, height, result->data<unsigned char>() + (offset++), width, height, GDT_Float32, 0, 0, nullptr);

                            auto ptr = result->data<float>();
                            for (int i = 0; i < width * height; ++i, ptr++)
                                *ptr = *ptr * value_scale + value_offset;
                        }
                        else
                        {
                            if (R)
                                R->RasterIO(GF_Read, 0, 0, width, height, result->data<unsigned char>() + (offset++), width, height, GDT_Byte, spacing, 0, nullptr);
                            if (G)
                                G->RasterIO(GF_Read, 0, 0, width, height, result->data<unsigned char>() + (offset++), width, height, GDT_Byte, spacing, 0, nullptr);
                            if (B)
                                B->RasterIO(GF_Read, 0, 0, width, height, result->data<unsigned char>() + (offset++), width, height, GDT_Byte, spacing, 0, nullptr);
                            if (A)
                                A->RasterIO(GF_Read, 0, 0, width, height, result->data<unsigned char>() + (offset++), width, height, GDT_Byte, spacing, 0, nullptr);
                        }
                    }

                    GDALClose(ds);
                }
                VSIUnlink(filename.c_str());
                VSIFree(memfile);
            }

            return result;
        }

    }
} // namespace ROCKY_NAMESPACE::GDAL


//...................................................................

GDAL::Driver::Driver() :
    _srcDS(NULL),
    _warpedDS(NULL),
    _maxDataLevel(30),
    _linearUnits(1.0)
{
    _threadId = std::this_thread::get_id();
}

GDAL::Driver::~Driver()
{
    if (_warpedDS)
        GDALClose(_warpedDS);
    else if (_srcDS)
        GDALClose(_srcDS);
}

// Open the data source and prepare it for reading
Status
GDAL::Driver::open(
    const std::string& name,
    const LayerBase* layer,
    unsigned tileSize,
    DataExtentList* layerDataExtents,
    const IOOptions& io)
{
    bool info = (layerDataExtents != NULL);

    _name = name;
    _layer = layer;

    // Is a valid external GDAL dataset specified ?
    bool useExternalDataset = false;
    if (_external && _external->dataset != NULL)
    {
        useExternalDataset = true;
    }

    if (useExternalDataset == false &&        
        (!layer->uri().has_value() || layer->uri()->empty()) &&
        (!layer->connection().has_value() || layer->connection()->empty()))
    {
        return Status(Status::ConfigurationError, "No URL, directory, or connection string specified");
    }

    // source connection:
    std::string source;
    bool isFile = true;

    if (layer->uri().has_value())
    {
        // Use the base instead of the full if this is a gdal virtual file system
        if (util::startsWith(layer->uri()->base(), "/vsi"))
        {
            source = layer->uri()->base();
        }
        else
        {
            source = layer->uri()->full();
        }
    }
    else if (layer->connection().has_value())
    {
        source = layer->connection();
        isFile = false;
    }

    if (useExternalDataset == false)
    {
        std::string input;

        if (layer->uri().has_value())
            input = layer->uri()->full();
        else
            input = source;

        if (input.empty())
        {
            return Status(Status::ResourceUnavailable, "Could not find any valid input.");
        }

        // Resolve the pathname...
        if (isFile && !std::filesystem::exists(input))
        {
            // TODO?
            //std::string found = osgDB::findDataFile(input);
            //if (!found.empty())
            //    input = found;
        }

        // Create the source dataset:
        _srcDS = (GDALDataset*)GDALOpen(input.c_str(), GA_ReadOnly);
        if (_srcDS)
        {
            char **subDatasets = _srcDS->GetMetadata("SUBDATASETS");
            int numSubDatasets = CSLCount(subDatasets);

            if (numSubDatasets > 0)
            {
                int subDataset = layer->subDataSet().has_value() ? static_cast<int>(layer->subDataSet()) : 1;
                if (subDataset < 1 || subDataset > numSubDatasets) subDataset = 1;
                std::stringstream buf;
                buf << "SUBDATASET_" << subDataset << "_NAME";
                char *pszSubdatasetName = CPLStrdup(CSLFetchNameValue(subDatasets, buf.str().c_str()));
                GDALClose(_srcDS);
                _srcDS = (GDALDataset*)GDALOpen(pszSubdatasetName, GA_ReadOnly);
                CPLFree(pszSubdatasetName);
            }
        }

        if (!_srcDS)
        {
            return Status(Status::ResourceUnavailable, "Failed to open " + input);
        }
    }
    else
    {
        _srcDS = _external->dataset;
    }

    // Establish the source spatial reference:
    SRS src_srs;

    std::string srcProj = _srcDS->GetProjectionRef();

    // If the projection is empty and we have GCP's then use the GCP projection.
    if (srcProj.empty() && _srcDS->GetGCPCount() > 0)
    {
        srcProj = _srcDS->GetGCPProjection();
    }

    if (!srcProj.empty())
    {
        src_srs = SRS(srcProj);
    }

    // still no luck? (for example, an ungeoreferenced file like a jpeg?)
    // try to read a .prj file:
    if (!src_srs.valid())
    {
        // not found in the dataset; try loading a .prj file
        auto prjLocation =
            std::filesystem::path(source)
            .replace_extension("prj")
            .lexically_normal()
            .generic_string();

        auto rr = URI(prjLocation).read(io); // TODO io
        if (rr.status.ok() && !rr.value.data.empty())
        {
            src_srs = SRS(util::trim(rr.value.data));
        }
    }

    if (!src_srs.valid())
    {
        return Status(Status::ResourceUnavailable,
            "Dataset has no spatial reference information (" + source + ")");
    }

    // These are the actual extents of the data:
    bool hasGCP = false;
    bool isRotated = false;
    bool requiresReprojection = false;
    
    bool hasGeoTransform = (_srcDS->GetGeoTransform(_geotransform) == CE_None);

    hasGCP = _srcDS->GetGCPCount() > 0 && _srcDS->GetGCPProjection();
    isRotated = hasGeoTransform && (_geotransform[2] != 0.0 || _geotransform[4] != 0.0);
    requiresReprojection = hasGCP || isRotated;

    // For a geographic SRS, use the whole-globe profile for performance.
    // Otherwise, collect information and make the profile later.
    if (src_srs.isGeodetic())
    {
        _profile = Profile(src_srs);
        if (!_profile.valid())
        {
            return Status(Status::ResourceUnavailable,
                "Cannot create geographic Profile from dataset's spatial reference information: " +
                std::string(src_srs.name()));
        }

        // no xform an geographic? Match the profile.
        if (!hasGeoTransform)
        {
            _geotransform[0] = _profile.extent().xMin();
            _geotransform[1] = _profile.extent().width() / (double)_srcDS->GetRasterXSize();
            _geotransform[2] = 0;
            _geotransform[3] = _profile.extent().yMax();
            _geotransform[4] = 0;
            _geotransform[5] = -_profile.extent().height() / (double)_srcDS->GetRasterYSize();
            hasGeoTransform = true;
        }
    }

    // Handle some special cases.
    std::string warpedSRSWKT;

    if (requiresReprojection || (_profile.valid() && !_profile.srs().isEquivalentTo(src_srs)))
    {
        std::string destWKT = _profile.valid() ? _profile.srs().wkt() : src_srs.wkt();
        _warpedDS = (GDALDataset*)GDALAutoCreateWarpedVRT(
            _srcDS,
            src_srs.wkt().c_str(),
            destWKT.c_str(),
            GRA_NearestNeighbour, // resample algorithm
            5.0, // max error
            nullptr); // options

        if (_warpedDS)
        {
            warpedSRSWKT = _warpedDS->GetProjectionRef();
            _warpedDS->GetGeoTransform(_geotransform);
        }
    }
    else
    {
        _warpedDS = _srcDS;
        warpedSRSWKT = src_srs.wkt();

        // re-read the extents from the new DS:
        _warpedDS->GetGeoTransform(_geotransform);
    }

    if (!_warpedDS)
    {
        return Status("Failed to create a final sampling dataset");
    }

    // calcluate the inverse of the geotransform:
    GDALInvGeoTransform(_geotransform, _invtransform);

    double minX, minY, maxX, maxY;
    pixelToGeo(0.0, _warpedDS->GetRasterYSize(), minX, minY);
    pixelToGeo(_warpedDS->GetRasterXSize(), 0.0, maxX, maxY);

    // If we don't have a profile yet, that means this is a projected dataset
    // so we will create the profile from the actual data extents.
    if (!_profile.valid())
    {
        SRS srs(warpedSRSWKT);
        if (srs.valid())
        {
            _profile = Profile(
                srs,
                Box(minX, minY, maxX, maxY));
        }

        if (!_profile.valid())
        {
            return Status::Error(
                "Cannot create projected Profile from dataset's warped spatial reference WKT: " + warpedSRSWKT);
        }
    }

    ROCKY_HARD_ASSERT(_profile.valid());

    //Compute the min and max data levels
    double resolutionX = (maxX - minX) / (double)_warpedDS->GetRasterXSize();
    double resolutionY = (maxY - minY) / (double)_warpedDS->GetRasterYSize();
    double maxResolution = std::min(resolutionX, resolutionY);

    if (_maxDataLevel.has_value())
    {
        //nop
    }
    else
    {
        unsigned int max_level = 30;
        for (unsigned int i = 0; i < max_level; ++i)
        {
            _maxDataLevel = i;
            auto[w, h] = _profile.tileDimensions(i);
            double resX = w / (double)tileSize;
            double resY = h / (double)tileSize;

            if (resX < maxResolution || resY < maxResolution)
            {
                break;
            }
        }
    }

    // If the input dataset is a VRT, then get the individual files in the dataset and use THEM for the DataExtents.
    // A VRT will create a potentially very large virtual dataset from sparse datasets, so using the extents from the underlying files
    // will allow rocky to only create tiles where there is actually data.
    DataExtentList dataExtents;

    auto srs = SRS(warpedSRSWKT);

    // record the data extent in profile space:
    _bounds = Box(minX, minY, maxX, maxY);

    const char* pora = _srcDS->GetMetadataItem("AREA_OR_POINT");
    bool is_area = pora != nullptr && util::toLower(std::string(pora)) == "area";

    bool clamped = false;
    if (srs.isGeodetic())
    {
        if (is_area && (_bounds.xmin < -180.0 || _bounds.xmax > 180.0))
        {
            _bounds.xmin += resolutionX * 0.5;
            _bounds.xmax -= resolutionX * 0.5;
        }

        if ((_bounds.xmax - _bounds.xmin) > 360.0)
        {
            _bounds.xmin = -180;
            _bounds.xmax = 180;
            clamped = true;
        }

        if (is_area && (_bounds.ymin < -90.0 || _bounds.ymax > 90.0))
        {
            _bounds.ymin += resolutionY * 0.5;
            _bounds.ymax -= resolutionY * 0.5;
        }

        if ((_bounds.ymax - _bounds.ymin) > 180)
        {
            _bounds.ymin = -90;
            _bounds.ymax = 90;
            clamped = true;
        }
    }
    _extents = GeoExtent(srs, _bounds);

    if (layerDataExtents)
    {
        GeoExtent profile_extent = _extents.transform(_profile.srs());
        if (dataExtents.empty())
        {
            // Use the extents of the whole file.
            if (_maxDataLevel.has_value())
                layerDataExtents->push_back(DataExtent(profile_extent, 0, _maxDataLevel));
            else
                layerDataExtents->push_back(DataExtent(profile_extent));
        }
        else
        {
            // Use the DataExtents from the subfiles of the VRT.
            layerDataExtents->insert(layerDataExtents->end(), dataExtents.begin(), dataExtents.end());
        }
    }

    // Get the linear units of the SRS for scaling elevation values
    _linearUnits = 1.0; // srs.getReportedLinearUnits();

    return StatusOK;
}

void
GDAL::Driver::pixelToGeo(double x, double y, double &geoX, double &geoY)
{
    geoX = _geotransform[0] + _geotransform[1] * x + _geotransform[2] * y;
    geoY = _geotransform[3] + _geotransform[4] * x + _geotransform[5] * y;
}

void
GDAL::Driver::geoToPixel(double geoX, double geoY, double &x, double &y)
{
    x = _invtransform[0] + _invtransform[1] * geoX + _invtransform[2] * geoY;
    y = _invtransform[3] + _invtransform[4] * geoX + _invtransform[5] * geoY;

    //Account for slight rounding errors.  If we are right on the edge of the dataset, clamp to the edge
    double eps = 0.0001;
    if (equiv(x, 0.0, eps)) x = 0;
    if (equiv(y, 0.0, eps)) y = 0;
    if (equiv(x, (double)_warpedDS->GetRasterXSize(), eps)) x = _warpedDS->GetRasterXSize();
    if (equiv(y, (double)_warpedDS->GetRasterYSize(), eps)) y = _warpedDS->GetRasterYSize();
}

bool
GDAL::Driver::isValidValue(float v, GDALRasterBand* band)
{
    float bandNoData = -32767.0f;
    int success;
    float value = band->GetNoDataValue(&success);
    if (success)
    {
        bandNoData = value;
    }

    //Check to see if the value is equal to the bands specified no data
    if (bandNoData == v)
        return false;

    //Check to see if the value is equal to the user specified nodata value
    if (_noDataValue.has_value(v))
        return false;

    //Check to see if the user specified a custom min/max
    if (_minValidValue.has_value() && v < _minValidValue)
        return false;

    if (_maxValidValue.has_value() && v > _maxValidValue)
        return false;

    return true;
}

float
GDAL::Driver::getValidElevationValue(float v, float noDataValueFromBand, float replacement)
{
    if (_noDataValue.has_value(v) || noDataValueFromBand == v)
        return replacement;

    //Check to see if the user specified a custom min/max
    if (_minValidValue.has_value() && v < _minValidValue)
        return replacement;

    if (_maxValidValue.has_value() && v > _maxValidValue)
        return replacement;

    return v;
}

float
GDAL::Driver::getInterpolatedValue(GDALRasterBand* band, double x, double y, bool applyOffset)
{
    double r, c;
    geoToPixel(x, y, c, r);

    if (applyOffset)
    {
        //Apply half pixel offset
        r -= 0.5;
        c -= 0.5;

        //Account for the half pixel offset in the geotransform.  If the pixel value is -0.5 we are still technically in the dataset
        //since 0,0 is now the center of the pixel.  So, if are within a half pixel above or a half pixel below the dataset just use
        //the edge values
        if (c < 0 && c >= -0.5)
        {
            c = 0;
        }
        else if (c > _warpedDS->GetRasterXSize() - 1 && c <= _warpedDS->GetRasterXSize() - 0.5)
        {
            c = _warpedDS->GetRasterXSize() - 1;
        }

        if (r < 0 && r >= -0.5)
        {
            r = 0;
        }
        else if (r > _warpedDS->GetRasterYSize() - 1 && r <= _warpedDS->GetRasterYSize() - 0.5)
        {
            r = _warpedDS->GetRasterYSize() - 1;
        }
    }

    float result = 0.0f;

    //If the location is outside of the pixel values of the dataset, just return 0
    if (c < 0 || r < 0 || c > _warpedDS->GetRasterXSize() - 1 || r > _warpedDS->GetRasterYSize() - 1)
        return NO_DATA_VALUE;

    if (_layer->interpolation() == Image::NEAREST)
    {
        rasterIO(band, GF_Read, (int)round(c), (int)round(r), 1, 1, &result, 1, 1, GDT_Float32, 0, 0);
        if (!isValidValue(result, band))
        {
            return NO_DATA_VALUE;
        }
    }
    else
    {
        int rowMin = std::max((int)floor(r), 0);
        int rowMax = std::max(std::min((int)ceil(r), (int)(_warpedDS->GetRasterYSize() - 1)), 0);
        int colMin = std::max((int)floor(c), 0);
        int colMax = std::max(std::min((int)ceil(c), (int)(_warpedDS->GetRasterXSize() - 1)), 0);

        if (rowMin > rowMax) rowMin = rowMax;
        if (colMin > colMax) colMin = colMax;

        float urHeight, llHeight, ulHeight, lrHeight;

        rasterIO(band, GF_Read, colMin, rowMin, 1, 1, &llHeight, 1, 1, GDT_Float32, 0, 0);
        rasterIO(band, GF_Read, colMin, rowMax, 1, 1, &ulHeight, 1, 1, GDT_Float32, 0, 0);
        rasterIO(band, GF_Read, colMax, rowMin, 1, 1, &lrHeight, 1, 1, GDT_Float32, 0, 0);
        rasterIO(band, GF_Read, colMax, rowMax, 1, 1, &urHeight, 1, 1, GDT_Float32, 0, 0);

        if ((!isValidValue(urHeight, band)) || (!isValidValue(llHeight, band)) || (!isValidValue(ulHeight, band)) || (!isValidValue(lrHeight, band)))
        {
            return NO_DATA_VALUE;
        }

        if (_layer->interpolation() == Image::AVERAGE)
        {
            double x_rem = c - (int)c;
            double y_rem = r - (int)r;

            double w00 = (1.0 - y_rem) * (1.0 - x_rem) * (double)llHeight;
            double w01 = (1.0 - y_rem) * x_rem * (double)lrHeight;
            double w10 = y_rem * (1.0 - x_rem) * (double)ulHeight;
            double w11 = y_rem * x_rem * (double)urHeight;

            result = (float)(w00 + w01 + w10 + w11);
        }
        else if (_layer->interpolation() == Image::BILINEAR)
        {
            //Check for exact value
            if ((colMax == colMin) && (rowMax == rowMin))
            {
                result = llHeight;
            }
            else if (colMax == colMin)
            {
                //Linear interpolate vertically
                result = ((float)rowMax - r) * llHeight + (r - (float)rowMin) * ulHeight;
            }
            else if (rowMax == rowMin)
            {
                //Linear interpolate horizontally
                result = ((float)colMax - c) * llHeight + (c - (float)colMin) * lrHeight;
            }
            else
            {
                //Bilinear interpolate
                float r1 = ((float)colMax - c) * llHeight + (c - (float)colMin) * lrHeight;
                float r2 = ((float)colMax - c) * ulHeight + (c - (float)colMin) * urHeight;
                result = ((float)rowMax - r) * r1 + (r - (float)rowMin) * r2;
            }
        }
    }

    return result;
}

bool
GDAL::Driver::intersects(const TileKey& key)
{
    return key.extent().intersects(_extents);
}

Result<shared_ptr<Image>>
GDAL::Driver::createImage(
    const TileKey& key,
    unsigned tileSize,
    bool isCoverage,
    const IOOptions& io)
{
    if (_maxDataLevel.has_value() && key.levelOfDetail() > _maxDataLevel)
    {
        return Status(Status::ResourceUnavailable);
    }

    if (io.canceled())
    {
        return Status(Status::ResourceUnavailable);
    }

    shared_ptr<Image> image;

    const bool invert = true;

    //Get the extents of the tile
    double xmin, ymin, xmax, ymax;
    key.extent().getBounds(xmin, ymin, xmax, ymax);

    // Compute the intersection of the incoming key with the data extents of the dataset
    rocky::GeoExtent intersection = key.extent().intersectionSameSRS(_extents);
    if (!intersection.valid())
    {
        return Status(Status::ResourceUnavailable);
    }

    double west = intersection.xMin();
    double east = intersection.xMax();
    double north = intersection.yMax();
    double south = intersection.yMin();

    // The extents and the intersection will be normalized between -180 and 180 longitude if they are geographic.
    // However, the georeferencing will expect the coordinates to be in the same longitude frame as the original dataset,
    // so the intersection bounds are adjusted here if necessary so that the values line up with the georeferencing.
    if (_extents.srs().isGeodetic())
    {
        while (west < _bounds.xmin)
        {
            west += 360.0;
            east = west + intersection.width();
        }
        while (west > _bounds.xmax)
        {
            west -= 360.0;
            east = west + intersection.width();
        }
    }

    // Determine the read window
    double src_min_x, src_min_y, src_max_x, src_max_y;
    // Get the pixel coordiantes of the intersection
    geoToPixel(west, intersection.yMax(), src_min_x, src_min_y);
    geoToPixel(east, intersection.yMin(), src_max_x, src_max_y);

    double src_width = src_max_x - src_min_x;
    double src_height = src_max_y - src_min_y;

    int rasterWidth = _warpedDS->GetRasterXSize();
    int rasterHeight = _warpedDS->GetRasterYSize();

    if (src_min_x + src_width > (double)rasterWidth)
    {
        src_width = (double)rasterWidth - src_min_x;
    }

    if (src_min_y + src_height > (double)rasterHeight)
    {
        src_height = (double)rasterHeight - src_min_y;
    }

    // Determine the destination window

    // Compute the offsets in geo coordinates of the intersection from the TileKey
    double offset_left = intersection.xMin() - xmin;
    double offset_top = ymax - intersection.yMax();


    int target_width = (int)ceil((intersection.width() / key.extent().width())*(double)tileSize);
    int target_height = (int)ceil((intersection.height() / key.extent().height())*(double)tileSize);
    int tile_offset_left = (int)floor((offset_left / key.extent().width()) * (double)tileSize);
    int tile_offset_top = (int)floor((offset_top / key.extent().height()) * (double)tileSize);

    // Compute spacing
    double dx = (xmax - xmin) / (double)(tileSize - 1);
    double dy = (ymax - ymin) / (double)(tileSize - 1);

    // Return if parameters are out of range.
    if (src_width <= 0 || src_height <= 0 || target_width <= 0 || target_height <= 0)
    {
        return Status(Status::ResourceUnavailable);
    }

    GDALRasterBand* bandRed = findBandByColorInterp(_warpedDS, GCI_RedBand);
    GDALRasterBand* bandGreen = findBandByColorInterp(_warpedDS, GCI_GreenBand);
    GDALRasterBand* bandBlue = findBandByColorInterp(_warpedDS, GCI_BlueBand);
    GDALRasterBand* bandAlpha = findBandByColorInterp(_warpedDS, GCI_AlphaBand);

    GDALRasterBand* bandGray = findBandByColorInterp(_warpedDS, GCI_GrayIndex);

    GDALRasterBand* bandPalette = findBandByColorInterp(_warpedDS, GCI_PaletteIndex);

    if (!bandRed && !bandGreen && !bandBlue && !bandAlpha && !bandGray && !bandPalette)
    {
        //We couldn't find any valid bands based on the color interp, so just make an educated guess based on the number of bands in the file
        //RGB = 3 bands
        if (_warpedDS->GetRasterCount() == 3)
        {
            bandRed = _warpedDS->GetRasterBand(1);
            bandGreen = _warpedDS->GetRasterBand(2);
            bandBlue = _warpedDS->GetRasterBand(3);
        }
        //RGBA = 4 bands
        else if (_warpedDS->GetRasterCount() == 4)
        {
            bandRed = _warpedDS->GetRasterBand(1);
            bandGreen = _warpedDS->GetRasterBand(2);
            bandBlue = _warpedDS->GetRasterBand(3);
            bandAlpha = _warpedDS->GetRasterBand(4);
        }
        //Gray = 1 band
        else if (_warpedDS->GetRasterCount() == 1)
        {
            bandGray = _warpedDS->GetRasterBand(1);
        }
        //Gray + alpha = 2 bands
        else if (_warpedDS->GetRasterCount() == 2)
        {
            bandGray = _warpedDS->GetRasterBand(1);
            bandAlpha = _warpedDS->GetRasterBand(2);
        }
    }

    // For images, the pixel format is always RGBA to support transparency
    Image::PixelFormat pixelFormat = Image::R8G8B8A8_UNORM;


    if (bandRed && bandGreen && bandBlue)
    {
        unsigned char *red = new unsigned char[target_width * target_height];
        unsigned char *green = new unsigned char[target_width * target_height];
        unsigned char *blue = new unsigned char[target_width * target_height];
        unsigned char *alpha = new unsigned char[target_width * target_height];

        //Initialize the alpha values to 255.
        memset(alpha, 255, target_width * target_height);

        image = Image::create(pixelFormat, tileSize, tileSize);

        memset(image->data<char>(), 0, image->sizeInBytes());

        rasterIO(bandRed, GF_Read, src_min_x, src_min_y, src_width, src_height, red, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());
        rasterIO(bandGreen, GF_Read, src_min_x, src_min_y, src_width, src_height, green, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());
        rasterIO(bandBlue, GF_Read, src_min_x, src_min_y, src_width, src_height, blue, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());

        if (bandAlpha)
        {
            rasterIO(bandAlpha, GF_Read, src_min_x, src_min_y, src_width, src_height, alpha, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());
        }

        for (int src_row = 0, dst_row = tile_offset_top;
            src_row < target_height;
            src_row++, dst_row++)
        {
            unsigned int flippedRow = tileSize - dst_row - 1;
            for (int src_col = 0, dst_col = tile_offset_left;
                src_col < target_width;
                ++src_col, ++dst_col)
            {
                int i = src_col + src_row * target_width;
                glm::fvec4 c = glm::fvec4(red[i], green[i], blue[i], alpha[i]) / 255.0f;

                if (!isValidValue(c.r, bandRed) ||
                    !isValidValue(c.g, bandGreen) ||
                    !isValidValue(c.b, bandBlue) ||
                    (bandAlpha && !isValidValue(c.a, bandAlpha)))
                {
                    c.a = 0.0f;
                }

                image->write(c, dst_col, flippedRow);
            }
        }

        delete[] red;
        delete[] green;
        delete[] blue;
        delete[] alpha;
    }

    else if (bandGray)
    {
        // This might be single-channel elevation data ... try to detect that
        // by assuming 16- or 32-bit data is elevation.
        bool isElevation = false;

        GDALDataType gdalDataType = bandGray->GetRasterDataType();

        int gdalSampleSize =
            (gdalDataType == GDT_Byte) ? 1 :
            (gdalDataType == GDT_UInt16 || gdalDataType == GDT_Int16) ? 2 :
            4;

        if (gdalDataType == GDT_Int16 || gdalDataType == GDT_UInt16)
        {
            isElevation = true;
        }
        else if (gdalDataType == GDT_Float32)
        {
            isElevation = true;
        }

        if (isElevation)
        {
            image = Image::create(Image::R32_SFLOAT, tileSize, tileSize);
            image->fill(glm::fvec4(NO_DATA_VALUE));
            
            if (gdalDataType == GDT_Int16)
            {
                short* temp = new short[target_width * target_height];

                rasterIO(bandGray, GF_Read, src_min_x, src_min_y, src_width, src_height, temp, target_width, target_height, gdalDataType, 0, 0, _layer->interpolation());

                int success = 0;
                short noDataValueFromBand = bandGray->GetNoDataValue(&success);
                if (!success) noDataValueFromBand = (short)-32767;

                for (int src_row = 0, dst_row = tile_offset_top; src_row < target_height; src_row++, dst_row++)
                {
                    unsigned int flippedRow = tileSize - dst_row - 1;
                    for (int src_col = 0, dst_col = tile_offset_left; src_col < target_width; ++src_col, ++dst_col)
                    {
                        glm::fvec4 c;
                        c.r = temp[src_col + src_row * target_width];
                        c.r = getValidElevationValue(c.r, noDataValueFromBand, NO_DATA_VALUE);
                        image->write(c, dst_col, flippedRow);
                    }
                }

                delete[] temp;
            }
            else // if (gdalDataType == GDT_Float32)
            {
                float* temp = new float[target_width * target_height];

                rasterIO(bandGray, GF_Read, src_min_x, src_min_y, src_width, src_height, temp, target_width, target_height, gdalDataType, 0, 0, _layer->interpolation());

                int success = 0;
                float noDataValueFromBand = bandGray->GetNoDataValue(&success);
                if (!success) noDataValueFromBand = NO_DATA_VALUE;

                for (int src_row = 0, dst_row = tile_offset_top; src_row < target_height; src_row++, dst_row++)
                {
                    unsigned int flippedRow = tileSize - dst_row - 1;
                    for (int src_col = 0, dst_col = tile_offset_left; src_col < target_width; ++src_col, ++dst_col)
                    {
                        glm::fvec4 c;
                        c.r = temp[src_col + src_row * target_width];
                        c.r = getValidElevationValue(c.r, noDataValueFromBand, NO_DATA_VALUE);
                        image->write(c, dst_col, flippedRow);
                    }
                }

                delete[] temp;
            }
        }
        
        else // grey + alpha color
        {
            image = Image::create(Image::R8G8B8A8_UNORM, tileSize, tileSize);
            image->fill(glm::fvec4(0));

            unsigned char* gray = new unsigned char[target_width * target_height];

            // color only:
            unsigned char* alpha = nullptr;
            if (bandAlpha)
            {
                alpha = new unsigned char[target_width * target_height];
                memset(alpha, 255, target_width * target_height);
            }

            rasterIO(bandGray, GF_Read, src_min_x, src_min_y, src_width, src_height, gray, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());

            // color only:
            if (bandAlpha)
            {
                rasterIO(bandAlpha, GF_Read, src_min_x, src_min_y, src_width, src_height, alpha, target_width, target_height, GDT_Byte, 0, 0, _layer->interpolation());
            }

            for (int src_row = 0, dst_row = tile_offset_top;
                src_row < target_height;
                src_row++, dst_row++)
            {
                unsigned int flippedRow = tileSize - dst_row - 1;
                for (int src_col = 0, dst_col = tile_offset_left; src_col < target_width; ++src_col, ++dst_col)
                {
                    glm::fvec4 c;
                    c.r = c.g = c.b = gray[src_col + src_row * target_width];

                    if (alpha)
                        c.a = alpha[src_col + src_row * target_width];

                    if (!isValidValue(c.r, bandGray) || (bandAlpha && !isValidValue(c.a, bandAlpha)))
                    {
                        c.a = 0.0f;
                    }

                    c /= 255.0f;

                    image->write(c, dst_col, flippedRow);
                }
            }

            if (gray) delete[] gray;
            if (alpha) delete[] alpha;
        }
    }

    else if (bandPalette)
    {
        //Palette indexed imagery doesn't support interpolation currently and only uses nearest
        //b/c interpolating palette indexes doesn't make sense.
        unsigned char *palette = new unsigned char[target_width * target_height];

        //image = new osg::Image;

        if (isCoverage == true)
        {
            //image = LandCover::createImage(tileSize);
            image = Image::create(
                Image::R32_SFLOAT,
                tileSize,
                tileSize);

            image->fill(glm::fvec4(NO_DATA_VALUE));

            // initialize all coverage texels to NODATA. -gw
            //ImageUtils::PixelWriter write(image.get());
            //write.assign(Color(NO_DATA_VALUE));
        }
        else
        {
            image = Image::create(
                pixelFormat,
                tileSize,
                tileSize);

            memset(image->data<unsigned char>(), 0, image->sizeInBytes());
        }

        rasterIO(
            bandPalette, 
            GF_Read,
            src_min_x, src_min_y, src_width, src_height,
            palette,
            target_width, target_height,
            GDT_Byte, 0, 0, 
            Image::NEAREST);

        //ImageUtils::PixelWriter write(image.get());

        Image::Pixel pixel;

        for (int src_row = 0, dst_row = tile_offset_top;
            src_row < target_height;
            src_row++, dst_row++)
        {
            unsigned int flippedRow = tileSize - dst_row - 1;
            for (int src_col = 0, dst_col = tile_offset_left;
                src_col < target_width;
                ++src_col, ++dst_col)
            {
                unsigned char p = palette[src_col + src_row * target_width];

                if (isCoverage)
                {
                    //if (_layer->coverageUsesPaletteIndex() == true)
                    //{
                    //    pixel.r = (float)p;
                    //}
                    //else
                    {
                        glm::u8vec4 color;
                        if (getPalleteIndexColor(bandPalette, p, color) &&
                            isValidValue((float)color.r, bandPalette)) // need this?
                        {
                            pixel.r = (float)color.r;
                        }
                        else
                        {
                            pixel.r = NO_DATA_VALUE;
                        }
                    }

                    glm::fvec4 fpixel = glm::fvec4(pixel) / 255.0f;

                    image->write(fpixel, dst_col, flippedRow);
                }
                else
                {
                    glm::u8vec4 color;
                    if (!getPalleteIndexColor(bandPalette, p, color))
                    {
                        color.a = 0.0f;
                    }
                    else if (!isValidValue((float)color.r, bandPalette)) // is this applicable for palettized data?
                    {
                        color.a = 0.0f;
                    }

                    glm::fvec4 fcolor = glm::fvec4(color) / 255.0f;
                    image->write(fcolor, dst_col, flippedRow);
                }
            }
        }

        delete[] palette;

    }
    else
    {
        Log::warn()
            << LC << "Could not find red, green and blue bands or gray bands in "
            << _layer->uri()->full()
            << ".  Cannot create image. " << std::endl;

        return Status(
            Status::ResourceUnavailable,
            "Could not find red, green, blue, or gray band");
    }

    return image;
}


//...................................................................

void GDAL::LayerBase::setURI(const URI& value) {
    _uri = value;
}
const optional<URI>& GDAL::LayerBase::uri() const {
    return _uri;
}
void GDAL::LayerBase::setConnection(const std::string& value) {
    _connection = value;
}
const optional<std::string>& GDAL::LayerBase::connection() const {
    return _connection;
}
void GDAL::LayerBase::setSubDataSet(unsigned value) {
    _subDataSet = value;
}
const optional<unsigned>& GDAL::LayerBase::subDataSet() const {
    return _subDataSet;
}
void GDAL::LayerBase::setInterpolation(const Image::Interpolation& value) {
    _interpolation = value;
}
const optional<Image::Interpolation>& GDAL::LayerBase::interpolation() const {
    return _interpolation;
}

//......................................................................

#undef LC
#define LC "[GDAL] \"" << getName() << "\" "

namespace
{
    template<typename T>
    Status openOnThisThread(
        const T* layer,
        shared_ptr<GDAL::Driver>& driver,
        Profile* profile,
        DataExtentList* out_dataExtents,
        const IOOptions& io)
    {
        driver = std::make_shared<GDAL::Driver>();

        auto elevationLayer = dynamic_cast<const ElevationLayer*>(layer);
        if (elevationLayer)
        {
            if (elevationLayer->noDataValue().has_value())
                driver->setNoDataValue(elevationLayer->noDataValue());
            if (elevationLayer->minValidValue().has_value())
                driver->setMinValidValue(elevationLayer->minValidValue());
            if (elevationLayer->maxValidValue().has_value())
                driver->setMaxValidValue(elevationLayer->maxValidValue());
        }

        if (layer->maxDataLevel().has_value())
            driver->setMaxDataLevel(layer->maxDataLevel());

        Status status = driver->open(
            layer->name(),
            layer,
            layer->tileSize(),
            out_dataExtents,
            io);

        if (status.failed())
            return status;

        if (driver->profile().valid() && profile != nullptr)
        {
            *profile = driver->profile();
        }

        return StatusOK;
    }
}
