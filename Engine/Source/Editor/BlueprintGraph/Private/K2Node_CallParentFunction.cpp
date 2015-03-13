// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "BlueprintGraphPrivatePCH.h"
#include "K2Node_CallParentFunction.h"
#include "EditorStyleSettings.h"
#include "GraphEditorSettings.h"

#define LOCTEXT_NAMESPACE "K2Node"

UK2Node_CallParentFunction::UK2Node_CallParentFunction(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bIsFinalFunction = true;
}

FText UK2Node_CallParentFunction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	UFunction* Function = FunctionReference.ResolveMember<UFunction>(GetBlueprintClassFromNode());
	FString FunctionName = Function ? GetUserFacingFunctionName( Function ) : FunctionReference.GetMemberName().ToString();

	if( GEditor && GetDefault<UEditorStyleSettings>()->bShowFriendlyNames )
	{
		FunctionName = FName::NameToDisplayString(FunctionName, false);
	}

	FFormatNamedArguments Args;
	Args.Add(TEXT("FunctionName"),  FText::FromString( FunctionName ));
	return FText::Format( LOCTEXT( "CallSuperFunction", "Parent: {FunctionName}" ), Args);
}

FLinearColor UK2Node_CallParentFunction::GetNodeTitleColor() const
{
	return GetDefault<UGraphEditorSettings>()->ParentFunctionCallNodeTitleColor;
}

void UK2Node_CallParentFunction::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	UEdGraphPin* SelfPin = Schema->FindSelfPin(*this, EGPD_Input);
	if( SelfPin )
	{
		SelfPin->bHidden = true;
	}
}

void UK2Node_CallParentFunction::SetFromFunction(const UFunction* Function)
{
	if (Function != NULL)
	{
		bIsPureFunc = Function->HasAnyFunctionFlags(FUNC_BlueprintPure);
		bIsConstFunc = Function->HasAnyFunctionFlags(FUNC_Const);

		UClass* OwnerClass = Function->GetOwnerClass();

		FGuid FunctionGuid;
		if (OwnerClass != nullptr)
		{
			OwnerClass = OwnerClass->GetAuthoritativeClass();
			UBlueprint::GetGuidFromClassByFieldName<UFunction>(OwnerClass, Function->GetFName(), FunctionGuid);
		}

		FunctionReference.SetDirect(Function->GetFName(), FunctionGuid, OwnerClass, /*bIsConsideredSelfContext =*/false);
	}
}

void UK2Node_CallParentFunction::PostPlacedNewNode()
{
	// We don't want to check if our function exists in the current scope

	UK2Node::PostPlacedNewNode();
}

#undef LOCTEXT_NAMESPACE
