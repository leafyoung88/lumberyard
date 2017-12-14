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


#pragma once

#include <AzCore/EBus/EBus.h>
#include <AzCore/Component/ComponentBus.h>
#include <AzCore/Math/Vector2.h>
#include <AzCore/Math/Vector3.h>
#include <AzCore/Math/Quaternion.h>
#include <AzCore/Asset/AssetCommon.h>

namespace EMotionFX
{
    class AnimGraphInstance;
}

namespace EMotionFX
{
    namespace Integration
    {
        /**
         * EmotionFX Anim Graph Component Request Bus
         * Used for making requests to the EMotionFX Anim Graph Components.
         */
        class AnimGraphComponentRequests
            : public AZ::ComponentBus
        {
        public:

            static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;

            /// Retrieves the component's live graph instance.
            /// \return pointer to anim graph instance.
            virtual EMotionFX::AnimGraphInstance* GetAnimGraphInstance() { return nullptr; }

            /// Retrieve parameter index for a given parameter name.
            /// Retrieving the index and using it to set parameter values is more performant than setting by name.
            /// \param parameterName - name of parameter for which to retrieve the index.
            /// \return parameter index
            virtual AZ::u32 FindParameterIndex(const char* parameterName) = 0;

            /// Updates a anim graph property given a float value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterFloat(AZ::u32 parameterIndex, float value) = 0;

            /// Updates a anim graph property given a boolean value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterBool(AZ::u32 parameterIndex, bool value) = 0;

            /// Updates a anim graph property given a string value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterString(AZ::u32 parameterIndex, const char* value) = 0;

            /// Updates a anim graph property given a Vector2 value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterVector2(AZ::u32 parameterIndex, const AZ::Vector2& value) = 0;

            /// Updates a anim graph property given a Vector3 value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterVector3(AZ::u32 parameterIndex, const AZ::Vector3& value) = 0;

            /// Updates a anim graph property given euler rotation values.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterRotationEuler(AZ::u32 parameterIndex, const AZ::Vector3& value) = 0;

            /// Updates a anim graph property given a quaternion value.
            /// \param parameterIndex - index of parameter to set
            /// \param value - value to set
            virtual void SetParameterRotation(AZ::u32 parameterIndex, const AZ::Quaternion& value) = 0;


            /// Updates a anim graph property given a float value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterFloat(const char* parameterName, float value) = 0;

            /// Updates a anim graph property given a boolean value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterBool(const char* parameterName, bool value) = 0;

            /// Updates a anim graph property given a string value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterString(const char* parameterName, const char* value) = 0;

            /// Updates a anim graph property given a Vector2 value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterVector2(const char* parameterName, const AZ::Vector2& value) = 0;

            /// Updates a anim graph property given a Vector3 value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterVector3(const char* parameterName, const AZ::Vector3& value) = 0;

            /// Updates a anim graph property given euler rotation values.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterRotationEuler(const char* parameterName, const AZ::Vector3& value) = 0;

            /// Updates a anim graph property given a quaternion value.
            /// \param parameterName - name of parameter to set
            /// \param value
            virtual void SetNamedParameterRotation(const char* parameterName, const AZ::Quaternion& value) = 0;


            /// Retrieves a anim graph property as a float value.
            /// \param parameterIndex - index of parameter to set
            virtual float GetParameterFloat(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property as a boolean value.
            /// \param parameterIndex - index of parameter to set
            virtual bool GetParameterBool(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property given a string value.
            /// \param parameterIndex - index of parameter to set
            virtual AZStd::string GetParameterString(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property as a Vector2 value.
            /// \param parameterIndex - index of parameter to set
            virtual AZ::Vector2 GetParameterVector2(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property as a Vector3 value.
            /// \param parameterIndex - index of parameter to set
            virtual AZ::Vector3 GetParameterVector3(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property given as euler rotation values.
            /// \param parameterIndex - index of parameter to set
            virtual AZ::Vector3 GetParameterRotationEuler(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property as a quaternion value.
            /// \param parameterIndex - index of parameter to set
            virtual AZ::Quaternion GetParameterRotation(AZ::u32 parameterIndex) = 0;

            /// Retrieves a anim graph property as a float value.
            /// \param parameterName - name of parameter to get
            virtual float GetNamedParameterFloat(const char* parameterName) = 0;

            /// Retrieves a anim graph property as a boolean value.
            /// \param parameterName - name of parameter to get
            virtual bool GetNamedParameterBool(const char* parameterName) = 0;

            /// Retrieves a anim graph property given a string value.
            /// \param parameterName - name of parameter to get
            virtual AZStd::string GetNamedParameterString(const char* parameterName) = 0;

            /// Retrieves a anim graph property as a Vector2 value.
            /// \param parameterName - name of parameter to get
            virtual AZ::Vector2 GetNamedParameterVector2(const char* parameterName) = 0;

            /// Retrieves a anim graph property as a Vector3 value.
            /// \param parameterName - name of parameter to get
            virtual AZ::Vector3 GetNamedParameterVector3(const char* parameterName) = 0;

            /// Retrieves a anim graph property given as euler rotation values.
            /// \param parameterName - name of parameter to get
            virtual AZ::Vector3 GetNamedParameterRotationEuler(const char* parameterName) = 0;

            /// Retrieves a anim graph property as a quaternion value.
            /// \param parameterName - name of parameter to get
            virtual AZ::Quaternion GetNamedParameterRotation(const char* parameterName) = 0;
        };

        using AnimGraphComponentRequestBus = AZ::EBus<AnimGraphComponentRequests>;

        /**
         * EmotionFX Anim Graph Component Notification Bus
         * Used for monitoring events from Anim Graph components.
         */
        class AnimGraphComponentNotifications
            : public AZ::ComponentBus
        {
        public:

            //////////////////////////////////////////////////////////////////////////
            /**
             * Custom connection policy notifies connecting listeners immediately if anim graph instance is already created.
             */
            template<class Bus>
            struct AssetConnectionPolicy
                : public AZ::EBusConnectionPolicy<Bus>
            {
                static void Connect(typename Bus::BusPtr& busPtr, typename Bus::Context& context, typename Bus::HandlerNode& handler, const typename Bus::BusIdType& id = 0)
                {
                    AZ::EBusConnectionPolicy<Bus>::Connect(busPtr, context, handler, id);

                    EMotionFX::AnimGraphInstance* instance = nullptr;
                    AnimGraphComponentRequestBus::EventResult(instance, id, &AnimGraphComponentRequestBus::Events::GetAnimGraphInstance);
                    if (instance)
                    {
                        handler->OnAnimGraphInstanceCreated(instance);
                    }
                }
            };
            template<typename Bus>
            using ConnectionPolicy = AssetConnectionPolicy<Bus>;
            //////////////////////////////////////////////////////////////////////////

            /// Notifies listeners when the component has created a graph instance.
            /// \param animGraphInstance - pointer to anim graph instance
            virtual void OnAnimGraphInstanceCreated(EMotionFX::AnimGraphInstance* /*animGraphInstance*/) {};

            /// Notifies listeners when the component is destroying a graph instance.
            /// \param animGraphInstance - pointer to anim graph instance
            virtual void OnAnimGraphInstanceDestroyed(EMotionFX::AnimGraphInstance* /*animGraphInstance*/) {};

            /// Notifies listeners when a float parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphFloatParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, float beforeValue, float afterValue) {};

            /// Notifies listeners when a bool parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphBoolParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, bool beforeValue, bool afterValue) {};

            /// Notifies listeners when a string parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphStringParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, const char* beforeValue, const char* afterValue) {};

            /// Notifies listeners when a vector2 parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphVector2ParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, const AZ::Vector2& beforeValue, const AZ::Vector2& afterValue) {};

            /// Notifies listeners when a vector3 parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphVector3ParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, const AZ::Vector3& beforeValue, const AZ::Vector3& afterValue) {};

            /// Notifies listeners when a rotation parameter changes
            /// \param animGraphInstance - pointer to anim graph instance
            /// \param parameterIndex - index of changed parameter
            /// \param beforeValue - value before the change
            /// \param afterValue - value after the change
            virtual void OnAnimGraphRotationParameterChanged(EMotionFX::AnimGraphInstance* /*animGraphInstance*/, AZ::u32 parameterIndex, const AZ::Quaternion& beforeValue, const AZ::Quaternion& afterValue) {};
        };

        using AnimGraphComponentNotificationBus = AZ::EBus<AnimGraphComponentNotifications>;


        //Editor
        class EditorAnimGraphComponentRequests
            : public AZ::ComponentBus
        {
        public:

            static const AZ::EBusHandlerPolicy HandlerPolicy = AZ::EBusHandlerPolicy::Single;

            /// Retrieves the component's Animgraph AssetId.
            /// \return assetId
            virtual const AZ::Data::AssetId& GetAnimGraphAssetId() = 0;

            /// Retrieves the component's MotionSet AssetId.
            /// \return assetId
            virtual const AZ::Data::AssetId& GetMotionSetAssetId() = 0;
        };

        using EditorAnimGraphComponentRequestBus = AZ::EBus<EditorAnimGraphComponentRequests>;

    } // namespace Integration
} // namespace EMotionFX