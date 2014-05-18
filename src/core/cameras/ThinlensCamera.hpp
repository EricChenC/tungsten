#ifndef THINLENSCAMERA_HPP_
#define THINLENSCAMERA_HPP_

#include <cmath>

#include "materials/DiskTexture.hpp"

#include "math/Angle.hpp"

#include "io/Scene.hpp"

#include "Camera.hpp"

namespace Tungsten
{

class Scene;

class ThinlensCamera : public Camera
{
    float _fovDeg;
    float _fovRad;
    float _planeDist;
    float _focusDist;
    float _apertureSize;
    float _chromaticAberration;
    float _catEye;

    std::shared_ptr<TextureA> _aperture;

    void precompute()
    {
        _fovRad = Angle::degToRad(_fovDeg);
        _planeDist = 1.0f/std::tan(_fovRad*0.5f);
        _aperture->makeSamplable(MAP_UNIFORM);
    }

public:
    ThinlensCamera()
    : Camera(),
      _fovDeg(60.0f),
      _focusDist(1.0f),
      _apertureSize(0.001f),
      _chromaticAberration(0.0f),
      _catEye(0.0f),
      _aperture(std::make_shared<DiskTexture<true>>())
    {
        precompute();
    }

    void fromJson(const rapidjson::Value &v, const Scene &scene) override
    {
        Camera::fromJson(v, scene);
        JsonUtils::fromJson(v, "fov", _fovDeg);
        JsonUtils::fromJson(v, "focus_distance", _focusDist);
        JsonUtils::fromJson(v, "aperture_size", _apertureSize);
        JsonUtils::fromJson(v, "aberration", _chromaticAberration);
        JsonUtils::fromJson(v, "cateye", _catEye);

        const rapidjson::Value::Member *aperture = v.FindMember("aperture");
        if (aperture)
            _aperture = scene.fetchScalarTexture<2>(aperture->value);

        precompute();
    }

    virtual rapidjson::Value toJson(Allocator &allocator) const override
    {
        rapidjson::Value v = Camera::toJson(allocator);
        v.AddMember("type", "thinlens", allocator);
        v.AddMember("fov", _fovDeg, allocator);
        v.AddMember("focus_distance", _focusDist, allocator);
        v.AddMember("aperture_size", _apertureSize, allocator);
        v.AddMember("aberration", _chromaticAberration, allocator);
        v.AddMember("cateye", _catEye, allocator);
        JsonUtils::addObjectMember(v, "aperture", *_aperture, allocator);
        return std::move(v);
    }

    float evalApertureThroughput(Vec3f planePos, Vec2f aperturePos) const
    {
        float aperture = (*_aperture)[aperturePos];

        if (_catEye > 0.0f) {
            aperturePos = (aperturePos*2.0f - 1.0f)*_apertureSize;
            Vec3f lensPos = Vec3f(aperturePos.x(), aperturePos.y(), 0.0f);
            Vec3f localDir = (planePos - lensPos).normalized();
            Vec2f diaphragmPos = lensPos.xy() - _catEye*_planeDist*localDir.xy();
            if (diaphragmPos.lengthSq() > sqr(_apertureSize))
                return 0.0f;
        }
        return aperture/_aperture->maximum();
    }

    Vec3f aberration(const Vec3f &planePos, Vec2u pixel, Vec2f &aperturePos, SampleGenerator &sampler) const
    {
        Vec2f shift = (Vec2f(Vec2i(pixel) - Vec2i(_res/2))/Vec2f(_res))*2.0f;
        shift.y() = -shift.y();

        float dist = shift.length();
        shift.normalize();
        float shiftAmount = dist*_chromaticAberration;
        Vec3f shiftAmounts(shiftAmount, 0.0f, -shiftAmount);
        int sampleKernel = sampler.nextI() % 3;
        float amount = shiftAmounts[sampleKernel];
        shiftAmounts -= amount;

        Vec2f blueShift  = aperturePos + shift*shiftAmounts.x();
        Vec2f greenShift = aperturePos + shift*shiftAmounts.y();
        Vec2f redShift   = aperturePos + shift*shiftAmounts.z();

        aperturePos -= amount*shift;

        return Vec3f(
            evalApertureThroughput(planePos, redShift),
            evalApertureThroughput(planePos, greenShift),
            evalApertureThroughput(planePos, blueShift)
        );
    }

    bool generateSample(Vec2u pixel, SampleGenerator &sampler, Vec3f &throughput, Ray &ray) const override final
    {
        Vec2f pixelUv = sampler.next2D();
        Vec2f lensUv = sampler.next2D();

        Vec3f planePos(
            -1.0f  + (float(pixel.x()) + pixelUv.x())*_pixelSize.x(),
            _ratio - (float(pixel.y()) + pixelUv.y())*_pixelSize.y(),
            _planeDist
        );
        planePos *= _focusDist/planePos.z();

        Vec2f aperturePos = _aperture->sample(lensUv);
        if (_chromaticAberration > 0.0f)
            throughput = aberration(planePos, pixel, aperturePos, sampler);
        else
            throughput = Vec3f(1.0f);
        aperturePos = (aperturePos*2.0f - 1.0f)*_apertureSize;

        Vec3f lensPos = Vec3f(aperturePos.x(), aperturePos.y(), 0.0f);
        Vec3f localDir = (planePos - lensPos).normalized();
        Vec3f dir = _transform.transformVector(localDir);

        if (_catEye > 0.0f && _chromaticAberration <= 0.0f) {
            Vec2f diaphragmPos = lensPos.xy() - _catEye*_planeDist*localDir.xy();
            if (diaphragmPos.lengthSq() > sqr(_apertureSize))
                return false;
        }

        ray = Ray(_transform*lensPos, dir);

        return true;
    }

    Mat4f approximateProjectionMatrix(int width, int height) const override final
    {
        return Mat4f::perspective(_fovDeg, float(width)/float(height), 1e-2f, 100.0f);
    }

    float approximateFov() const override final
    {
        return _fovRad;
    }

    float fovDeg() const
    {
        return _fovDeg;
    }

    float apertureSize() const
    {
        return _apertureSize;
    }

    float focusDist() const
    {
        return _focusDist;
    }
};

}

#endif /* THINLENSCAMERA_HPP_ */
