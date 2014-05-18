#include <functional>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/filewritestream.h>

#include "JsonUtils.hpp"
#include "FileUtils.hpp"
#include "Scene.hpp"

#include "integrators/PathTraceIntegrator.hpp"
#include "integrators/RayStreamIntegrator.hpp"

#include "primitives/InfiniteSphereCap.hpp"
#include "primitives/InfiniteSphere.hpp"
#include "primitives/Spotlight.hpp"
#include "primitives/Sphere.hpp"
#include "primitives/Quad.hpp"
#include "primitives/Mesh.hpp"

#include "materials/CheckerTexture.hpp"
#include "materials/BladeTexture.hpp"
#include "materials/DiskTexture.hpp"

#include "volume/HeterogeneousMedium.hpp"
#include "volume/HomogeneousMedium.hpp"
#include "volume/AtmosphericMedium.hpp"

#include "bsdfs/RoughDielectricBsdf.hpp"
#include "bsdfs/RoughConductorBsdf.hpp"
#include "bsdfs/RoughPlasticBsdf.hpp"
#include "bsdfs/DielectricBsdf.hpp"
#include "bsdfs/SmoothCoatBsdf.hpp"
#include "bsdfs/RoughCoatBsdf.hpp"
#include "bsdfs/ConductorBsdf.hpp"
#include "bsdfs/OrenNayarBsdf.hpp"
#include "bsdfs/ThinSheetBsdf.hpp"
#include "bsdfs/ForwardBsdf.hpp"
#include "bsdfs/LambertBsdf.hpp"
#include "bsdfs/PlasticBsdf.hpp"
#include "bsdfs/MirrorBsdf.hpp"
#include "bsdfs/PhongBsdf.hpp"
#include "bsdfs/MixedBsdf.hpp"
#include "bsdfs/NullBsdf.hpp"
#include "bsdfs/Bsdf.hpp"

#include "cameras/ThinlensCamera.hpp"
#include "cameras/PinholeCamera.hpp"

#include "Debug.hpp"

namespace Tungsten
{

std::shared_ptr<Medium> Scene::instantiateMedium(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Medium> result;
    if (type == "homogeneous")
        result = std::make_shared<HomogeneousMedium>();
    else if (type == "heterogeneous")
        result = std::make_shared<HeterogeneousMedium>();
    else if (type == "atmosphere")
        result = std::make_shared<AtmosphericMedium>();
    else
        FAIL("Unkown medium type: '%s'", type.c_str());
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Bsdf> Scene::instantiateBsdf(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Bsdf> result;
    if (type == "lambert")
        result = std::make_shared<LambertBsdf>();
    else if (type == "phong")
        result = std::make_shared<PhongBsdf>();
    else if (type == "mixed")
        result = std::make_shared<MixedBsdf>();
    else if (type == "dielectric")
        result = std::make_shared<DielectricBsdf>();
    else if (type == "conductor")
        result = std::make_shared<ConductorBsdf>();
    else if (type == "mirror")
        result = std::make_shared<MirrorBsdf>();
    else if (type == "rough_conductor")
        result = std::make_shared<RoughConductorBsdf>();
    else if (type == "rough_dielectric")
        result = std::make_shared<RoughDielectricBsdf>();
    else if (type == "smooth_coat")
        result = std::make_shared<SmoothCoatBsdf>();
    else if (type == "null")
        result = std::make_shared<NullBsdf>();
    else if (type == "forward")
        result = std::make_shared<ForwardBsdf>();
    else if (type == "thinsheet")
        result = std::make_shared<ThinSheetBsdf>();
    else if (type == "oren_nayar")
        result = std::make_shared<OrenNayarBsdf>();
    else if (type == "plastic")
        result = std::make_shared<PlasticBsdf>();
    else if (type == "rough_plastic")
        result = std::make_shared<RoughPlasticBsdf>();
    else if (type == "rough_coat")
        result = std::make_shared<RoughCoatBsdf>();
    else
        FAIL("Unkown bsdf type: '%s'", type.c_str());
    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Primitive> Scene::instantiatePrimitive(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Primitive> result;
    if (type == "mesh")
        result = std::make_shared<TriangleMesh>();
    else if (type == "sphere")
        result = std::make_shared<Sphere>();
    else if (type == "quad")
        result = std::make_shared<Quad>();
    else if (type == "spot")
        result = std::make_shared<Spotlight>();
    else if (type == "infinite_sphere")
        result = std::make_shared<InfiniteSphere>();
    else if (type == "infinite_sphere_cap")
        result = std::make_shared<InfiniteSphereCap>();
    else
        FAIL("Unknown primitive type: '%s'", type.c_str());

    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Camera> Scene::instantiateCamera(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Camera> result;
    if (type == "pinhole")
        result = std::make_shared<PinholeCamera>();
    else if (type == "thinlens")
        result = std::make_shared<ThinlensCamera>();
    else
        FAIL("Unknown camera type: '%s'", type.c_str());

    result->fromJson(value, *this);
    return result;
}

std::shared_ptr<Integrator> Scene::instantiateIntegrator(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Integrator> result;
    if (type == "path_trace")
        result = std::make_shared<PathTraceIntegrator>();
    else if (type == "ray_stream")
        result = std::make_shared<RayStreamIntegrator>();
    else
        FAIL("Unknown integrator type: '%s'", type.c_str());

    result->fromJson(value, *this);
    return result;
}

template<int Dimension>
std::shared_ptr<Texture<true, Dimension>>
    Scene::instantiateScalarTexture(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Texture<true, Dimension>> result;
    if (type == "bitmap") {
        // Ugly special casing, but whatever
        std::string path(JsonUtils::as<std::string>(value, "path"));
        if (Dimension == 2)
            return fetchScalarMap(path);
        //return std::make_shared<BitmapTexture<true, Dimension>>(path);
        return nullptr;
    } else if (type == "constant") {
        result = std::make_shared<ConstantTexture<true, Dimension>>();
    } else if (type == "checker") {
        result = std::make_shared<CheckerTexture<true>>();
    } else if (type == "disk") {
        result = std::make_shared<DiskTexture<true>>();
    } else if (type == "blade") {
        result = std::make_shared<BladeTexture<true>>();
    } else {
        FAIL("Unkown texture type: '%s'", type.c_str());
    }

    result->fromJson(value, *this);
    return result;
}

template<int Dimension>
std::shared_ptr<Texture<false, Dimension>>
    Scene::instantiateColorTexture(std::string type, const rapidjson::Value &value) const
{
    std::shared_ptr<Texture<false, Dimension>> result;
    if (type == "bitmap") {
        // Ugly special casing, but whatever
        std::string path(JsonUtils::as<std::string>(value, "path"));
        if (Dimension == 2)
            return fetchColorMap(path);
        //return std::make_shared<BitmapTexture<false, Dimension>>(path);
        return nullptr;
    } else if (type == "constant") {
        result = std::make_shared<ConstantTexture<false, Dimension>>();
    } else if (type == "checker") {
        result = std::make_shared<CheckerTexture<false>>();
    } else if (type == "disk") {
        result = std::make_shared<DiskTexture<false>>();
    } else if (type == "blade") {
        result = std::make_shared<BladeTexture<false>>();
    } else {
        FAIL("Unkown texture type: '%s'", type.c_str());
    }

    result->fromJson(value, *this);
    return result;
}

template<typename Instantiator, typename Element>
void Scene::loadObjectList(const rapidjson::Value &container, Instantiator instantiator, std::vector<std::shared_ptr<Element>> &result)
{
    for (unsigned i = 0; i < container.Size(); ++i) {
        if (container[i].IsObject())
            result.push_back(instantiator(JsonUtils::as<std::string>(container[i], "type"), container[i]));
        else
            LOG("Scene", WARN, "Don't know what to do with non-object");
    }
}

template<typename T>
std::shared_ptr<T> Scene::findObject(const std::vector<std::shared_ptr<T>> &list, std::string name) const
{
    for (const std::shared_ptr<T> &t : list)
        if (t->name() == name)
            return t;
    FAIL("Unable to find object '%s'", name.c_str());
    return nullptr;
}

template<typename T, typename Instantiator>
std::shared_ptr<T> Scene::fetchObject(const std::vector<std::shared_ptr<T>> &list, const rapidjson::Value &v, Instantiator instantiator) const
{
    if (v.IsString()) {
        return findObject(list, v.GetString());
    } else if (v.IsObject()) {
        return instantiator(JsonUtils::as<std::string>(v, "type"), v);
    } else {
        FAIL("Unkown value type");
        return nullptr;
    }
}

std::shared_ptr<Medium> Scene::fetchMedium(const rapidjson::Value &v) const
{
    using namespace std::placeholders;
    return fetchObject(_media, v, std::bind(&Scene::instantiateMedium, this, _1, _2));
}

std::shared_ptr<Bsdf> Scene::fetchBsdf(const rapidjson::Value &v) const
{
    using namespace std::placeholders;
    return fetchObject(_bsdfs, v, std::bind(&Scene::instantiateBsdf, this, _1, _2));
}

template<int Dimension>
std::shared_ptr<Texture<true, Dimension>> Scene::fetchScalarTexture(const rapidjson::Value &v) const
{
    if (v.IsString()) {
        if (Dimension == 2)
            return fetchScalarMap(v.GetString());
        else
            FAIL("Cannot instantiate bitmap texture of dimension %d", Dimension);
    } else if (v.IsNumber()) {
        return std::make_shared<ConstantTextureA>(JsonUtils::as<float>(v));
    } else if (v.IsArray()) {
        FAIL("Cannot instantiate scalar value from vector valued argument");
    } else if (v.IsObject()) {
        return instantiateScalarTexture<Dimension>(JsonUtils::as<std::string>(v, "type"), v);
    } else {
        FAIL("Cannot instantiate texture from unknown value type");
    }
    return nullptr;
}

template<int Dimension>
std::shared_ptr<Texture<false, Dimension>> Scene::fetchColorTexture(const rapidjson::Value &v) const
{
    if (v.IsString()) {
        if (Dimension == 2)
            return fetchColorMap(v.GetString());
        else
            FAIL("Cannot instantiate bitmap texture of dimension %d", Dimension);
    } else if (v.IsNumber()) {
        return std::make_shared<ConstantTextureRgb>(Vec3f(JsonUtils::as<float>(v)));
    } else if (v.IsArray()) {
        return std::make_shared<ConstantTextureRgb>(JsonUtils::as<Vec3f>(v));
    } else if (v.IsObject()) {
        return instantiateColorTexture<Dimension>(JsonUtils::as<std::string>(v, "type"), v);
    } else {
        FAIL("Cannot instantiate texture from unknown value type");
    }
    return nullptr;
}

template std::shared_ptr<Texture<true, 2>> Scene::fetchScalarTexture(const rapidjson::Value &) const;
template std::shared_ptr<Texture<false, 2>> Scene::fetchColorTexture(const rapidjson::Value &) const;

std::shared_ptr<TextureRgb> Scene::fetchColorMap(const std::string &path) const
{
    if (_colorMaps.count(path))
        return _colorMaps[path];

    std::shared_ptr<BitmapTextureRgb> tex(BitmapTextureUtils::loadColorTexture(path));
    _colorMaps[path] = tex;
    return tex;
}

std::shared_ptr<TextureA> Scene::fetchScalarMap(const std::string &path) const
{
    if (_scalarMaps.count(path))
        return _scalarMaps[path];

    std::shared_ptr<BitmapTextureA> tex(BitmapTextureUtils::loadScalarTexture(path));
    _scalarMaps[path] = tex;
    return tex;
}

const Primitive *Scene::findPrimitive(const std::string &name) const
{
    for (const std::shared_ptr<Primitive> &m : _primitives)
        if (m->name() == name)
            return m.get();
    return nullptr;
}

template<typename T>
bool Scene::addUnique(const std::shared_ptr<T> &o, std::vector<std::shared_ptr<T>> &list)
{
    bool retry = false;
    int dupeCount = 0;
    std::string baseName = o->name();
    if (baseName.empty())
        return true;
    for (int i = 1; !baseName.empty() && isdigit(baseName.back()); i *= 10, baseName.pop_back())
        dupeCount += i*(baseName.back() - '0');
    std::string newName = o->name();
    do {
        retry = false;
        for (const std::shared_ptr<T> &m : list) {
            if (m.get() == o.get())
                return false;
            if (m->name() == newName) {
                newName = tfm::format("%s%d", baseName, ++dupeCount);
                retry = true;
                break;
            }
        }
    } while (retry);

    o->setName(newName);
    list.push_back(o);

    return true;
}

template<typename T1, typename T2>
void Scene::addTexture(std::shared_ptr<T1> &t, std::map<std::string, std::shared_ptr<T2>> &maps)
{
    std::shared_ptr<T2> downCast = std::dynamic_pointer_cast<T2>(t);
    if (!downCast)
        return;
    if (maps.count(downCast->path())) {
        if (maps[downCast->path()]->fullPath() == downCast->fullPath()) {
            t = maps[downCast->path()];
        } else {
            int i = 1;
            std::string newPath = downCast->path();
            while (maps.count(newPath))
                newPath = tfm::format("%s%d.%s", FileUtils::stripExt(downCast->path()), i++, FileUtils::extractExt(downCast->path()));
            downCast->setPath(newPath);
        }
    }

    maps.insert(std::make_pair(downCast->path(), downCast));
}

void Scene::addPrimitive(const std::shared_ptr<Primitive> &mesh)
{
    if (addUnique(mesh, _primitives))
        addBsdf(mesh->bsdf());
}

void Scene::addBsdf(const std::shared_ptr<Bsdf> &bsdf)
{
    if (addUnique(bsdf, _bsdfs)) {
        if (bsdf->intMedium())
            addUnique(bsdf->intMedium(), _media);
        if (bsdf->extMedium())
            addUnique(bsdf->extMedium(), _media);
        addTexture(bsdf->alpha(), _scalarMaps);
        addTexture(bsdf->bump(), _scalarMaps);
        addTexture(bsdf->color(), _colorMaps);
    }
}

void Scene::merge(Scene scene)
{
    for (std::shared_ptr<Primitive> &m : scene._primitives)
        addPrimitive(m);
}

Scene::Scene()
: _integrator(std::make_shared<PathTraceIntegrator>())
{
}

Scene::Scene(const std::string &srcDir)
: _srcDir(srcDir),
  _integrator(std::make_shared<PathTraceIntegrator>())
{
}

void Scene::fromJson(const rapidjson::Value &v, const Scene &scene)
{
    JsonSerializable::fromJson(v, scene);

    using namespace std::placeholders;

    const rapidjson::Value::Member *primitives = v.FindMember("primitives");
    const rapidjson::Value::Member *media      = v.FindMember("media");
    const rapidjson::Value::Member *bsdfs      = v.FindMember("bsdfs");
    const rapidjson::Value::Member *camera     = v.FindMember("camera");
    const rapidjson::Value::Member *integrator = v.FindMember("integrator");
    const rapidjson::Value::Member *renderer   = v.FindMember("renderer");

    SOFT_ASSERT(primitives != nullptr, "Scene file must contain 'primitives' array");
    SOFT_ASSERT(bsdfs      != nullptr, "Scene file must contain 'bsdfs' array");
    SOFT_ASSERT(camera     != nullptr && camera->value.IsObject(), "Scene file must contain 'camera' object");

    if (media)
        loadObjectList( media->value, std::bind(&Scene::instantiateMedium,    this, _1, _2), _media);
    loadObjectList(     bsdfs->value, std::bind(&Scene::instantiateBsdf,      this, _1, _2), _bsdfs);
    loadObjectList(primitives->value, std::bind(&Scene::instantiatePrimitive, this, _1, _2), _primitives);

    _camera = instantiateCamera(JsonUtils::as<std::string>(camera->value, "type"), camera->value);

    if (integrator)
        _integrator = instantiateIntegrator(JsonUtils::as<std::string>(integrator->value, "type"), integrator->value);
    if (renderer)
        _rendererSettings.fromJson(renderer->value, *this);
}

Scene::Scene(const std::string &srcDir,
      std::vector<std::shared_ptr<Primitive>> primitives,
      std::vector<std::shared_ptr<Bsdf>> bsdfs,
      std::shared_ptr<Camera> camera)
: _srcDir(srcDir),
  _primitives(std::move(primitives)),
  _bsdfs(std::move(bsdfs)),
  _camera(camera),
  _integrator(std::make_shared<PathTraceIntegrator>())
{
}

rapidjson::Value Scene::toJson(Allocator &allocator) const
{
    rapidjson::Value v = JsonSerializable::toJson(allocator);

    rapidjson::Value primitives(rapidjson::kArrayType);
    for (const std::shared_ptr<Primitive> &t : _primitives)
        primitives.PushBack(t->toJson(allocator), allocator);

    rapidjson::Value media(rapidjson::kArrayType);
    for (const std::shared_ptr<Medium> &b : _media)
        media.PushBack(b->toJson(allocator), allocator);

    rapidjson::Value bsdfs(rapidjson::kArrayType);
    for (const std::shared_ptr<Bsdf> &b : _bsdfs)
        bsdfs.PushBack(b->toJson(allocator), allocator);

    v.AddMember("media", media, allocator);
    v.AddMember("bsdfs", bsdfs, allocator);
    v.AddMember("primitives", primitives, allocator);
    v.AddMember("camera", _camera->toJson(allocator), allocator);
    v.AddMember("integrator", _integrator->toJson(allocator), allocator);
    v.AddMember("renderer", _rendererSettings.toJson(allocator), allocator);

    return std::move(v);
}

void Scene::saveData(const std::string &dst) const
{
    std::string previousDir = FileUtils::getCurrentDir();
    FileUtils::changeCurrentDir(dst);

    for (const std::shared_ptr<Primitive> &m : _primitives)
        m->saveData();

    for (const auto &p : _colorMaps)
        p.second->saveData();
    for (const auto &p : _scalarMaps)
        p.second->saveData();

    FileUtils::changeCurrentDir(previousDir);
}

void Scene::deletePrimitives(const std::unordered_set<Primitive *> &primitives)
{
    std::vector<std::shared_ptr<Primitive>> newPrims;
    newPrims.reserve(_primitives.size());

    for (std::shared_ptr<Primitive> &m : _primitives)
        if (!primitives.count(m.get()))
            newPrims.push_back(m);

    _primitives = std::move(newPrims);
}

TraceableScene *Scene::makeTraceable()
{
    return new TraceableScene(*_camera, *_integrator, _primitives, _media, _rendererSettings);
}

Scene *Scene::load(const std::string &path)
{
    std::string json = FileUtils::loadText(path.c_str());

    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    if (document.HasParseError()) {
        LOG("Scene::load", WARN, "JSON parse error: %s", document.GetParseError());
        return nullptr;
    }

    std::string previousDir = FileUtils::getCurrentDir();
    FileUtils::changeCurrentDir(FileUtils::extractDir(std::string(path)));

    Scene *scene = new Scene(FileUtils::extractDir(path));
    scene->fromJson(document, *scene);

    FileUtils::changeCurrentDir(previousDir);

    scene->setPath(path);

    return scene;
}

void Scene::save(const std::string &path, const Scene &scene, bool includeData)
{
    rapidjson::Document document;
    document.SetObject();

    *(static_cast<rapidjson::Value *>(&document)) = scene.toJson(document.GetAllocator());

    char buffer[4*1024];
    FILE *fp = fopen(path.c_str(), "wb");
    rapidjson::FileWriteStream out(fp, buffer, sizeof(buffer));

    rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(out);
    document.Accept(writer);
    fclose(fp);

    if (includeData)
        scene.saveData(FileUtils::extractDir(path));
}

}
