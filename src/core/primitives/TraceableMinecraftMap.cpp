#include "TraceableMinecraftMap.hpp"

#include "primitives/TriangleMesh.hpp"

#include "materials/ConstantTexture.hpp"

#include "mc-loader/ResourcePackLoader.hpp"
#include "mc-loader/BiomeTexture.hpp"
#include "mc-loader/MapLoader.hpp"
#include "mc-loader/ModelRef.hpp"

#include "cameras/PinholeCamera.hpp"

#include "bsdfs/TransparencyBsdf.hpp"
#include "bsdfs/LambertBsdf.hpp"

#include "math/Mat4f.hpp"

#include "io/ImageIO.hpp"
#include "io/MeshIO.hpp"
#include "io/Scene.hpp"

#include <tinyformat/tinyformat.hpp>
#include <unordered_set>

namespace Tungsten {

struct MapIntersection
{
    float u, v;
    uint32 id;
};

TraceableMinecraftMap::TraceableMinecraftMap()
: _missingBsdf(std::make_shared<LambertBsdf>())
{
    _missingBsdf->setAlbedo(std::make_shared<ConstantTexture>(0.2f));
    _bsdfs.emplace_back(_missingBsdf);
}

TraceableMinecraftMap::TraceableMinecraftMap(const TraceableMinecraftMap &o)
: Primitive(o)
{
    _mapPath = o._mapPath;
    _packPath = o._packPath;

    _missingBsdf = o._missingBsdf;
    _bsdfCache = o._bsdfCache;
}

void TraceableMinecraftMap::getTexProperties(const std::string &path, int w, int h,
        int &tileW, int &tileH, bool &clamp, bool &linear)
{
    tileW = w;
    tileH = w;
    linear = false;
    clamp = false;

    File meta(path + ".mcmeta");
    if (!meta.exists())
        return;

    std::string json = FileUtils::loadText(meta.path().c_str());
    if (json.empty())
        return;

    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError() || !document.IsObject())
        return;

    const rapidjson::Value::Member *animation = document.FindMember("animation");
    const rapidjson::Value::Member *texture   = document.FindMember("texture");

    if (animation) {
        int numTilesX, numTilesY;
        if (JsonUtils::fromJson(animation->value, "width", numTilesX))
            tileW = w/numTilesX;
        if (JsonUtils::fromJson(animation->value, "height", numTilesY))
            tileH = h/numTilesY;
    }

    if (texture) {
        JsonUtils::fromJson(texture->value, "blur", linear);
        JsonUtils::fromJson(texture->value, "clamp", clamp);
    }
}

void TraceableMinecraftMap::loadTexture(ResourcePackLoader &pack, const std::string &name,
        std::shared_ptr<BitmapTexture> &albedo, std::shared_ptr<BitmapTexture> &opacity, Vec4c tint)
{
    std::string path = pack.textureBasePath() + name + ".png";

    int w, h;
    std::unique_ptr<uint8[]> img = ImageIO::loadLdr(path, TexelConversion::REQUEST_RGB, w, h);

    if (!img)
        return;

    int tileW, tileH;
    bool linear, clamp;
    getTexProperties(path, w, h, tileW, tileH, clamp, linear);

    int yOffset = ((h/tileH)/2)*tileH;
    bool opaque = true;
    std::unique_ptr<uint8[]> tile(new uint8[tileW*tileH*4]), alpha;
    for (int y = 0; y < tileH; ++y) {
        for (int x = 0; x < tileW; ++x) {
            for (int i = 0; i < 4; ++i)
                tile[i + 4*(x + y*tileW)] = (int(img[i + 4*(x + (y + yOffset)*w)])*tint[i]) >> 8;
            opaque = opaque && (tile[3 + 4*(x + y*tileW)] == 0xFF);
        }
    }
    if (!opaque) {
        alpha.reset(new uint8[tileH*tileW]);
        for (int i = 0; i < tileH*tileW; ++i)
            alpha[i] = tile[i*4 + 3];
    }

    albedo = std::make_shared<BitmapTexture>(name + ".png", tile.release(),
            tileW, tileH, BitmapTexture::TexelType::RGB_LDR, linear, clamp);

    if (!opaque)
        opacity = std::make_shared<BitmapTexture>(name + ".png", alpha.release(),
                tileW, tileH, BitmapTexture::TexelType::SCALAR_LDR, linear, clamp);
}

int TraceableMinecraftMap::fetchBsdf(ResourcePackLoader &pack, const TexturedQuad &quad)
{
    std::string key = quad.texture;

    if (!quad.overlay.empty())
        key += "&" + quad.overlay;

    Vec4c filter(255);
    if (quad.tintIndex == ResourcePackLoader::TINT_FOLIAGE)
        key += "-BIOME_FOLIAGE";
    else if (quad.tintIndex == ResourcePackLoader::TINT_GRASS)
        key += "-BIOME_GRASS";
    else if (quad.tintIndex != ResourcePackLoader::TINT_NONE) {
        int level = quad.tintIndex - ResourcePackLoader::TINT_REDSTONE0;
        key += tfm::format("-REDSTONE_TINT%i", level);
        filter = Vec4c(Vec4i((191*level)/15 + 64, (64*level)/15, 0, 255));
    }

    auto iter = _bsdfCache.find(key);
    if (iter != _bsdfCache.end())
        return iter->second;

    std::shared_ptr<BitmapTexture> albedo, opacity;
    loadTexture(pack, quad.texture, albedo, opacity, filter);

    if (!albedo)
        return 0;

    std::shared_ptr<BitmapTexture> overlayAlbedo, overlayMask;
    if (!quad.overlay.empty())
        loadTexture(pack, quad.overlay, overlayAlbedo, overlayMask, Vec4c(255));

    std::shared_ptr<BitmapTexture> substrate, overlay, overlayOpacity;
    if (overlayAlbedo) {
        substrate = albedo;
        overlay = overlayAlbedo;
        overlayOpacity = overlayMask;
    } else {
        overlay = albedo;
    }

    std::shared_ptr<Texture> base;
    if (overlayAlbedo || quad.tintIndex != ResourcePackLoader::TINT_NONE)
        base = std::make_shared<BiomeTexture>(substrate, overlay, overlayOpacity, _biomeMap, quad.tintIndex);
    else
        base = albedo;

    std::shared_ptr<Bsdf> bsdf(std::make_shared<LambertBsdf>());
    bsdf->setAlbedo(base);

    if (opacity)
        bsdf = std::make_shared<TransparencyBsdf>(opacity, bsdf);

    _bsdfCache.insert(std::make_pair(key, int(_bsdfs.size())));
    _bsdfs.emplace_back(std::move(bsdf));

    return _bsdfs.size() - 1;
}

void TraceableMinecraftMap::buildBiomeColors(ResourcePackLoader &pack, int rx, int rz, uint8 *biomes)
{
    std::unique_ptr<uint8[]> grassTop(new uint8[256*256*4]), grassBottom(new uint8[256*256*4]);
    std::unique_ptr<uint8[]> foliageTop(new uint8[256*256*4]), foliageBottom(new uint8[256*256*4]);
    std::unique_ptr<uint8[]> tmp(new uint8[256*256*4]);
    std::unique_ptr<float[]> heights(new float[256*256*4]);

    auto get = [](std::unique_ptr<uint8[]> &dst, int x, int z) -> Vec4c& {
        return *(reinterpret_cast<Vec4c *>(dst.get()) + x + z*256);
    };
    auto toRgba = [](Vec3f x) {
        return Vec4c(Vec4f(x.x(), x.y(), x.z(), 1.0f)*255.0f);
    };

    for (int z = 0; z < 256; ++z) {
        for (int x = 0; x < 256; ++x) {
            BiomeColor color = pack.biomeColors()[biomes[x + z*256]];

            get(grassTop,      x, z) = toRgba(color.grassTop);
            get(grassBottom,   x, z) = toRgba(color.grassBottom);
            get(foliageTop,    x, z) = toRgba(color.foliageTop);
            get(foliageBottom, x, z) = toRgba(color.foliageBottom);
            heights[x + z*256] = color.height;
        }
    }

    const int dx[9] = {-1, 0, 1, -1, 0, 1, -1,  0,  1};
    const int dz[9] = { 1, 1, 1,  0, 0, 0, -1, -1, -1};

    const uint8 gaussianKernel[9] = {
        16, 8, 16,
         8, 4,  8,
        16, 8, 16,
    };

    auto blurColors = [&](std::unique_ptr<uint8[]> &dst) {
        std::memset(tmp.get(), 0, 256*256*4*sizeof(uint8));

        for (int z = 0; z < 256; ++z)
            for (int x = 0; x < 256; ++x)
                for (int i = 0; i < 9; ++i)
                    get(tmp, x, z) += get(dst, clamp(x + dx[i], 0, 255), clamp(z + dz[i], 0, 255))/gaussianKernel[i];

        tmp.swap(dst);
    };
    auto makeTexture = [](uint8 *tex) {
        return std::unique_ptr<BitmapTexture>(new BitmapTexture("", tex,
                256, 256, BitmapTexture::TexelType::RGB_LDR, true, true));
    };

    blurColors(grassTop);
    blurColors(grassBottom);
    blurColors(foliageTop);
    blurColors(foliageBottom);

    _biomes.emplace_back(new BiomeTileTexture{
        makeTexture(grassTop.release()),
        makeTexture(grassBottom.release()),
        makeTexture(foliageTop.release()),
        makeTexture(foliageBottom.release()),
        std::move(heights)
    });

    _biomeMap.insert(std::make_pair(Vec2i(rx, rz), _biomes.back().get()));
}

void TraceableMinecraftMap::convertQuads(ResourcePackLoader &pack, const std::vector<TexturedQuad> &model, const Mat4f &transform)
{
    int base = _geometry.size()*4;
    int simdBase = base/4;
    int numTris = model.size()*2;
    int numSimdTris = (numTris + 3)/4;
    _geometry.resize(simdBase + numSimdTris);
    _triInfo.resize(base + numTris);

    _modelSpan.emplace_back(simdBase, simdBase + numSimdTris);

    int idx = base;
    for (const TexturedQuad &quad : model) {
        int material = fetchBsdf(pack, quad);

        Vec2f uv0(quad.uv0.x(), 1.0f - quad.uv0.y());
        Vec2f uv1(quad.uv1.x(), 1.0f - quad.uv1.y());
        Vec2f uv2(quad.uv2.x(), 1.0f - quad.uv2.y());
        Vec2f uv3(quad.uv3.x(), 1.0f - quad.uv3.y());
        Vec3f p0 = transform*quad.p0;
        Vec3f p1 = transform*quad.p1;
        Vec3f p2 = transform*quad.p2;
        Vec3f p3 = transform*quad.p3;

        Vec3f Ng = ((p1 - p0).cross(p2 - p0)).normalized();

        _triInfo[idx + 0] = TriangleInfo{Ng, uv0, uv2, uv1, material};
        _triInfo[idx + 1] = TriangleInfo{Ng, uv0, uv3, uv2, material};

        _geometry[idx/4].set(idx % 4, p0, p2, p1, idx); idx++;
        _geometry[idx/4].set(idx % 4, p0, p3, p2, idx); idx++;
    }
    for (int i = idx; i < ((idx + 3)/4)*4; ++i)
        _geometry[i/4].set(i % 4, Vec3f(0.0f), Vec3f(0.0f), Vec3f(0.0f), 0);
}

void TraceableMinecraftMap::buildModel(ResourcePackLoader &pack, const ModelRef &model)
{
    if (!model.builtModel())
        return;

    Mat4f tform =
         Mat4f::translate(Vec3f(0.5f))
        *Mat4f::rotXYZ(Vec3f(0.0f, float(-model.yRot()), 0.0f))
        *Mat4f::rotXYZ(Vec3f(float(model.xRot()), 0.0f, 0.0f))
        *Mat4f::rotXYZ(Vec3f(0.0f, 0.0f, float(model.zRot())))
        *Mat4f::scale(Vec3f(1.0f/16.0f))
        *Mat4f::translate(Vec3f(-8.0f));
    _modelToPrimitive.insert(std::make_pair(&model, _modelSpan.size()));

    convertQuads(pack, *model.builtModel(), tform);
}

void TraceableMinecraftMap::buildModels(ResourcePackLoader &pack)
{
    for (const BlockDescriptor &desc : pack.blockDescriptors())
        for (const BlockVariant &var : desc.variants())
            for (const ModelRef &model : var.models())
                buildModel(pack, model);
}

int TraceableMinecraftMap::resolveLiquidBlock(ResourcePackLoader &pack, int x, int y, int z)
{
    uint32 blocks[18];
    int levels[9] = {0};
    int isAir[9] = {0};

    for (int ny = y, idx = 0; ny <= y + 1; ++ny) {
        for (int nz = z - 1; nz <= z + 1; ++nz) {
            for (int nx = x - 1; nx <= x + 1; ++nx, ++idx) {
                blocks[idx] = getBlock(nx, ny, nz);
                if (idx < 9 && blocks[idx] == 0)
                    isAir[idx] = 1;
                if (ny > y && pack.isLiquid(blocks[idx]))
                    levels[idx - 9] = 9;
                else if (pack.isLiquid(blocks[idx]))
                    levels[idx] = pack.liquidLevel(blocks[idx]);
            }
        }
    }
    bool isLava = pack.isLava(blocks[4]);
    bool hasFace[] = {
        pack.isLiquid(blocks[3]),
        pack.isLiquid(blocks[5]),
        pack.isLiquid(getBlock(x, y - 1, z)),
        pack.isLiquid(blocks[13]),
        pack.isLiquid(blocks[1]),
        pack.isLiquid(blocks[7]),
    };

    int heights[] = {
        max(levels[0], levels[1], levels[3], levels[4]),
        max(levels[1], levels[2], levels[4], levels[5]),
        max(levels[3], levels[4], levels[6], levels[7]),
        max(levels[4], levels[5], levels[7], levels[8])
    };
    int scale[] = {
        1 + isAir[0] + isAir[1] + isAir[3] + isAir[4],
        1 + isAir[1] + isAir[2] + isAir[4] + isAir[5],
        1 + isAir[3] + isAir[4] + isAir[6] + isAir[7],
        1 + isAir[4] + isAir[5] + isAir[7] + isAir[8]
    };
    for (int i = 0; i < 4; ++i)
        if (heights[i] >= 8)
            scale[i] = 1;

    uint32 key = 0;
    for (int i = 0; i < 4; ++i)
        key = (key << 4) | uint32(heights[i]);
    for (int i = 0; i < 6; ++i)
        key = (key << 1) | (hasFace[i] ? 1 : 0);
    key = (key << 1) | (isLava ? 1 : 0);

    auto iter = _liquidMap.find(key);
    if (iter != _liquidMap.end())
        return iter->second;

    static Vec3f faceVerts[6][4] = {
        {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}},
        {{1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 1.0f}},
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {0.0f, 1.0f, 1.0f}},
        {{1.0f, 1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{0.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    static const int indices[6][4] = {
        {0, 2, 2, 0}, {3, 1, 1, 3}, {2, 3, 1, 0},
        {0, 1, 3, 2}, {1, 0, 0, 1}, {2, 3, 3, 2}
    };
    static const int indexToUv[4][4] = {{4, 5, 7, 8}, {3, 4, 6, 7}, {1, 2, 4, 5}, {0, 1, 3, 4}};
    CONSTEXPR float neg = 0.5f - 0.70711f;
    CONSTEXPR float pos = 0.5f + 0.70711f;
    static const Vec2f uvs[10][4] = {
        {{0.5f,  pos}, { neg, 0.5f}, {0.5f,  neg}, { pos, 0.5f}},
        {{1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
        {{ pos, 0.5f}, {0.5f,  pos}, { neg, 0.5f}, {0.5f,  neg}},

        {{1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}},
        {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}},

        {{ neg, 0.5f}, {0.5f,  neg}, { pos, 0.5f}, {0.5f,  pos}},
        {{0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {0.0f, 0.0f}},
        {{0.5f,  neg}, { pos, 0.5f}, {0.5f,  pos}, { neg, 0.5f}},

        {{1.0f, -1.0f}, {-1.0f, -1.0f}, {-1.0f, 1.0f}, {1.0f, 1.0f}},
    };

    std::vector<TexturedQuad> model;

    auto buildVertex = [&](int i, int t, int uvIndex, Vec3f &posDst, Vec2f &uvDst) {
        int idx = indices[i][t];
        posDst = faceVerts[i][t];
        posDst.y() *= heights[idx]/(9.0f*scale[idx]);

        Vec3f x = faceVerts[i][1] - faceVerts[i][0];
        Vec3f y = faceVerts[i][3] - faceVerts[i][0];
        float u = x.dot(posDst - faceVerts[i][0])/x.lengthSq();
        float v = y.dot(posDst - faceVerts[i][0])/y.lengthSq();
        uvDst = uvs[uvIndex][0]*(1.0f - u - v) + uvs[uvIndex][1]*u + uvs[uvIndex][3]*v;
        uvDst = uvDst*0.5f + 0.5f;
    };

    for (int i = 0; i < 6; ++i) {
        if (hasFace[i])
            continue;

        int maxDiff = 0;
        int idx = 4;
        if (i/2 == 1) {
            for (int j = 0, k = 3, l = 2; j < 4; l = k, k = j, ++j) {
                int diffS = heights[indices[i][k]] - heights[indices[i][j]];
                int diffD = heights[indices[i][l]] - heights[indices[i][j]];
                if (diffS > maxDiff) maxDiff = diffS, idx = indexToUv[indices[i][k]][indices[i][j]];
                if (diffD > maxDiff) maxDiff = diffD, idx = indexToUv[indices[i][l]][indices[i][j]];
            }
            if (idx == 4)
                idx = 9;
        }

        TexturedQuad quad;
        quad.texture = pack.liquidTexture(isLava, idx == 9);
        quad.tintIndex = -1;
        buildVertex(i, 0, idx, quad.p0, quad.uv0);
        buildVertex(i, 1, idx, quad.p1, quad.uv1);
        buildVertex(i, 2, idx, quad.p2, quad.uv2);
        buildVertex(i, 3, idx, quad.p3, quad.uv3);
        model.emplace_back(std::move(quad));
    }

    convertQuads(pack, model, Mat4f());

    _liquidMap.insert(std::make_pair(key, int(_modelSpan.size()) - 1));

    return _modelSpan.size() - 1;
}

void TraceableMinecraftMap::resolveBlocks(ResourcePackLoader &pack)
{
    struct DeferredBlock
    {
        ElementType &dst;
        ElementType value;
    };
    std::vector<DeferredBlock> deferredBlocks;

    for (auto &region : _regions) {
        region.second->iterateNonZeroVoxels([&](ElementType &voxel, int x, int y, int z) {
            int globalX = region.first.x()*256 + x;
            int globalZ = region.first.y()*256 + z;
            if (pack.isSpecialBlock(voxel)) {
                const ModelRef *ref = pack.mapSpecialBlock(*this, globalX, y, globalZ,
                        x + 256*y + 256*256*z, voxel);
                ElementType value = 0;
                auto iter = _modelToPrimitive.find(ref);
                if (iter != _modelToPrimitive.end())
                    value = iter->second + 1;

                deferredBlocks.push_back(DeferredBlock{voxel, value});
            } else if (pack.isLiquid(voxel)) {
                deferredBlocks.push_back(DeferredBlock{
                    voxel, ElementType(resolveLiquidBlock(pack, globalX, y, globalZ)) + 1
                });
            }
        });
    }

    for (auto &region : _regions) {
        region.second->iterateNonZeroVoxels([&](ElementType &voxel, int x, int y, int z) {
            if (!pack.isSpecialBlock(voxel) && !pack.isLiquid(voxel)) {
                const ModelRef *ref = pack.mapBlock(voxel, x + 256*y + 256*256*z);
                auto iter = _modelToPrimitive.find(ref);
                if (iter == _modelToPrimitive.end())
                    voxel = 0;
                else
                    voxel = iter->second + 1;
            }
        });
    }
    for (DeferredBlock &block : deferredBlocks)
        block.dst = block.value;
}

void TraceableMinecraftMap::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    Primitive::fromJson(v, scene);

    JsonUtils::fromJson(v, "map_path", _mapPath);
    JsonUtils::fromJson(v, "resource_path", _packPath);

    Bvh::PrimVector prims;

    _bounds = Box3f();

    ResourcePackLoader pack(_packPath);
    buildModels(pack);

    MapLoader<ElementType> loader(_mapPath);
    loader.loadRegions([&](int x, int z, int height, ElementType *data, uint8 *biomes) {
        Box3f bounds(Vec3f(x*256.0f, 0.0f, z*256.0f), Vec3f((x + 1)*256.0f, float(height), (z + 1)*256.0f));
        Vec3f centroid((x + 0.5f)*256.0f, height*0.5f, (z + 0.5f)*256.0f);

        _bounds.grow(bounds);

        prims.emplace_back(bounds, centroid, _grids.size());

        buildBiomeColors(pack, x, z, biomes);

        _grids.emplace_back(new HierarchicalGrid(bounds.min(), data));
        _regions[Vec2i(x, z)] = _grids.back().get();
    });

    resolveBlocks(pack);

    _chunkBvh.reset(new Bvh::BinaryBvh(std::move(prims), 1));
}

rapidjson::Value TraceableMinecraftMap::toJson(Allocator &allocator) const
{
    rapidjson::Value v = Primitive::toJson(allocator);
    v.AddMember("type", "minecraft_map", allocator);
    v.AddMember("map_path", _mapPath.c_str(), allocator);
    v.AddMember("resource_path", _packPath.c_str(), allocator);
    return std::move(v);
}

bool TraceableMinecraftMap::intersect(Ray &ray, IntersectionTemporary &data) const
{
    MapIntersection *isect = data.as<MapIntersection>();

    isect->blockIsect = false;
    isect->face = 0;

    float farT = ray.farT();
    Vec3f dT = std::abs(1.0f/ray.dir());

    _chunkBvh->trace(ray, [&](Ray &ray, uint32 id, float tMin) {
        _grids[id]->trace(ray, dT, tMin, [&](uint32 idx, const Vec3f &offset, float /*t*/) {
            Vec3f oldPos = ray.pos();
            ray.setPos(oldPos - offset);

            int start = _modelSpan[idx].first;
            int end   = _modelSpan[idx].second;

            for (int i = start; i < end; ++i)
                intersectTriangle4(ray, _geometry[i], isect->u, isect->v, isect->id);

            ray.setPos(oldPos);
            return ray.farT() < farT;
        });
    });

    if (ray.farT() < farT) {
        data.primitive = this;

        return true;
    }

    return false;
}

bool TraceableMinecraftMap::occluded(const Ray &ray) const
{
    IntersectionTemporary data;
    Ray temp(ray);
    return intersect(temp, data);
}

bool TraceableMinecraftMap::hitBackside(const IntersectionTemporary &/*data*/) const
{
    return false;
}

void TraceableMinecraftMap::intersectionInfo(const IntersectionTemporary &data, IntersectionInfo &info) const
{
    const MapIntersection *isect = data.as<MapIntersection>();

    uint32 id = isect->id;
    info.Ng = info.Ns = _triInfo[id].Ng;
    Vec2f uv0 = _triInfo[id].uv0;
    Vec2f uv1 = _triInfo[id].uv1;
    Vec2f uv2 = _triInfo[id].uv2;
    info.uv = (1.0f - isect->u - isect->v)*uv0 + isect->u*uv1 + isect->v*uv2;
    info.bsdf = _bsdfs[_triInfo[id].material].get();
    info.primitive = this;
}

bool TraceableMinecraftMap::tangentSpace(const IntersectionTemporary &/*data*/, const IntersectionInfo &/*info*/,
        Vec3f &/*T*/, Vec3f &/*B*/) const
{
    return false;
}

bool TraceableMinecraftMap::isSamplable() const
{
    return false;
}

void TraceableMinecraftMap::makeSamplable(uint32 /*threadIndex*/)
{
}

float TraceableMinecraftMap::inboundPdf(uint32 /*threadIndex*/, const IntersectionTemporary &/*data*/,
        const IntersectionInfo &/*info*/, const Vec3f &/*p*/, const Vec3f &/*d*/) const
{
    return 0.0f;
}

bool TraceableMinecraftMap::sampleInboundDirection(uint32 /*threadIndex*/, LightSample &/*sample*/) const
{
    return false;
}

bool TraceableMinecraftMap::sampleOutboundDirection(uint32 /*threadIndex*/, LightSample &/*sample*/) const
{
    return false;
}

bool TraceableMinecraftMap::invertParametrization(Vec2f /*uv*/, Vec3f &/*pos*/) const
{
    return false;
}

bool TraceableMinecraftMap::isDelta() const
{
    return false;
}

bool TraceableMinecraftMap::isInfinite() const
{
    return false;
}

float TraceableMinecraftMap::approximateRadiance(uint32 /*threadIndex*/, const Vec3f &/*p*/) const
{
    return -1.0f;
}

Box3f TraceableMinecraftMap::bounds() const
{
    return _bounds;
}

const TriangleMesh &TraceableMinecraftMap::asTriangleMesh()
{
    if (!_proxy) {
        _proxy.reset(new TriangleMesh());
        _proxy->makeCube();
    }
    return *_proxy;
}

int TraceableMinecraftMap::numBsdfs() const
{
    return _bsdfs.size();
}

std::shared_ptr<Bsdf> &TraceableMinecraftMap::bsdf(int index)
{
    return _bsdfs[index];
}

void TraceableMinecraftMap::prepareForRender()
{

}

void TraceableMinecraftMap::cleanupAfterRender()
{
    /*_grids.clear();
    _chunkBvh.reset();

    for (auto &m : _models)
        m->cleanupAfterRender();*/
}

Primitive *TraceableMinecraftMap::clone()
{
    return new TraceableMinecraftMap(*this);
}

}