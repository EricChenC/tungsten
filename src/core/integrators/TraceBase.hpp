#ifndef TRACEBASE_HPP_
#define TRACEBASE_HPP_

#include "TraceSettings.hpp"

#include "samplerecords/SurfaceScatterEvent.hpp"
#include "samplerecords/VolumeScatterEvent.hpp"
#include "samplerecords/LightSample.hpp"

#include "sampling/PathSampleGenerator.hpp"
#include "sampling/UniformSampler.hpp"
#include "sampling/Distribution1D.hpp"
#include "sampling/SampleWarp.hpp"

#include "renderer/TraceableScene.hpp"

#include "cameras/Camera.hpp"

#include "volume/Medium.hpp"

#include "math/TangentFrame.hpp"
#include "math/MathUtil.hpp"
#include "math/Angle.hpp"

#include "bsdfs/Bsdf.hpp"

#include <vector>
#include <memory>
#include <cmath>

namespace Tungsten {

class TraceBase
{
protected:
    const TraceableScene *_scene;
    TraceSettings _settings;
    uint32 _threadId;

    // For computing direct lighting probabilities
    std::vector<float> _lightPdf;
    // For sampling light sources in adjoint light tracing
    std::unique_ptr<Distribution1D> _lightSampler;

    TraceBase(TraceableScene *scene, const TraceSettings &settings, uint32 threadId);

    bool isConsistent(const SurfaceScatterEvent &event, const Vec3f &w) const;

    Vec3f generalizedShadowRay(Ray &ray,
                               const Medium *medium,
                               const Primitive *endCap,
                               int bounce);

    Vec3f attenuatedEmission(const Primitive &light,
                             const Medium *medium,
                             float expectedDist,
                             IntersectionTemporary &data,
                             IntersectionInfo &info,
                             int bounce,
                             Ray &ray);

    bool lensSample(const Camera &camera,
                    SurfaceScatterEvent &event,
                    const Medium *medium,
                    int bounce,
                    const Ray &parentRay,
                    Vec3f &weight,
                    Vec2f &pixel);

    Vec3f lightSample(const Primitive &light,
                      SurfaceScatterEvent &event,
                      const Medium *medium,
                      int bounce,
                      const Ray &parentRay);

    Vec3f bsdfSample(const Primitive &light,
                     SurfaceScatterEvent &event,
                     const Medium *medium,
                     int bounce,
                     const Ray &parentRay);

    Vec3f volumeLightSample(VolumeScatterEvent &event,
                        const Primitive &light,
                        const Medium *medium,
                        bool performMis,
                        int bounce,
                        const Ray &parentRay);

    Vec3f volumePhaseSample(const Primitive &light,
                        VolumeScatterEvent &event,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    Vec3f sampleDirect(const Primitive &light,
                       SurfaceScatterEvent &event,
                       const Medium *medium,
                       int bounce,
                       const Ray &parentRay);

    Vec3f volumeSampleDirect(const Primitive &light,
                        VolumeScatterEvent &event,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    const Primitive *chooseLight(PathSampleGenerator &sampler, const Vec3f &p, float &weight);
    const Primitive *chooseLightAdjoint(PathSampleGenerator &sampler, float &pdf);

    Vec3f volumeEstimateDirect(VolumeScatterEvent &event,
                        const Medium *medium,
                        int bounce,
                        const Ray &parentRay);

    Vec3f estimateDirect(SurfaceScatterEvent &event,
                         const Medium *medium,
                         int bounce,
                         const Ray &parentRay);

public:
    SurfaceScatterEvent makeLocalScatterEvent(IntersectionTemporary &data, IntersectionInfo &info,
            Ray &ray, PathSampleGenerator *sampler) const;

    bool handleVolume(PathSampleGenerator &sampler, const Medium *&medium, int bounce,
               bool adjoint, bool enableLightSampling, Ray &ray,
               Vec3f &throughput, Vec3f &emission, bool &wasSpecular, bool &hitSurface,
               Medium::MediumState &state);

    bool handleSurface(SurfaceScatterEvent &event, IntersectionTemporary &data,
                       IntersectionInfo &info, PathSampleGenerator &sampler, const Medium *&medium,
                       int bounce, bool adjoint, bool enableLightSampling, Ray &ray,
                       Vec3f &throughput, Vec3f &emission, bool &wasSpecular,
                       Medium::MediumState &state);
};

}

#endif /* TRACEBASE_HPP_ */
