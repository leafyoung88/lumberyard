/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/
// Original file Copyright Crytek GMBH or its affiliates, used under license.

#include "StdAfx.h"
#include <AzCore/Serialization/SerializeContext.h>
#include <AzCore/Math/Transform.h>
#include <AzFramework/Math/MathUtils.h>

#include "CompoundSplineTrack.h"
#include "AnimSplineTrack.h"
#include "Maestro/Types/AnimParamType.h"
#include "Maestro/Types/AnimValueType.h"

CCompoundSplineTrack::CCompoundSplineTrack(int nDims, AnimValueType inValueType, CAnimParamType subTrackParamTypes[MAX_SUBTRACKS])
    : m_refCount(0)
{
    assert(nDims > 0 && nDims <= MAX_SUBTRACKS);
    m_node = nullptr;
    m_nDimensions = nDims;
    m_valueType = inValueType;

    m_nParamType = AnimParamType::Invalid;
    m_flags = 0;

    m_subTracks.resize(MAX_SUBTRACKS);
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i].reset(aznew C2DSplineTrack());
        m_subTracks[i]->SetParameterType(subTrackParamTypes[i]);

        if (inValueType == AnimValueType::RGB)
        {
            m_subTracks[i]->SetKeyValueRange(0.0f, 255.f);
        }
    }

    m_subTrackNames.resize(MAX_SUBTRACKS);
    m_subTrackNames[0] = "X";
    m_subTrackNames[1] = "Y";
    m_subTrackNames[2] = "Z";
    m_subTrackNames[3] = "W";

#ifdef MOVIESYSTEM_SUPPORT_EDITING
    m_bCustomColorSet = false;
#endif
}

//////////////////////////////////////////////////////////////////////////
// Need default constructor for AZ Serialization
CCompoundSplineTrack::CCompoundSplineTrack()
    : m_refCount(0)
    , m_nDimensions(0)
    , m_valueType(AnimValueType::Float)
#ifdef MOVIESYSTEM_SUPPORT_EDITING
    , m_bCustomColorSet(false)
#endif
{
}

void CCompoundSplineTrack::SetNode(IAnimNode* node)
{
    m_node = node;
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i]->SetNode(node);
    }
}
//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetTimeRange(const Range& timeRange)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i]->SetTimeRange(timeRange);
    }
}

//////////////////////////////////////////////////////////////////////////
/// @deprecated Serialization for Sequence data in Component Entity Sequences now occurs through AZ::SerializeContext and the Sequence Component
bool CCompoundSplineTrack::Serialize(XmlNodeRef& xmlNode, bool bLoading, bool bLoadEmptyTracks /*=true */)
{
#ifdef MOVIESYSTEM_SUPPORT_EDITING
    if (bLoading)
    {
        int flags = m_flags;
        xmlNode->getAttr("Flags", flags);
        SetFlags(flags);
        xmlNode->getAttr("HasCustomColor", m_bCustomColorSet);
        if (m_bCustomColorSet)
        {
            unsigned int abgr;
            xmlNode->getAttr("CustomColor", abgr);
            m_customColor = ColorB(abgr);
        }
    }
    else
    {
        int flags = GetFlags();
        xmlNode->setAttr("Flags", flags);
        xmlNode->setAttr("HasCustomColor", m_bCustomColorSet);
        if (m_bCustomColorSet)
        {
            xmlNode->setAttr("CustomColor", m_customColor.pack_abgr8888());
        }
    }
#endif

    for (int i = 0; i < m_nDimensions; i++)
    {
        XmlNodeRef subTrackNode;
        if (bLoading)
        {
            subTrackNode = xmlNode->getChild(i);
        }
        else
        {
            subTrackNode = xmlNode->newChild("NewSubTrack");
        }
        m_subTracks[i]->Serialize(subTrackNode, bLoading, bLoadEmptyTracks);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
bool CCompoundSplineTrack::SerializeSelection(XmlNodeRef& xmlNode, bool bLoading, bool bCopySelected /*=false*/, float fTimeOffset /*=0*/)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        XmlNodeRef subTrackNode;
        if (bLoading)
        {
            subTrackNode = xmlNode->getChild(i);
        }
        else
        {
            subTrackNode = xmlNode->newChild("NewSubTrack");
        }
        m_subTracks[i]->SerializeSelection(subTrackNode, bLoading, bCopySelected, fTimeOffset);
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::GetValue(float time, float& value, bool applyMultiplier)
{
    for (int i = 0; i < 1 && i < m_nDimensions; i++)
    {
        m_subTracks[i]->GetValue(time, value, applyMultiplier);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::GetValue(float time, Vec3& value, bool applyMultiplier)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        float v = value[i];
        m_subTracks[i]->GetValue(time, v, applyMultiplier);
        value[i] = v;
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::GetValue(float time, Vec4& value, bool applyMultiplier)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        float v = value[i];
        m_subTracks[i]->GetValue(time, v, applyMultiplier);
        value[i] = v;
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::GetValue(float time, Quat& value)
{
    if (m_nDimensions == 3)
    {
        // Assume Euler Angles XYZ
        float angles[3] = {0, 0, 0};
        for (int i = 0; i < m_nDimensions; i++)
        {
            m_subTracks[i]->GetValue(time, angles[i]);
        }
        value = Quat::CreateRotationXYZ(Ang3(DEG2RAD(angles[0]), DEG2RAD(angles[1]), DEG2RAD(angles[2])));
    }
    else
    {
        assert(0);
        value.SetIdentity();
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetValue(float time, const float& value, bool bDefault, bool applyMultiplier)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i]->SetValue(time, value, bDefault, applyMultiplier);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetValue(float time, const Vec3& value, bool bDefault, bool applyMultiplier)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i]->SetValue(time, value[i], bDefault, applyMultiplier);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetValue(float time, const Vec4& value, bool bDefault, bool applyMultiplier)
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        m_subTracks[i]->SetValue(time, value[i], bDefault, applyMultiplier);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetValue(float time, const Quat& value, bool bDefault)
{
    if (m_nDimensions == 3)
    {
        // Assume Euler Angles XYZ
        Ang3 angles = Ang3::GetAnglesXYZ(value);
        for (int i = 0; i < 3; i++)
        {
            float degree = RAD2DEG(angles[i]);
            if (false == bDefault)
            {
                // Try to prefer the shortest path of rotation.
                float degree0 = 0.0f;
                m_subTracks[i]->GetValue(time, degree0);
                degree = PreferShortestRotPath(degree, degree0);
            }
            m_subTracks[i]->SetValue(time, degree, bDefault);
        }
    }
    else
    {
        assert(0);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::OffsetKeyPosition(const Vec3& offset)
{
    if (m_nDimensions == 3)
    {
        for (int i = 0; i < 3; i++)
        {
            IAnimTrack* pSubTrack = m_subTracks[i].get();
            // Iterate over all keys.
            for (int k = 0, num = pSubTrack->GetNumKeys(); k < num; k++)
            {
                // Offset each key.
                float time = pSubTrack->GetKeyTime(k);
                float value = 0;
                pSubTrack->GetValue(time, value);
                value = value + offset[i];
                pSubTrack->SetValue(time, value);
            }
        }
    }
    else
    {
        assert(0);
    }
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::UpdateKeyDataAfterParentChanged(const AZ::Transform& oldParentWorldTM, const AZ::Transform& newParentWorldTM)
{
    AZ_Assert(m_nDimensions == 3, "Expected 3 dimensions, position, rotation or scale.");

    // Get the inverse of the new parent world transform.
    AZ::Transform newParentInverseWorldTM = newParentWorldTM;
    newParentInverseWorldTM.InvertFull();

    // Collect all times that have key data on any track.
    AZStd::vector<float> allTimes;
    for (int i = 0; i < 3; i++)
    {
        IAnimTrack* subTrack = m_subTracks[i].get();
        for (int k = 0, num = subTrack->GetNumKeys(); k < num; k++)
        {
            // If this key time is not already in the list, add it.
            float time = subTrack->GetKeyTime(k);
            if (AZStd::find(allTimes.begin(), allTimes.end(), time) == allTimes.end())
            {
                allTimes.push_back(time);
            }
        }
    }

    // Set or create key data for each time gathered from the keys.
    for (float time : allTimes)
    {
        IAnimTrack* subTrack[3]{ m_subTracks[0].get(), m_subTracks[1].get(), m_subTracks[2].get() };

        // Create a 3 float vector with values from the 3 tracks.
        AZ::Vector3 vector;
        for (int i = 0; i < 3; i++)
        {
            float value = 0;
            subTrack[i]->GetValue(time, value);
            vector.SetElement(i, value);
        }

        // Different track types need to be handled slightly differently
        switch (m_nParamType.GetType())
        {

        case AnimParamType::Position:
        {
            // Use the old parent world transform to get the current key data into world space.
            AZ::Vector3 worldPosition = oldParentWorldTM * vector;

            // Use the inverse transform of the new parent to convert the world space
            // key data into local space relative to the new parent.
            vector = newParentInverseWorldTM * worldPosition;
        }
        break;

        case AnimParamType::Rotation:
        {
           // Use the old parent world rotation to get the key data into world space.
            AZ::Vector3 worldRoation = AzFramework::ConvertTransformToEulerDegrees(oldParentWorldTM) + vector;

            // Remove the world rotation of the the new parent to convert the world space
            // key data into local space relative to the new parent.
            vector = AzFramework::ConvertTransformToEulerDegrees(newParentInverseWorldTM) + worldRoation;        
        }
        break;

        case AnimParamType::Scale:
        {
            // Use the old parent world transform scale to get the key data into world space.
            AZ::Vector3 worldScale = oldParentWorldTM.RetrieveScaleExact() * vector;

            // Use the inverse transform scale of the new parent to convert the world space
            // key data into local space relative to the new parent.
            vector = newParentInverseWorldTM.RetrieveScaleExact() * worldScale;
        }
        break;
        
        default:
            AZ_Assert(false, "Unsupported Anim Param Type: %s", m_nParamType.GetName());

        }

        // Update all of the tracks with the new float values.
        // This may create a new key if there was not one before.
        for (int i = 0; i < 3; i++)
        {
            // Update track key data
            subTrack[i]->SetValue(time, vector.GetElement(i));
        }
    }
}

//////////////////////////////////////////////////////////////////////////
IAnimTrack* CCompoundSplineTrack::GetSubTrack(int nIndex) const
{
    assert(nIndex >= 0 && nIndex < m_nDimensions);
    return m_subTracks[nIndex].get();
}

//////////////////////////////////////////////////////////////////////////
const char* CCompoundSplineTrack::GetSubTrackName(int nIndex) const
{
    assert(nIndex >= 0 && nIndex < m_nDimensions);
    return m_subTrackNames[nIndex].c_str();
}


//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::SetSubTrackName(int nIndex, const char* name)
{
    assert(nIndex >= 0 && nIndex < m_nDimensions);
    assert(name);
    m_subTrackNames[nIndex] = name;
}

//////////////////////////////////////////////////////////////////////////
int CCompoundSplineTrack::GetNumKeys() const
{
    int nKeys = 0;
    for (int i = 0; i < m_nDimensions; i++)
    {
        nKeys += m_subTracks[i]->GetNumKeys();
    }
    return nKeys;
}

//////////////////////////////////////////////////////////////////////////
bool CCompoundSplineTrack::HasKeys() const
{
    for (int i = 0; i < m_nDimensions; i++)
    {
        if (m_subTracks[i]->GetNumKeys())
        {
            return true;
        }
    }
    return false;
}

float CCompoundSplineTrack::PreferShortestRotPath(float degree, float degree0) const
{
    // Assumes the degree is in (-PI, PI).
    assert(-181.0f < degree && degree < 181.0f);
    float degree00 = degree0;
    degree0 = fmod_tpl(degree0, 360.0f);
    float n = (degree00 - degree0) / 360.0f;
    float degreeAlt;
    if (degree >= 0)
    {
        degreeAlt = degree - 360.0f;
    }
    else
    {
        degreeAlt = degree + 360.0f;
    }
    if (fabs(degreeAlt - degree0) < fabs(degree - degree0))
    {
        return degreeAlt + n * 360.0f;
    }
    else
    {
        return degree + n * 360.0f;
    }
}

int CCompoundSplineTrack::GetSubTrackIndex(int& key) const
{
    assert(key >= 0 && key < GetNumKeys());
    int count = 0;
    for (int i = 0; i < m_nDimensions; i++)
    {
        if (key < count + m_subTracks[i]->GetNumKeys())
        {
            key = key - count;
            return i;
        }
        count += m_subTracks[i]->GetNumKeys();
    }
    return -1;
}

void CCompoundSplineTrack::RemoveKey(int num)
{
    assert(num >= 0 && num < GetNumKeys());
    int i = GetSubTrackIndex(num);
    assert(i >= 0);
    if (i < 0)
    {
        return;
    }
    m_subTracks[i]->RemoveKey(num);
}

void CCompoundSplineTrack::GetKeyInfo(int key, const char*& description, float& duration)
{
    static char str[64];
    duration = 0;
    description = str;
    const char* subDesc = NULL;
    float time = GetKeyTime(key);
    int m = 0;
    /// Using the time obtained, combine descriptions from keys of the same time
    /// in sub-tracks if any into one compound description.
    str[0] = 0;
    // A head case
    for (m = 0; m < m_subTracks[0]->GetNumKeys(); ++m)
    {
        if (m_subTracks[0]->GetKeyTime(m) == time)
        {
            float dummy;
            m_subTracks[0]->GetKeyInfo(m, subDesc, dummy);
            cry_strcat(str, subDesc);
            break;
        }
    }
    if (m == m_subTracks[0]->GetNumKeys())
    {
        cry_strcat(str, m_subTrackNames[0].c_str());
    }
    // Tail cases
    for (int i = 1; i < GetSubTrackCount(); ++i)
    {
        cry_strcat(str, ",");
        for (m = 0; m < m_subTracks[i]->GetNumKeys(); ++m)
        {
            if (m_subTracks[i]->GetKeyTime(m) == time)
            {
                float dummy;
                m_subTracks[i]->GetKeyInfo(m, subDesc, dummy);
                cry_strcat(str, subDesc);
                break;
            }
        }
        if (m == m_subTracks[i]->GetNumKeys())
        {
            cry_strcat(str, m_subTrackNames[i].c_str());
        }
    }
}

float CCompoundSplineTrack::GetKeyTime(int index) const
{
    assert(index >= 0 && index < GetNumKeys());
    int i = GetSubTrackIndex(index);
    assert(i >= 0);
    if (i < 0)
    {
        return 0;
    }
    return m_subTracks[i]->GetKeyTime(index);
}

void CCompoundSplineTrack::SetKeyTime(int index, float time)
{
    assert(index >= 0 && index < GetNumKeys());
    int i = GetSubTrackIndex(index);
    assert(i >= 0);
    if (i < 0)
    {
        return;
    }
    m_subTracks[i]->SetKeyTime(index, time);
}

bool CCompoundSplineTrack::IsKeySelected(int key) const
{
    assert(key >= 0 && key < GetNumKeys());
    int i = GetSubTrackIndex(key);
    assert(i >= 0);
    if (i < 0)
    {
        return false;
    }
    return m_subTracks[i]->IsKeySelected(key);
}

void CCompoundSplineTrack::SelectKey(int key, bool select)
{
    assert(key >= 0 && key < GetNumKeys());
    int i = GetSubTrackIndex(key);
    assert(i >= 0);
    if (i < 0)
    {
        return;
    }
    float keyTime = m_subTracks[i]->GetKeyTime(key);
    // In the case of compound tracks, animators want to
    // select all keys of the same time in the sub-tracks together.
    const float timeEpsilon = 0.001f;
    for (int k = 0; k < m_nDimensions; ++k)
    {
        for (int m = 0; m < m_subTracks[k]->GetNumKeys(); ++m)
        {
            if (fabs(m_subTracks[k]->GetKeyTime(m) - keyTime) < timeEpsilon)
            {
                m_subTracks[k]->SelectKey(m, select);
                break;
            }
        }
    }
}

int CCompoundSplineTrack::NextKeyByTime(int key) const
{
    assert(key >= 0 && key < GetNumKeys());
    float time = GetKeyTime(key);
    int count = 0, result = -1;
    float timeNext = FLT_MAX;
    for (int i = 0; i < GetSubTrackCount(); ++i)
    {
        for (int k = 0; k < m_subTracks[i]->GetNumKeys(); ++k)
        {
            float t = m_subTracks[i]->GetKeyTime(k);
            if (t > time)
            {
                if (t < timeNext)
                {
                    timeNext = t;
                    result = count + k;
                }
                break;
            }
        }
        count += m_subTracks[i]->GetNumKeys();
    }
    return result;
}

//////////////////////////////////////////////////////////////////////////
void CCompoundSplineTrack::Reflect(AZ::SerializeContext* serializeContext)
{
    serializeContext->Class<CCompoundSplineTrack>()
        ->Version(1)
        ->Field("Flags", &CCompoundSplineTrack::m_flags)
        ->Field("ParamType", &CCompoundSplineTrack::m_nParamType)
        ->Field("NumSubTracks", &CCompoundSplineTrack::m_nDimensions)
        ->Field("SubTracks", &CCompoundSplineTrack::m_subTracks)
        ->Field("SubTrackNames", &CCompoundSplineTrack::m_subTrackNames)
        ->Field("ValueType", &CCompoundSplineTrack::m_valueType);
}