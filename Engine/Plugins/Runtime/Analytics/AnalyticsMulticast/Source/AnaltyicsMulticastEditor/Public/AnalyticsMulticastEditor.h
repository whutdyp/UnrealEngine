// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ModuleInterface.h"

class FAnalyticsMulticastEditorModule :
	public IModuleInterface
{
private:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

