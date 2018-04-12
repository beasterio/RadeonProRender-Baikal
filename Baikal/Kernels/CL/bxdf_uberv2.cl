#ifndef BXDF_UBERV2_CL
#define BXDF_UBERV2_CL

#include <inputmaps.cl>

typedef struct _UberV2ShaderData
{
    float4 diffuse_color;
    float4 reflection_color;
    float4 coating_color;
    float4 refraction_color;
    float4 emission_color;
    float4 sss_absorption_color;
    float4 sss_scatter_color;
    float4 sss_subsurface_color;
    float4 shading_normal;

    float reflection_roughness;
    float reflection_anisotropy;
    float reflection_anisotropy_rotation;
    float reflection_ior;

    float reflection_metalness;
    float coating_ior;
    float refraction_roughness;
    float refraction_ior;

    float transparency;
    float sss_absorption_distance;
    float sss_scatter_distance;
    float sss_scatter_direction;

} UberV2ShaderData;

//Temorary code. Will be moved to generator later with UberV2 redesign
UberV2ShaderData UberV2PrepareInputs(
    // Geometry
    DifferentialGeometry const* dg,
    // Values for input maps
    GLOBAL InputMapData const* restrict input_map_values,
    // Texture args
    TEXTURE_ARG_LIST
)
{
    UberV2ShaderData shader_data;

    shader_data.diffuse_color = GetInputMapFloat4(dg->mat.uberv2.diffuse_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.reflection_color = GetInputMapFloat4(dg->mat.uberv2.reflection_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.coating_color = GetInputMapFloat4(dg->mat.uberv2.coating_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.refraction_color = GetInputMapFloat4(dg->mat.uberv2.refraction_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.emission_color = GetInputMapFloat4(dg->mat.uberv2.emission_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_absorption_color = GetInputMapFloat4(dg->mat.uberv2.sss_absorption_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_scatter_color = GetInputMapFloat4(dg->mat.uberv2.sss_scatter_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_subsurface_color = GetInputMapFloat4(dg->mat.uberv2.sss_subsurface_color_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.shading_normal = GetInputMapFloat4(dg->mat.uberv2.shading_normal_input_id, dg, input_map_values, TEXTURE_ARGS);

    shader_data.reflection_roughness = GetInputMapFloat(dg->mat.uberv2.reflection_roughness_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.reflection_anisotropy = GetInputMapFloat(dg->mat.uberv2.reflection_anisotropy_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.reflection_anisotropy_rotation = GetInputMapFloat(dg->mat.uberv2.reflection_anisotropy_rotation_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.reflection_ior = GetInputMapFloat(dg->mat.uberv2.reflection_ior_input_id, dg, input_map_values, TEXTURE_ARGS);

    shader_data.reflection_metalness = GetInputMapFloat(dg->mat.uberv2.reflection_metalness_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.coating_ior = GetInputMapFloat(dg->mat.uberv2.coating_ior_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.refraction_roughness = GetInputMapFloat(dg->mat.uberv2.refraction_roughness_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.refraction_ior = GetInputMapFloat(dg->mat.uberv2.refraction_ior_input_id, dg, input_map_values, TEXTURE_ARGS);

    shader_data.transparency = GetInputMapFloat(dg->mat.uberv2.transparency_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_absorption_distance = GetInputMapFloat(dg->mat.uberv2.sss_absorption_distance_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_scatter_distance = GetInputMapFloat(dg->mat.uberv2.sss_scatter_distance_input_id, dg, input_map_values, TEXTURE_ARGS);
    shader_data.sss_scatter_direction = GetInputMapFloat(dg->mat.uberv2.sss_scatter_direction_input_id, dg, input_map_values, TEXTURE_ARGS);
    return shader_data;
}

#include <../Baikal/Kernels/CL/bxdf_uberv2_bricks.cl>

/// Calculates Fresnel for provided parameters. Swaps IORs if needed
float CalculateFresnel(
    // IORs
    float top_ior,
    float bottom_ior,
    // Angle between normal and incoming ray
    float ndotwi
)
{
    float etai =  top_ior;
    float etat =  bottom_ior;
    float cosi = ndotwi;

    // Revert normal and eta if needed
    if (cosi < 0.f)
    {
        float tmp = etai;
        etai = etat;
        etat = tmp;
        cosi = -cosi;
    }

    float eta = etai / etat;
    float sini2 = 1.f - cosi * cosi;
    float sint2 = eta * eta * sini2;
    float fresnel = 1.f;

    if (sint2 < 1.f)
    {
        float cost = native_sqrt(max(0.f, 1.f - sint2));
        fresnel = FresnelDielectric(etai, etat, cosi, cost);
    }

    return fresnel;
}

/// Fills BxDF flags structure
void GetMaterialBxDFType(
    // Incoming direction
    float3 wi,
    // Sampler
    Sampler* sampler,
    // Sampler args
    SAMPLER_ARG_LIST,
    // Geometry
    DifferentialGeometry* dg,
    // Shader data
    UberV2ShaderData const* shader_data
)
{
    int bxdf_flags = 0;
    if ((dg->mat.uberv2.layers & kEmissionLayer) == kEmissionLayer) // Emissive flag
    {
        bxdf_flags = kBxdfFlagsEmissive;
    }

    /// Set transparency flag if we have transparency layer and plan to sample it
    if ((dg->mat.uberv2.layers & kTransparencyLayer) == kTransparencyLayer)
    {
        float sample = Sampler_Sample1D(sampler, SAMPLER_ARGS);
        if (sample < shader_data->transparency)
        {
            bxdf_flags |= (kBxdfFlagsTransparency | kBxdfFlagsSingular);
            Bxdf_SetFlags(dg, bxdf_flags);
            Bxdf_UberV2_SetSampledComponent(dg, kBxdfUberV2SampleTransparency);
            return;
        }
    }

    const int bxdf_type = (dg->mat.uberv2.layers & (kCoatingLayer | kReflectionLayer | kRefractionLayer | kDiffuseLayer));
    const float ndotwi = dot(dg->n, wi);

    /// Check refraction layer. If we have it and plan to sample it - set flags and sampled component
    if ((bxdf_type & kRefractionLayer) == kRefractionLayer)
    {
        float sample = Sampler_Sample1D(sampler, SAMPLER_ARGS);
        const float fresnel = CalculateFresnel(1.0f, shader_data->refraction_ior, ndotwi);
        if (sample >= fresnel)
        {
            Bxdf_UberV2_SetSampledComponent(dg, kBxdfUberV2SampleRefraction);
            if (shader_data->refraction_roughness < ROUGHNESS_EPS)
            {
                bxdf_flags |= kBxdfFlagsSingular;
            }
            Bxdf_SetFlags(dg, bxdf_flags);
            return;
        }
    }

    // At this point we have only BRDFs left
    bxdf_flags |= kBxdfFlagsBrdf;

    // Vacuum IOR. Will be replaced by top layer IOR each time we go to nex layer
    float top_ior = 1.0f;
    // Check coating layer
    if ((bxdf_type & kCoatingLayer) == kCoatingLayer)
    {
        float sample = Sampler_Sample1D(sampler, SAMPLER_ARGS);
        const float fresnel = CalculateFresnel(top_ior, shader_data->coating_ior, ndotwi);
        if (sample < fresnel) // Will sample coating layer
        {
            bxdf_flags |= kBxdfFlagsSingular; // Coating always singular
            Bxdf_SetFlags(dg, bxdf_flags);

            Bxdf_UberV2_SetSampledComponent(dg, kBxdfUberV2SampleCoating);
            return;
        }
        top_ior = shader_data->coating_ior;
    }

    // Check reflection layer
    if ((bxdf_type & kReflectionLayer) == kReflectionLayer)
    {
        const float fresnel = CalculateFresnel(top_ior, shader_data->reflection_ior, ndotwi);
        float sample = Sampler_Sample1D(sampler, SAMPLER_ARGS);
        if (sample < fresnel) // Will sample reflection layer
        {
            if (shader_data->reflection_roughness < ROUGHNESS_EPS)
            {
                bxdf_flags |= kBxdfFlagsSingular;
            }

            Bxdf_UberV2_SetSampledComponent(dg, kBxdfUberV2SampleReflection);
            Bxdf_SetFlags(dg, bxdf_flags);
            return;
        }
    }

    // Sample diffuse
    Bxdf_UberV2_SetSampledComponent(dg, kBxdfUberV2SampleDiffuse);
    Bxdf_SetFlags(dg, bxdf_flags);
    return;
}

// Calucates Fresnel blend for two float3 values.
// F(top_ior, bottom_ior) * top_value + (1 - F(top_ior, bottom_ior) * bottom_value)
float3 Fresnel_Blend(
    // IORs
    float top_ior,
    float bottom_ior,
    // Values to blend
    float3 top_value,
    float3 bottom_value,
    // Incoming direction
    float3 wi
)
{
    float fresnel = CalculateFresnel(top_ior, bottom_ior, wi.y);
    return fresnel * top_value + (1.f - fresnel) * bottom_value;
}

// Calucates Fresnel blend for two float values.
// F(top_ior, bottom_ior) * top_value + (1 - F(top_ior, bottom_ior) * bottom_value)
float Fresnel_Blend_F(
    // IORs
    float top_ior,
    float bottom_ior,
    // Values to blend
    float top_value,
    float bottom_value,
    // Incoming direction
    float3 wi
)
{
    float fresnel = CalculateFresnel(top_ior, bottom_ior, wi.y);
    return fresnel * top_value + (1.f - fresnel) * bottom_value;
}

/// Calculates fresnel blend for all active layers
/// If material have single layer - directly return it
/// If we have mix - calculate following result:
/// result = mix(BxDF, 0.0f, transparency) where
/// BxDF = F(1.0, refraction_ior) * BRDF + (1.0f - F(1.0, refraction_ior)) * refraction
/// BRDF = F(1.0, coating_ior) * coating + (1.0f - F(1.0f, coating_ior) *
/// (F(coating_ior, reflection_ior) * reflection + (1.0f - F(coating_ior, reflection_ior)) * diffuse)
float3 UberV2_Evaluate(
    // Geometry
    DifferentialGeometry const* dg,
    // Incoming direction
    float3 wi,
    // Outgoing direction
    float3 wo,
    // Texture args
    TEXTURE_ARG_LIST,
    // Shader data
    UberV2ShaderData const* shader_data
)
{
    int layers = dg->mat.uberv2.layers;

    int fresnel_blend_layers = popcount(layers & (kCoatingLayer | kReflectionLayer | kDiffuseLayer | kRefractionLayer));
    int brdf_layers = popcount(layers & (kCoatingLayer | kReflectionLayer | kDiffuseLayer));

    if (fresnel_blend_layers == 0) return 0.0f;
    float3 result;
    // Check if we have single layer material or not
    if (fresnel_blend_layers == 1)
    {
        if ((layers & kCoatingLayer) == kCoatingLayer)
        {
            result = UberV2_IdealReflect_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kReflectionLayer) == kReflectionLayer)
        {
            result = UberV2_Reflection_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kRefractionLayer) == kRefractionLayer)
        {
            result = UberV2_Refraction_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kDiffuseLayer) == kDiffuseLayer)
        {
            result = UberV2_Lambert_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        }

        // Apply transparency if any
        if ((layers & kTransparencyLayer) == kTransparencyLayer)
        {
            result = mix(result, 0.f, shader_data->transparency);
        }

        return result;
    }

    // Collect IORs and values from all layers
    // We will calculate final Fresnel blend using it.
    // If we have all layers arrays will look like this:
    // iors = {1.0, CoatingIOR, ReflectionIor}
    // values = {coating, reflection, diffuse}
    float iors[3] = { 1.0f, 1.0f, 1.0f };
    float3 values[3];
    int id = 0;

    if ((layers & kCoatingLayer) == kCoatingLayer)
    {
        iors[id + 1] = shader_data->coating_ior;
        values[id] = UberV2_IdealReflect_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    if ((layers & kReflectionLayer) == kReflectionLayer)
    {
        iors[id + 1] = shader_data->reflection_ior;
        values[id] = UberV2_Reflection_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    if ((layers & kDiffuseLayer) == kDiffuseLayer)
    {
        values[id] = UberV2_Lambert_Evaluate(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    // Calculate BRDF part. Calculated in reverse order that will give us (if we have all layers)
    // result = F(1.0, coating_ior) * coating + (1.0f - F(1.0f, coating_ior) *
    // (F(coating_ior, reflection_ior) * reflection + (1.0f - F(coating_ior, reflection_ior)) * diffuse)
    result = values[brdf_layers - 1];
    for (int a = brdf_layers - 1; a > 0; --a)
    {
        result = Fresnel_Blend(iors[a - 1], iors[a], values[a - 1], result, wi);
    }

    // If we have refraction we need to blend it with previously calculated BRDF part using following formula:
    // F(1.0f, refraction_ior) * BRDF + (1.0f - F(1.0f, refraction_ior)) * refraction)
    if ((layers & kRefractionLayer) == kRefractionLayer)
    {
        result = Fresnel_Blend(1.0f, shader_data->refraction_ior, result, UberV2_Refraction_Evaluate(shader_data, wi, wo, TEXTURE_ARGS), wi);
    }

    // If we have transparency layer - we simply blend it with result
    if ((layers & kTransparencyLayer) == kTransparencyLayer)
    {
        result = mix(result, 0.f, shader_data->transparency);
        return 0.f;
    }


    return result;
}

/// Calculates fresnel blend for all active layers on PDF
/// If material have single layer - directly return it
/// If we have mix - calculate following result:
/// result = mix(BxDF, 0.0f, transparency) where
/// BxDF = F(1.0, refraction_ior) * BRDF + (1.0f - F(1.0, refraction_ior)) * refraction
/// BRDF = F(1.0, coating_ior) * coating + (1.0f - F(1.0f, coating_ior) *
/// (F(coating_ior, reflection_ior) * reflection + (1.0f - F(coating_ior, reflection_ior)) * diffuse)
float UberV2_GetPdf(
    // Geometry
    DifferentialGeometry const* dg,
    // Incoming direction
    float3 wi,
    // Outgoing direction
    float3 wo,
    // Texture args
    TEXTURE_ARG_LIST,
    // Shader data
    UberV2ShaderData const* shader_data
)
{
    const int layers = dg->mat.uberv2.layers;

    const int fresnel_blend_layers = popcount(layers & (kCoatingLayer | kReflectionLayer | kDiffuseLayer | kRefractionLayer));
    const int brdf_layers = popcount(layers & (kCoatingLayer | kReflectionLayer | kDiffuseLayer));

    if (fresnel_blend_layers == 0) return 0.0f;

    float result = 0.0f;

    //Check if we have single layer material or not
    if (fresnel_blend_layers == 1)
    {
        if ((layers & kCoatingLayer) == kCoatingLayer)
        {
            result = UberV2_IdealReflect_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kReflectionLayer) == kReflectionLayer)
        {
            result = UberV2_Reflection_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kRefractionLayer) == kRefractionLayer)
        {
            result = UberV2_Refraction_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        }
        else if ((layers & kDiffuseLayer) == kDiffuseLayer)
        {
            result = UberV2_Lambert_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        }

        // Apply transparency if any
        if ((layers & kTransparencyLayer) == kTransparencyLayer)
        {
            result = mix(result, 0.f, shader_data->transparency);
        }

        return result;
    }

    // Collect IORs and values from all layers
    // We will calculate final Fresnel blend using it.
    // If we have all layers arrays will look like this:
    // iors = {1.0, CoatingIOR, ReflectionIor}
    // values = {coating, reflection, diffuse}
    float iors[3] = { 1.0f, 1.0f, 1.0f };
    float values[3];
    int id = 0;

    if ((layers & kCoatingLayer) == kCoatingLayer)
    {
        iors[id + 1] = shader_data->coating_ior;
        values[id] = UberV2_IdealReflect_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    if ((layers & kReflectionLayer) == kReflectionLayer)
    {
        iors[id + 1] = shader_data->reflection_ior;
        values[id] = UberV2_Reflection_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    if ((layers & kDiffuseLayer) == kDiffuseLayer)
    {
        values[id] = UberV2_Lambert_GetPdf(shader_data, wi, wo, TEXTURE_ARGS);
        ++id;
    }

    // Calculate BRDF part. Calculated in reverse order that will give us (if we have all layers)
    // result = F(1.0, coating_ior) * coating + (1.0f - F(1.0f, coating_ior) *
    // (F(coating_ior, reflection_ior) * reflection + (1.0f - F(coating_ior, reflection_ior)) * diffuse)
    result = values[brdf_layers - 1];
    for (int a = brdf_layers - 1; a > 0; --a)
    {
        result = Fresnel_Blend_F(iors[a - 1], iors[a], values[a - 1], result, wi);
    }

    // If we have refraction we need to blend it with previously calculated BRDF part using following formula:
    // F(1.0f, refraction_ior) * BRDF + (1.0f - F(1.0f, refraction_ior)) * refraction)
    if ((layers & kRefractionLayer) == kRefractionLayer)
    {
        result = Fresnel_Blend_F(1.0f, shader_data->refraction_ior, result, UberV2_Refraction_GetPdf(shader_data, wi, wo, TEXTURE_ARGS), wi);
    }

    // If we have transparency layer - we simply blend it with result
    if ((layers & kTransparencyLayer) == kTransparencyLayer)
    {
        return 0.f;
        result = mix(result, 0.f, shader_data->transparency);
    }

    return result;
}

/// UberV2 sampling
/// Uses sampled component selected by GetMaterialBxDFType
float3 UberV2_Sample(
    // Geometry
    DifferentialGeometry const* dg,
    // Incoming direction
    float3 wi,
    // Texture args
    TEXTURE_ARG_LIST,
    // Sample
    float2 sample,
    // Outgoing  direction
    float3* wo,
    // PDF at wo
    float* pdf,
    UberV2ShaderData const* shader_data
)
{
    const int sampledComponent = Bxdf_UberV2_GetSampledComponent(dg);

    switch (sampledComponent)
    {
    case kBxdfUberV2SampleTransparency:
        return UberV2_Passthrough_Sample(shader_data, wi, TEXTURE_ARGS, sample, wo, pdf);
    case kBxdfUberV2SampleCoating:
        return UberV2_Coating_Sample(shader_data, wi, TEXTURE_ARGS, wo, pdf);
    case kBxdfUberV2SampleReflection:
        return UberV2_Reflection_Sample(shader_data, wi, TEXTURE_ARGS, sample, wo, pdf);
    case kBxdfUberV2SampleRefraction:
        return UberV2_Refraction_Sample(shader_data, wi, TEXTURE_ARGS, sample, wo, pdf);
    case kBxdfUberV2SampleDiffuse:
        return UberV2_Lambert_Sample(shader_data, wi, TEXTURE_ARGS, sample, wo, pdf);
    }

    return 0.f;
}

void UberV2_ApplyShadingNormal(
    // Geometry
    DifferentialGeometry* dg,
    // Prepared UberV2 shader inputs
    UberV2ShaderData const* shader_data
)
{
    const int layers = dg->mat.uberv2.layers;

    if ((layers & kShadingNormalLayer) == kShadingNormalLayer)
    {
        dg->n = normalize(shader_data->shading_normal.z * dg->n + shader_data->shading_normal.x * dg->dpdu + shader_data->shading_normal.y * dg->dpdv);
        dg->dpdv = normalize(cross(dg->n, dg->dpdu));
        dg->dpdu = normalize(cross(dg->dpdv, dg->n));
    }
}

#endif
