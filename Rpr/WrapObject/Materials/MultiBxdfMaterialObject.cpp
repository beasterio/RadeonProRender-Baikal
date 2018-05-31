/**********************************************************************
Copyright (c) 2016 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
********************************************************************/
#include "MultiBxdfMaterialObject.h"
#include "TextureMaterialObject.h"
#include "WrapObject/Exception.h"
#include "SceneGraph/material.h"
#include <iostream>

using namespace Baikal;
using namespace RadeonRays;

const std::map<std::string, std::string> kInputNamesDictionary = {
    { "color0", "base_material" },
    { "color1", "top_material" },};

MultiBxdfMaterialObject::MultiBxdfMaterialObject(MaterialObject::Type mat_type, Baikal::MultiBxdf::Type type)
    : MaterialObject(mat_type)
{
    m_mat = MultiBxdf::Create(type);
    m_mat->SetInputValue("weight", { 0.1f, 0.1f, 0.1f, 0.1f });
    m_mat->SetInputValue("ior", { 1.0f, 1.0f, 1.0f, 1.0f });
    m_mat->SetInputValue("base_material", SingleBxdf::Create(SingleBxdf::BxdfType::kLambert));
    m_mat->SetInputValue("top_material", SingleBxdf::Create(SingleBxdf::BxdfType::kLambert));
    m_mat->SetThin(false);
}

void MultiBxdfMaterialObject::SetInputMaterial(const std::string& input_name, MaterialObject* input)
{
    if (!input->GetMaterial())
    {
        std::cout << "input empty" << std::endl;
    }
    //handle blend material case
    if (GetType() == kBlend && input_name == "weight")
    {
        auto input_type = input->GetType();
        //expected only fresnel materials
        if (input_type == Type::kFresnel && input_type == Type::kFresnelShlick)
        {
            auto blend_mat = std::dynamic_pointer_cast<MultiBxdf>(m_mat);
            blend_mat->SetType(MultiBxdf::Type::kFresnelBlend);
            blend_mat->SetInputValue("ior", input->GetMaterial()->GetInputValue("ior").float_value);
        }
    }
    else
    {
        //translate name
        std::string translated_name = input_name;
        auto it = kInputNamesDictionary.find(input_name);
        if (it != kInputNamesDictionary.end())
        {
            translated_name = it->second;
        }
        m_mat->SetInputValue(translated_name, input->GetMaterial());
    }
}

void MultiBxdfMaterialObject::SetInputTexture(const std::string& input_name, TextureMaterialObject* input)
{
    //base_material and top_material use only Baikal::Materiakl as input
    //so add single bxdf for texture input
    if (input_name == "base_material" || input_name == "top_material")
    {
        Material::Ptr albedo = SingleBxdf::Create(SingleBxdf::BxdfType::kLambert);
        albedo->SetInputValue("albedo", input->GetTexture());
        m_mat->SetInputValue(input_name, albedo);
    }
    else
    {
        m_mat->SetInputValue(input_name, input->GetTexture());
    }

    //handle blend material case
    if (GetType() == kBlend && input_name == "weight")
    {
        auto blend_mat = std::dynamic_pointer_cast<MultiBxdf>(m_mat);
        blend_mat->SetType(MultiBxdf::Type::kMix);
    }
}


void MultiBxdfMaterialObject::SetInputF(const std::string& input_name, const RadeonRays::float4& val)
{
    //base_material and top_material use only Baikal::Materiakl as input
    //so add single bxdf for float input
    if (input_name == "base_material" || input_name == "top_material")
    {
        Material::Ptr albedo = SingleBxdf::Create(SingleBxdf::BxdfType::kLambert);
        albedo->SetInputValue("albedo", val);
        m_mat->SetInputValue(input_name, albedo);
    }
    else
    {
        m_mat->SetInputValue(input_name, val);
    }

    //handle blend material case
    if (GetType() == kBlend && input_name == "weight")
    {
        auto blend_mat = std::dynamic_pointer_cast<MultiBxdf>(m_mat);
        blend_mat->SetType(MultiBxdf::Type::kMix);
    }
}

void MultiBxdfMaterialObject::Update(MaterialObject* mat)
{
    const std::string& input_name = GetInputTranslatedName(mat);
    if (GetType() == kBlend && input_name == "weight")
    {
        auto input_type = mat->GetType();
        //expected only fresnel materials
        if (input_type == Type::kFresnel || input_type == Type::kFresnelShlick)
        {
            //need to get SingleBxdf::BxdfType::kTranslucent for valid ior value
            auto fresnel_mat = mat->GetMaterial()->GetInputValue("base_material").mat_value;
            auto blend_mat = std::dynamic_pointer_cast<MultiBxdf>(m_mat);
            blend_mat->SetType(MultiBxdf::Type::kFresnelBlend);
            blend_mat->SetInputValue("ior", fresnel_mat->GetInputValue("ior").float_value);
        }
    }
}

Baikal::Material::Ptr MultiBxdfMaterialObject::GetMaterial()
{
    return m_mat;
}
