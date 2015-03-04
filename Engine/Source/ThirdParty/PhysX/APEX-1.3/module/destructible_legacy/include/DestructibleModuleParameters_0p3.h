/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */


// This file was generated by NxParameterized/scripts/GenParameterized.pl
// Created: 2015.01.18 19:26:28

#ifndef HEADER_DestructibleModuleParameters_0p3_h
#define HEADER_DestructibleModuleParameters_0p3_h

#include "NxParametersTypes.h"

#ifndef NX_PARAMETERIZED_ONLY_LAYOUTS
#include "NxParameterized.h"
#include "NxParameters.h"
#include "NxParameterizedTraits.h"
#include "NxTraitsInternal.h"
#endif

namespace physx
{
namespace apex
{

#pragma warning(push)
#pragma warning(disable: 4324) // structure was padded due to __declspec(align())

namespace DestructibleModuleParameters_0p3NS
{



struct ParametersStruct
{

	physx::PxU32 maxDynamicChunkIslandCount;
	bool sortFIFOByBenefit;
	physx::PxF32 validBoundsPadding;
	physx::PxF32 maxChunkSeparationLOD;
	physx::PxU32 maxActorCreatesPerFrame;
	physx::PxU32 maxChunkDepthOffset;
	physx::PxF32 massScale;
	physx::PxF32 scaledMassExponent;

};

static const physx::PxU32 checksum[] = { 0xea4f5a53, 0x75a1901c, 0xd7138e07, 0xef8c1364, };

} // namespace DestructibleModuleParameters_0p3NS

#ifndef NX_PARAMETERIZED_ONLY_LAYOUTS
class DestructibleModuleParameters_0p3 : public NxParameterized::NxParameters, public DestructibleModuleParameters_0p3NS::ParametersStruct
{
public:
	DestructibleModuleParameters_0p3(NxParameterized::Traits* traits, void* buf = 0, PxI32* refCount = 0);

	virtual ~DestructibleModuleParameters_0p3();

	virtual void destroy();

	static const char* staticClassName(void)
	{
		return("DestructibleModuleParameters");
	}

	const char* className(void) const
	{
		return(staticClassName());
	}

	static const physx::PxU32 ClassVersion = ((physx::PxU32)0 << 16) + (physx::PxU32)3;

	static physx::PxU32 staticVersion(void)
	{
		return ClassVersion;
	}

	physx::PxU32 version(void) const
	{
		return(staticVersion());
	}

	static const physx::PxU32 ClassAlignment = 8;

	static const physx::PxU32* staticChecksum(physx::PxU32& bits)
	{
		bits = 8 * sizeof(DestructibleModuleParameters_0p3NS::checksum);
		return DestructibleModuleParameters_0p3NS::checksum;
	}

	static void freeParameterDefinitionTable(NxParameterized::Traits* traits);

	const physx::PxU32* checksum(physx::PxU32& bits) const
	{
		return staticChecksum(bits);
	}

	const DestructibleModuleParameters_0p3NS::ParametersStruct& parameters(void) const
	{
		DestructibleModuleParameters_0p3* tmpThis = const_cast<DestructibleModuleParameters_0p3*>(this);
		return *(static_cast<DestructibleModuleParameters_0p3NS::ParametersStruct*>(tmpThis));
	}

	DestructibleModuleParameters_0p3NS::ParametersStruct& parameters(void)
	{
		return *(static_cast<DestructibleModuleParameters_0p3NS::ParametersStruct*>(this));
	}

	virtual NxParameterized::ErrorType getParameterHandle(const char* long_name, NxParameterized::Handle& handle) const;
	virtual NxParameterized::ErrorType getParameterHandle(const char* long_name, NxParameterized::Handle& handle);

	void initDefaults(void);

protected:

	virtual const NxParameterized::DefinitionImpl* getParameterDefinitionTree(void);
	virtual const NxParameterized::DefinitionImpl* getParameterDefinitionTree(void) const;


	virtual void getVarPtr(const NxParameterized::Handle& handle, void*& ptr, size_t& offset) const;

private:

	void buildTree(void);
	void initDynamicArrays(void);
	void initStrings(void);
	void initReferences(void);
	void freeDynamicArrays(void);
	void freeStrings(void);
	void freeReferences(void);

	static bool mBuiltFlag;
	static NxParameterized::MutexType mBuiltFlagMutex;
};

class DestructibleModuleParameters_0p3Factory : public NxParameterized::Factory
{
	static const char* const vptr;

public:
	virtual NxParameterized::Interface* create(NxParameterized::Traits* paramTraits)
	{
		// placement new on this class using mParameterizedTraits

		void* newPtr = paramTraits->alloc(sizeof(DestructibleModuleParameters_0p3), DestructibleModuleParameters_0p3::ClassAlignment);
		if (!NxParameterized::IsAligned(newPtr, DestructibleModuleParameters_0p3::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class DestructibleModuleParameters_0p3");
			paramTraits->free(newPtr);
			return 0;
		}

		memset(newPtr, 0, sizeof(DestructibleModuleParameters_0p3)); // always initialize memory allocated to zero for default values
		return NX_PARAM_PLACEMENT_NEW(newPtr, DestructibleModuleParameters_0p3)(paramTraits);
	}

	virtual NxParameterized::Interface* finish(NxParameterized::Traits* paramTraits, void* bufObj, void* bufStart, physx::PxI32* refCount)
	{
		if (!NxParameterized::IsAligned(bufObj, DestructibleModuleParameters_0p3::ClassAlignment)
		        || !NxParameterized::IsAligned(bufStart, DestructibleModuleParameters_0p3::ClassAlignment))
		{
			NX_PARAM_TRAITS_WARNING(paramTraits, "Unaligned memory allocation for class DestructibleModuleParameters_0p3");
			return 0;
		}

		// Init NxParameters-part
		// We used to call empty constructor of DestructibleModuleParameters_0p3 here
		// but it may call default constructors of members and spoil the data
		NX_PARAM_PLACEMENT_NEW(bufObj, NxParameterized::NxParameters)(paramTraits, bufStart, refCount);

		// Init vtable (everything else is already initialized)
		*(const char**)bufObj = vptr;

		return (DestructibleModuleParameters_0p3*)bufObj;
	}

	virtual const char* getClassName()
	{
		return (DestructibleModuleParameters_0p3::staticClassName());
	}

	virtual physx::PxU32 getVersion()
	{
		return (DestructibleModuleParameters_0p3::staticVersion());
	}

	virtual physx::PxU32 getAlignment()
	{
		return (DestructibleModuleParameters_0p3::ClassAlignment);
	}

	virtual const physx::PxU32* getChecksum(physx::PxU32& bits)
	{
		return (DestructibleModuleParameters_0p3::staticChecksum(bits));
	}
};
#endif // NX_PARAMETERIZED_ONLY_LAYOUTS

} // namespace apex
} // namespace physx

#pragma warning(pop)

#endif
