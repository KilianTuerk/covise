/* This file is part of COVISE.

   You can use it under the terms of the GNU Lesser General Public License
   version 2.1 or later, see lgpl-2.1.txt.

 * License: LGPL 2+ */

#include <cstdio>
#include <iostream>
#include <sstream>
#include <anari/type_utility.h>
#include <config/CoviseConfig.h>
#include <cover/coVRConfig.h>
#include <cover/coVRPluginSupport.h>
#include "Renderer.h"

using namespace covise;
using namespace opencover;


void statusFunc(const void *userData,
    ANARIDevice device,
    ANARIObject source,
    ANARIDataType sourceType,
    ANARIStatusSeverity severity,
    ANARIStatusCode code,
    const char *message)
{
    (void)userData;
    if (severity == ANARI_SEVERITY_FATAL_ERROR)
        fprintf(stderr, "[FATAL] %s\n", message);
    else if (severity == ANARI_SEVERITY_ERROR)
        fprintf(stderr, "[ERROR] %s\n", message);
    else if (severity == ANARI_SEVERITY_WARNING)
        fprintf(stderr, "[WARN ] %s\n", message);
    else if (severity == ANARI_SEVERITY_PERFORMANCE_WARNING)
        fprintf(stderr, "[PERF ] %s\n", message);
    else if (severity == ANARI_SEVERITY_INFO)
        fprintf(stderr, "[INFO] %s\n", message);
}

static std::string getExt(const std::string &fileName)
{
    int pos = fileName.rfind('.');
    if (pos == fileName.npos)
        return "";
    return fileName.substr(pos);
}

static std::vector<std::string> string_split(std::string s, char delim)
{
    std::vector<std::string> result;

    std::istringstream stream(s);

    for (std::string token; std::getline(stream, token, delim); )
    {
        result.push_back(token);
    }

    return result;
}



Renderer::Renderer()
{
}

Renderer::~Renderer()
{
    cover->getScene()->removeChild(multiChannelDrawer);
}

void Renderer::loadScene(std::string fn)
{
    // deferred!
    fileName.value = fn;
    fileName.changed = true;
}

void Renderer::unloadScene(std::string fn)
{
    // NO!
}

void Renderer::loadVolume(const void *data, int sizeX, int sizeY, int sizeZ, int bpc,
                          float minValue, float maxValue)
{
    // deferred!
    volumeData.data = data;
    volumeData.sizeX = sizeX;
    volumeData.sizeY = sizeY;
    volumeData.sizeZ = sizeZ;
    volumeData.bpc = bpc;
    volumeData.minValue = minValue;
    volumeData.maxValue = maxValue;
    volumeData.changed = true;
}

void Renderer::unloadVolume()
{
    // NO!
}

void Renderer::loadVolumeRAW(std::string fn)
{
    // deferred!

    // parse dimensions
    std::vector<std::string> strings = string_split(fn, '_');

    int sizeX=0, sizeY=0, sizeZ=0;
    for (auto str : strings) {
        int res = sscanf(str.c_str(), "%dx%dx%dx", &sizeX, &sizeY, &sizeZ);
        if (res == 3)
            break;
    }

    size_t numVoxels = sizeX*size_t(sizeY)*sizeZ;
    if (numVoxels == 0)
        return;

    volumeData.data = new uint8_t[numVoxels];
    volumeData.sizeX = sizeX;
    volumeData.sizeY = sizeY;
    volumeData.sizeZ = sizeZ;
    volumeData.bpc = 1;// !
    volumeData.minValue = 0.f;
    volumeData.maxValue = 1.f;
    volumeData.changed = true;
    volumeData.deleteData = true;

    FILE *file = fopen(fn.c_str(), "rb");
    fread((void *)volumeData.data, numVoxels, 1, file);
    fclose(file);
}

void Renderer::unloadVolumeRAW(std::string fn)
{
    // NO!
}

void Renderer::expandBoundingSphere(osg::BoundingSphere &bs)
{
    if (!anari.world)
        return;

    float bounds[6];

    if (anari.volume) { // asgComputeBounds doesn't work for volumes yet...
        bounds[0] = -volumeData.sizeX*.5f;
        bounds[1] = -volumeData.sizeY*.5f;
        bounds[2] = -volumeData.sizeZ*.5f;
        bounds[3] =  volumeData.sizeX*.5f;
        bounds[4] =  volumeData.sizeY*.5f;
        bounds[5] =  volumeData.sizeZ*.5f;
    } else {
        asgComputeBounds(anari.root,
                         &bounds[0],&bounds[1],&bounds[2],
                         &bounds[3],&bounds[4],&bounds[5]);

    }

    osg::Vec3f minCorner(bounds[0],bounds[1],bounds[2]);
    osg::Vec3f maxCorner(bounds[3],bounds[4],bounds[5]);

    osg::Vec3f center = (maxCorner-minCorner)*.5f;
    float radius = (center-minCorner).length();
    bs.set(center, radius);
}

void Renderer::renderFrame(osg::RenderInfo &info)
{
    if (!multiChannelDrawer) {
        multiChannelDrawer = new MultiChannelDrawer(false, false);
        multiChannelDrawer->setMode(MultiChannelDrawer::AsIs);
        cover->getScene()->addChild(multiChannelDrawer);
        channelInfos.resize(multiChannelDrawer->numViews());
    }

    if (!anari.library)
        initANARI();

    if (!anari.library) // init failed!
        return;

    if (fileName.changed) {
        initScene();
        fileName.changed = false;
    }

    if (volumeData.changed) {
        initVolume();
        volumeData.changed = false;
    }

    for (unsigned chan=0; chan<multiChannelDrawer->numViews(); ++chan) {
        renderFrame(info, chan);
    }
}

void Renderer::renderFrame(osg::RenderInfo &info, unsigned chan)
{
    auto cam = coVRConfig::instance()->channels[chan].camera;
    auto vp = cam->getViewport();
    int width = vp->width();
    int height = vp->height();

    if (channelInfos[chan].width != width || channelInfos[chan].height != height) {
        channelInfos[chan].width = width;
        channelInfos[chan].height = height;
        multiChannelDrawer->resizeView(chan, width, height,
                                       channelInfos[chan].colorFormat,
                                       channelInfos[chan].depthFormat);
        multiChannelDrawer->clearColor(chan);
        multiChannelDrawer->clearDepth(chan);

        float r = coCoviseConfig::getFloat("r", "COVER.Background", 0.0f);
        float g = coCoviseConfig::getFloat("g", "COVER.Background", 0.0f);
        float b = coCoviseConfig::getFloat("b", "COVER.Background", 0.0f);
        float bgcolor[] = {r,g,b,1.f};
        anariSetParameter(anari.device, anari.renderer, "backgroundColor", ANARI_FLOAT32_VEC4,
                          bgcolor);
        anariCommitParameters(anari.device, anari.renderer);

        unsigned imgSize[] = {(unsigned)width,(unsigned)height};
        anariSetParameter(anari.device, anari.frames[chan], "size", ANARI_UINT32_VEC2, imgSize);
        anariCommitParameters(anari.device, anari.frames[chan]);
    }

    osg::Matrix mv = multiChannelDrawer->modelMatrix(chan) * multiChannelDrawer->viewMatrix(chan);
    osg::Matrix pr = multiChannelDrawer->projectionMatrix(chan);

    osg::Vec3f eye, center, up;
    mv.getLookAt(eye, center, up);
    osg::Vec3f dir = center-eye;

    float fovy, aspect, znear, zfar;
    pr.getPerspective(fovy, aspect, znear, zfar);

    float imgRegion[] = {0.f,0.f,1.f,1.f};

    anariSetParameter(anari.device, anari.cameras[chan], "aspect", ANARI_FLOAT32, &aspect);
    anariSetParameter(anari.device, anari.cameras[chan], "position", ANARI_FLOAT32_VEC3, eye.ptr());
    anariSetParameter(anari.device, anari.cameras[chan], "direction", ANARI_FLOAT32_VEC3, dir.ptr());
    anariSetParameter(anari.device, anari.cameras[chan], "up", ANARI_FLOAT32_VEC3, up.ptr());
    anariSetParameter(anari.device, anari.cameras[chan], "imageRegion", ANARI_FLOAT32_BOX2, imgRegion);
    anariCommitParameters(anari.device, anari.cameras[chan]);

    anariRenderFrame(anari.device, anari.frames[chan]);
    anariFrameReady(anari.device, anari.frames[chan], ANARI_WAIT);

    uint32_t widthOUT;
    uint32_t heightOUT;
    ANARIDataType typeOUT;
    const uint32_t *fbPointer = (const uint32_t *)anariMapFrame(anari.device, anari.frames[chan],
                                                                "channel.color",
                                                                &widthOUT,
                                                                &heightOUT,
                                                                &typeOUT);
    memcpy((uint32_t *)multiChannelDrawer->rgba(chan), fbPointer,
           widthOUT*heightOUT*anari::sizeOf(typeOUT));
    anariUnmapFrame(anari.device, anari.frames[chan], "channel.color");

    const uint32_t *dbPointer = (const uint32_t *)anariMapFrame(anari.device, anari.frames[chan],
                                                                "channel.depth",
                                                                &widthOUT,
                                                                &heightOUT,
                                                                &typeOUT);
    memcpy((uint32_t *)multiChannelDrawer->depth(chan), dbPointer,
           widthOUT*heightOUT*anari::sizeOf(typeOUT));

    anariUnmapFrame(anari.device, anari.frames[chan], "channel.depth");

    multiChannelDrawer->update();
    multiChannelDrawer->swapFrame();
}

void Renderer::initANARI()
{
    anari.library = anariLoadLibrary(anari.libtype.c_str(), statusFunc);
    if (!anari.library) return;
    anari.device = anariNewDevice(anari.library, anari.devtype.c_str());
    if (!anari.device) return;
    anariCommitParameters(anari.device, anari.device);
    anari.world = anariNewWorld(anari.device);
    anari.headLight = anariNewLight(anari.device,"directional");
    ANARIArray1D lights = anariNewArray1D(anari.device, &anari.headLight, 0, 0,
                                          ANARI_LIGHT, 1, 0);
    anariSetParameter(anari.device, anari.world, "light", ANARI_ARRAY1D, &lights);
    anariCommitParameters(anari.device, anari.world);
    anari.renderer = anariNewRenderer(anari.device, anari.renderertype.c_str());

    anari.frames.resize(multiChannelDrawer->numViews());
    anari.cameras.resize(multiChannelDrawer->numViews());
    for (unsigned chan=0; chan<multiChannelDrawer->numViews(); ++chan) {
        ANARIFrame &frame = anari.frames[chan];
        ANARICamera &camera = anari.cameras[chan];

        frame = anariNewFrame(anari.device);
        anariSetParameter(anari.device, frame, "world", ANARI_WORLD, &anari.world);

        ANARIDataType fbFormat = ANARI_UFIXED8_RGBA_SRGB;
        ANARIDataType dbFormat = ANARI_FLOAT32;
        anariSetParameter(anari.device, frame, "channel.color", ANARI_DATA_TYPE, &fbFormat);
        anariSetParameter(anari.device, frame, "channel.depth", ANARI_DATA_TYPE, &dbFormat);
        anariSetParameter(anari.device, frame, "renderer", ANARI_RENDERER, &anari.renderer);

        camera = anariNewCamera(anari.device, "perspective");
        anariSetParameter(anari.device, frame, "camera", ANARI_CAMERA, &camera);
        anariCommitParameters(anari.device, frame);
    }

    anariRelease(anari.device, lights);
}


// Scene loading

#define ASG_SAFE_CALL(X) X // TODO!

void Renderer::initScene()
{
    const char *fileName = this->fileName.value.c_str();

    anari.root = asgNewObject();

    // Load from file
    std::string ext = getExt(fileName);
    if (ext==".pbf" || ext==".pbrt")
        ASG_SAFE_CALL(asgLoadPBRT(anari.root, fileName, 0));
    else
        ASG_SAFE_CALL(asgLoadASSIMP(anari.root, fileName, 0));

    // Build up ANARI world
    ASG_SAFE_CALL(asgBuildANARIWorld(anari.root, anari.device, anari.world,
                                     ASG_BUILD_WORLD_FLAG_FULL_REBUILD, 0));

    anariCommitParameters(anari.device, anari.world);
}

void Renderer::initVolume()
{
    if (volumeData.bpc == 1) {
        // Convert to float..
        volumeData.voxels.resize(volumeData.sizeX*size_t(volumeData.sizeY)*volumeData.sizeZ);

        for (size_t i=0; i<volumeData.voxels.size(); ++i) {
            volumeData.voxels[i] = ((const uint8_t *)volumeData.data)[i]/255.999f;
        }

        anari.root = asgNewObject();

        anari.volume = asgNewStructuredVolume(volumeData.voxels.data(),
                                              volumeData.sizeX, volumeData.sizeY, volumeData.sizeZ,
                                              ASG_DATA_TYPE_FLOAT32, nullptr);
        ASG_SAFE_CALL(asgStructuredVolumeSetRange(anari.volume,
                                                  volumeData.minValue, volumeData.maxValue));

        volumeData.rgbLUT.resize(15);
        volumeData.alphaLUT.resize(5);

        anari.lut = asgNewLookupTable1D(volumeData.rgbLUT.data(),
                                        volumeData.alphaLUT.data(),
                                        volumeData.alphaLUT.size(),
                                        nullptr);
        ASG_SAFE_CALL(asgMakeDefaultLUT1D(anari.lut, ASG_LUT_ID_DEFAULT_LUT));
        ASG_SAFE_CALL(asgStructuredVolumeSetLookupTable1D(anari.volume, anari.lut));

        ASG_SAFE_CALL(asgObjectAddChild(anari.root, anari.volume));

        ASG_SAFE_CALL(asgBuildANARIWorld(anari.root, anari.device, anari.world,
                                         ASG_BUILD_WORLD_FLAG_FULL_REBUILD, 0));

        anariCommitParameters(anari.device, anari.world);
    }

    if (volumeData.deleteData) {
        switch (volumeData.bpc) {
            case 1:
                delete[] (uint8_t *)volumeData.data;
                break;

            case 2:
                delete[] (uint16_t *)volumeData.data;
                break;

            case 4:
                delete[] (float *)volumeData.data;
                break;
        }
        volumeData.deleteData = false;
    }
}

