// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "LandscapeEditorPrivatePCH.h"

#include "EditorSupportDelegates.h"
#include "ObjectTools.h"
#include "LandscapeEdMode.h"
#include "ScopedTransaction.h"
#include "LandscapeEdit.h"
#include "LandscapeRender.h"
#include "LandscapeDataAccess.h"
#include "LandscapeSplineProxies.h"
#include "LandscapeEditorModule.h"
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Editor/PropertyEditor/Public/PropertyEditorModule.h"
#include "Editor/UnrealEd/Public/Toolkits/ToolkitManager.h"
#include "LandscapeEdModeTools.h"
#include "ScopedTransaction.h"
#include "ImageWrapper.h"
#include "DynamicMeshBuilder.h"

//Slate dependencies
#include "Editor/LevelEditor/Public/LevelEditor.h"
#include "Editor/LevelEditor/Public/SLevelViewport.h"
#include "SLandscapeEditor.h"

// Classes
#include "Landscape.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeMaterialInstanceConstant.h"
#include "LandscapeSplinesComponent.h"
#include "InstancedFoliageActor.h"
#include "ComponentReregisterContext.h"
#include "Engine/Selection.h"
#include "LandscapeGizmoActiveActor.h"
#include "EngineUtils.h"
#include "Engine/Light.h"

#define LOCTEXT_NAMESPACE "Landscape"

DEFINE_LOG_CATEGORY(LogLandscapeEdMode);

struct HNewLandscapeGrabHandleProxy : public HHitProxy
{
	DECLARE_HIT_PROXY();

	ELandscapeEdge::Type Edge;

	HNewLandscapeGrabHandleProxy(ELandscapeEdge::Type InEdge) :
		HHitProxy(HPP_Wireframe),
		Edge(InEdge)
	{
	}

	virtual EMouseCursor::Type GetMouseCursor()
	{
		switch (Edge)
		{
		case ELandscapeEdge::X_Negative:
		case ELandscapeEdge::X_Positive:
			return EMouseCursor::ResizeLeftRight;
		case ELandscapeEdge::Y_Negative:
		case ELandscapeEdge::Y_Positive:
			return EMouseCursor::ResizeUpDown;
		case ELandscapeEdge::X_Negative_Y_Negative:
		case ELandscapeEdge::X_Positive_Y_Positive:
			return EMouseCursor::ResizeSouthEast;
		case ELandscapeEdge::X_Negative_Y_Positive:
		case ELandscapeEdge::X_Positive_Y_Negative:
			return EMouseCursor::ResizeSouthWest;
		}

		return EMouseCursor::SlashedCircle;
	}
};

IMPLEMENT_HIT_PROXY(HNewLandscapeGrabHandleProxy, HHitProxy)


void ALandscape::SplitHeightmap(ULandscapeComponent* Comp, bool bMoveToCurrentLevel /*= false*/)
{
	ULandscapeInfo* Info = Comp->GetLandscapeInfo();
	int32 ComponentSizeVerts = Comp->NumSubsections * (Comp->SubsectionSizeQuads + 1);
	// make sure the heightmap UVs are powers of two.
	int32 HeightmapSizeU = (1 << FMath::CeilLogTwo(ComponentSizeVerts));
	int32 HeightmapSizeV = (1 << FMath::CeilLogTwo(ComponentSizeVerts));

	UTexture2D* HeightmapTexture = NULL;
	TArray<FColor*> HeightmapTextureMipData;
	// Scope for FLandscapeEditDataInterface
	{
		// Read old data and split
		FLandscapeEditDataInterface LandscapeEdit(Info);
		TArray<uint8> HeightData;
		HeightData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		// Because of edge problem, normal would be just copy from old component data
		TArray<uint8> NormalData;
		NormalData.AddZeroed((1 + Comp->ComponentSizeQuads)*(1 + Comp->ComponentSizeQuads)*sizeof(uint16));
		LandscapeEdit.GetHeightDataFast(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, (uint16*)NormalData.GetData());

		// Construct the heightmap textures
		UObject* TextureOuter = bMoveToCurrentLevel ? Comp->GetWorld()->GetCurrentLevel()->GetOutermost() : nullptr;
		HeightmapTexture = Comp->GetLandscapeProxy()->CreateLandscapeTexture(HeightmapSizeU, HeightmapSizeV, TEXTUREGROUP_Terrain_Heightmap, TSF_BGRA8, TextureOuter);

		int32 MipSubsectionSizeQuads = Comp->SubsectionSizeQuads;
		int32 MipSizeU = HeightmapSizeU;
		int32 MipSizeV = HeightmapSizeV;
		while (MipSizeU > 1 && MipSizeV > 1 && MipSubsectionSizeQuads >= 1)
		{
			FColor* HeightmapTextureData = (FColor*)HeightmapTexture->Source.LockMip(HeightmapTextureMipData.Num());
			FMemory::Memzero(HeightmapTextureData, MipSizeU*MipSizeV*sizeof(FColor));
			HeightmapTextureMipData.Add(HeightmapTextureData);

			MipSizeU >>= 1;
			MipSizeV >>= 1;

			MipSubsectionSizeQuads = ((MipSubsectionSizeQuads + 1) >> 1) - 1;
		}

		Comp->HeightmapScaleBias = FVector4(1.0f / (float)HeightmapSizeU, 1.0f / (float)HeightmapSizeV, 0.0f, 0.0f);
		Comp->HeightmapTexture = HeightmapTexture;

		if (Comp->MaterialInstance)
		{
			Comp->MaterialInstance->SetTextureParameterValueEditorOnly(FName(TEXT("Heightmap")), HeightmapTexture);
		}

		for (int32 i = 0; i < HeightmapTextureMipData.Num(); i++)
		{
			HeightmapTexture->Source.UnlockMip(i);
		}
		LandscapeEdit.SetHeightData(Comp->GetSectionBase().X, Comp->GetSectionBase().Y, Comp->GetSectionBase().X + Comp->ComponentSizeQuads, Comp->GetSectionBase().Y + Comp->ComponentSizeQuads, (uint16*)HeightData.GetData(), 0, false, (uint16*)NormalData.GetData());
	}

	// End of LandscapeEdit interface
	HeightmapTexture->PostEditChange();
	// Reregister
	FComponentReregisterContext ReregisterContext(Comp);
}



void FLandscapeTool::SetEditRenderType()
{
	GLandscapeEditRenderMode = ELandscapeEditRenderMode::SelectRegion | (GLandscapeEditRenderMode & ELandscapeEditRenderMode::BitMaskForMask);
}

//
// FEdModeLandscape
//

/** Constructor */
FEdModeLandscape::FEdModeLandscape()
	: FEdMode()
	, NewLandscapePreviewMode(ENewLandscapePreviewMode::None)
	, DraggingEdge(ELandscapeEdge::None)
	, DraggingEdge_Remainder(0)
	, CurrentGizmoActor(nullptr)
	, CopyPasteTool(nullptr)
	, SplinesTool(nullptr)
	, LandscapeRenderAddCollision(nullptr)
	, CachedLandscapeMaterial(nullptr)
	, bToolActive(false)
{
	GLayerDebugColorMaterial = LoadObject<UMaterial>(NULL, TEXT("/Engine/EditorLandscapeResources/LayerVisMaterial.LayerVisMaterial"), NULL, LOAD_None, NULL);
	GSelectionColorMaterial = LoadObject<UMaterialInstanceConstant>(NULL, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_Selected.SelectBrushMaterial_Selected"), NULL, LOAD_None, NULL);
	GSelectionRegionMaterial = LoadObject<UMaterialInstanceConstant>(NULL, TEXT("/Engine/EditorLandscapeResources/SelectBrushMaterial_SelectedRegion.SelectBrushMaterial_SelectedRegion"), NULL, LOAD_None, NULL);
	GMaskRegionMaterial = LoadObject<UMaterialInstanceConstant>(NULL, TEXT("/Engine/EditorLandscapeResources/MaskBrushMaterial_MaskedRegion.MaskBrushMaterial_MaskedRegion"), NULL, LOAD_None, NULL);
	GLandscapeBlackTexture = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EngineResources/Black.Black"), NULL, LOAD_None, NULL);

	// Initialize modes
	InitializeToolModes();
	CurrentToolMode = nullptr;

	// Initialize tools.
	InitializeTool_Paint();
	InitializeTool_Smooth();
	InitializeTool_Flatten();
	InitializeTool_Ramp();
	InitializeTool_Erosion();
	InitializeTool_HydraErosion();
	InitializeTool_Noise();
	InitializeTool_Retopologize();
	InitializeTool_NewLandscape();
	InitializeTool_ResizeLandscape();
	InitializeTool_Select();
	InitializeTool_AddComponent();
	InitializeTool_DeleteComponent();
	InitializeTool_MoveToLevel();
	InitializeTool_Mask();
	InitializeTool_CopyPaste();
	InitializeTool_Visibility();
	InitializeTool_Splines();

	CurrentTool = nullptr;
	CurrentToolIndex = INDEX_NONE;

	// Initialize brushes
	InitializeBrushes();

	CurrentBrush = LandscapeBrushSets[0].Brushes[0];
	CurrentBrushSetIndex = 0;

	CurrentToolTarget.LandscapeInfo = nullptr;
	CurrentToolTarget.TargetType = ELandscapeToolTargetType::Heightmap;
	CurrentToolTarget.LayerInfo = nullptr;

	UISettings = NewObject<ULandscapeEditorObject>(GetTransientPackage(), TEXT("UISettings"), RF_Transactional);
	UISettings->SetParent(this);
}


/** Destructor */
FEdModeLandscape::~FEdModeLandscape()
{
	// Destroy tools.
	LandscapeTools.Empty();

	// Destroy brushes
	LandscapeBrushSets.Empty();

	// Clean up Debug Materials
	FlushRenderingCommands();
	GLayerDebugColorMaterial = NULL;
	GSelectionColorMaterial = NULL;
	GSelectionRegionMaterial = NULL;
	GMaskRegionMaterial = NULL;
	GLandscapeBlackTexture = NULL;
}

/** FGCObject interface */
void FEdModeLandscape::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Call parent implementation
	FEdMode::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(UISettings);

	Collector.AddReferencedObject(GLayerDebugColorMaterial);
	Collector.AddReferencedObject(GSelectionColorMaterial);
	Collector.AddReferencedObject(GSelectionRegionMaterial);
	Collector.AddReferencedObject(GMaskRegionMaterial);
	Collector.AddReferencedObject(GLandscapeBlackTexture);
}

void FEdModeLandscape::InitializeToolModes()
{
	FLandscapeToolMode* ToolMode_Manage = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Manage"), ELandscapeToolTargetTypeMask::NA);
	ToolMode_Manage->ValidTools.Add(TEXT("NewLandscape"));
	ToolMode_Manage->ValidTools.Add(TEXT("Select"));
	ToolMode_Manage->ValidTools.Add(TEXT("AddComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("DeleteComponent"));
	ToolMode_Manage->ValidTools.Add(TEXT("MoveToLevel"));
	ToolMode_Manage->ValidTools.Add(TEXT("ResizeLandscape"));
	ToolMode_Manage->ValidTools.Add(TEXT("Splines"));
	ToolMode_Manage->CurrentToolName = TEXT("Select");

	FLandscapeToolMode* ToolMode_Sculpt = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Sculpt"), ELandscapeToolTargetTypeMask::Heightmap | ELandscapeToolTargetTypeMask::Visibility);
	ToolMode_Sculpt->ValidTools.Add(TEXT("Sculpt"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Ramp"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Noise"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Erosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("HydraErosion"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Retopologize"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Visibility"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("Mask"));
	ToolMode_Sculpt->ValidTools.Add(TEXT("CopyPaste"));

	FLandscapeToolMode* ToolMode_Paint = new(LandscapeToolModes)FLandscapeToolMode(TEXT("ToolMode_Paint"), ELandscapeToolTargetTypeMask::Weightmap);
	ToolMode_Paint->ValidTools.Add(TEXT("Paint"));
	ToolMode_Paint->ValidTools.Add(TEXT("Smooth"));
	ToolMode_Paint->ValidTools.Add(TEXT("Flatten"));
	ToolMode_Paint->ValidTools.Add(TEXT("Noise"));
	ToolMode_Paint->ValidTools.Add(TEXT("Visibility"));
}

bool FEdModeLandscape::UsesToolkits() const
{
	return true;
}

/** FEdMode: Called when the mode is entered */
void FEdModeLandscape::Enter()
{
	// Call parent implementation
	FEdMode::Enter();

	ALandscapeProxy* SelectedLandscape = GEditor->GetSelectedActors()->GetTop<ALandscapeProxy>();
	if (SelectedLandscape)
	{
		CurrentToolTarget.LandscapeInfo = SelectedLandscape->GetLandscapeInfo();
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(SelectedLandscape, true, false);
	}
	else
	{
		GEditor->SelectNone(false, true);
	}

	for (TActorIterator<ALandscapeGizmoActiveActor> It(GetWorld()); It; ++It)
	{
		CurrentGizmoActor = *It;
		break;
	}

	if (!CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor = GetWorld()->SpawnActor<ALandscapeGizmoActiveActor>();
		CurrentGizmoActor->ImportFromClipboard();
	}

	// Update list of landscapes and layers
	// For now depends on the SpawnActor() above in order to get the current editor world as edmodes don't get told
	UpdateLandscapeList();
	UpdateTargetList();

	OnWorldChangeDelegateHandle                 = FEditorSupportDelegates::WorldChange.AddRaw(this, &FEdModeLandscape::OnWorldChange);
	OnMaterialCompilationFinishedDelegateHandle = UMaterial::OnMaterialCompilationFinished().AddRaw(this, &FEdModeLandscape::OnMaterialCompilationFinished);

	if (CurrentGizmoActor.IsValid())
	{
		CurrentGizmoActor->SetTargetLandscape(CurrentToolTarget.LandscapeInfo.Get());
	}
	CastChecked<ALandscapeGizmoActiveActor>(CurrentGizmoActor.Get())->bSnapToLandscapeGrid = UISettings->bSnapGizmo;

	int32 SquaredDataTex = ALandscapeGizmoActiveActor::DataTexSize * ALandscapeGizmoActiveActor::DataTexSize;

	if (CurrentGizmoActor.IsValid() && !CurrentGizmoActor->GizmoTexture)
	{
		// Init Gizmo Texture...
		CurrentGizmoActor->GizmoTexture = NewObject<UTexture2D>(GetTransientPackage(), NAME_None, RF_Transient);
		if (CurrentGizmoActor->GizmoTexture)
		{
			CurrentGizmoActor->GizmoTexture->Source.Init(
				ALandscapeGizmoActiveActor::DataTexSize,
				ALandscapeGizmoActiveActor::DataTexSize,
				1,
				1,
				TSF_G8
				);
			CurrentGizmoActor->GizmoTexture->SRGB = false;
			CurrentGizmoActor->GizmoTexture->CompressionNone = true;
			CurrentGizmoActor->GizmoTexture->MipGenSettings = TMGS_NoMipmaps;
			CurrentGizmoActor->GizmoTexture->AddressX = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->AddressY = TA_Clamp;
			CurrentGizmoActor->GizmoTexture->LODGroup = TEXTUREGROUP_Terrain_Weightmap;
			uint8* TexData = CurrentGizmoActor->GizmoTexture->Source.LockMip(0);
			FMemory::Memset(TexData, 0, SquaredDataTex*sizeof(uint8));
			// Restore Sampled Data if exist...
			if (CurrentGizmoActor->CachedScaleXY > 0.0f)
			{
				int32 SizeX = FMath::CeilToInt(CurrentGizmoActor->CachedWidth / CurrentGizmoActor->CachedScaleXY);
				int32 SizeY = FMath::CeilToInt(CurrentGizmoActor->CachedHeight / CurrentGizmoActor->CachedScaleXY);
				for (int32 Y = 0; Y < CurrentGizmoActor->SampleSizeY; ++Y)
				{
					for (int32 X = 0; X < CurrentGizmoActor->SampleSizeX; ++X)
					{
						float TexX = X * SizeX / CurrentGizmoActor->SampleSizeX;
						float TexY = Y * SizeY / CurrentGizmoActor->SampleSizeY;
						int32 LX = FMath::FloorToInt(TexX);
						int32 LY = FMath::FloorToInt(TexY);

						float FracX = TexX - LX;
						float FracY = TexY - LY;

						FGizmoSelectData* Data00 = CurrentGizmoActor->SelectedData.Find(ALandscape::MakeKey(LX, LY));
						FGizmoSelectData* Data10 = CurrentGizmoActor->SelectedData.Find(ALandscape::MakeKey(LX + 1, LY));
						FGizmoSelectData* Data01 = CurrentGizmoActor->SelectedData.Find(ALandscape::MakeKey(LX, LY + 1));
						FGizmoSelectData* Data11 = CurrentGizmoActor->SelectedData.Find(ALandscape::MakeKey(LX + 1, LY + 1));

						TexData[X + Y*ALandscapeGizmoActiveActor::DataTexSize] = FMath::Lerp(
							FMath::Lerp(Data00 ? Data00->Ratio : 0, Data10 ? Data10->Ratio : 0, FracX),
							FMath::Lerp(Data01 ? Data01->Ratio : 0, Data11 ? Data11->Ratio : 0, FracX),
							FracY
							) * 255;
					}
				}
			}
			CurrentGizmoActor->GizmoTexture->Source.UnlockMip(0);
			CurrentGizmoActor->GizmoTexture->PostEditChange();
			FlushRenderingCommands();
		}
	}

	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->SampledHeight.Num() != SquaredDataTex)
	{
		CurrentGizmoActor->SampledHeight.Empty(SquaredDataTex);
		CurrentGizmoActor->SampledHeight.AddZeroed(SquaredDataTex);
		CurrentGizmoActor->DataType = LGT_None;
	}

	if (CurrentGizmoActor.IsValid()) // Update Scene Proxy
	{
		CurrentGizmoActor->ReregisterAllComponents();
	}

	GLandscapeEditRenderMode = ELandscapeEditRenderMode::None;
	GLandscapeEditModeActive = true;

	// Load UI settings from config file
	UISettings->Load();

	// Initialize current tool prior to creating the landscape toolkit in case it has a dependency on it
	if (LandscapeList.Num() == 0)
	{
		SetCurrentToolMode("ToolMode_Manage", false);
		SetCurrentTool("NewLandscape");
	}
	else
	{
		if (CurrentToolMode == NULL)
		{
			SetCurrentToolMode("ToolMode_Sculpt", false);
			SetCurrentTool("Sculpt");
		}
		else
		{
			SetCurrentTool(CurrentToolMode->CurrentToolName);
		}
	}

	// Create the landscape editor window
	if (!Toolkit.IsValid())
	{
		Toolkit = MakeShareable(new FLandscapeToolKit);
		Toolkit->Init(Owner->GetToolkitHost());
	}

	// Force real-time viewports.  We'll back up the current viewport state so we can restore it when the
	// user exits this mode.
	const bool bWantRealTime = true;
	const bool bRememberCurrentState = true;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	CurrentBrush->EnterBrush();
	if (GizmoBrush)
	{
		GizmoBrush->EnterBrush();
	}
}


/** FEdMode: Called when the mode is exited */
void FEdModeLandscape::Exit()
{
	FEditorSupportDelegates::WorldChange.Remove(OnWorldChangeDelegateHandle);
	UMaterial::OnMaterialCompilationFinished().Remove(OnMaterialCompilationFinishedDelegateHandle);

	// Restore real-time viewport state if we changed it
	const bool bWantRealTime = false;
	const bool bRememberCurrentState = false;
	ForceRealTimeViewports(bWantRealTime, bRememberCurrentState);

	if (Toolkit.IsValid())
	{
		FToolkitManager::Get().CloseToolkit(Toolkit.ToSharedRef());
		Toolkit.Reset();
	}

	CurrentBrush->LeaveBrush();
	if (GizmoBrush)
	{
		GizmoBrush->LeaveBrush();
	}

	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		if (CurrentTool)
		{
			CurrentTool->ExitTool();
		}
	}
	CurrentTool = NULL;
	// Leave CurrentToolIndex set so we can restore the active tool on re-opening the landscape editor

	LandscapeList.Empty();
	LandscapeTargetList.Empty();

	// Clear any import landscape data
	UISettings->ClearImportLandscapeData();

	// Save UI settings to config file
	UISettings->Save();
	GLandscapeViewMode = ELandscapeViewMode::Normal;
	GLandscapeEditRenderMode = ELandscapeEditRenderMode::None;
	GLandscapeEditModeActive = false;

	CurrentGizmoActor = NULL;

	GEditor->SelectNone(false, true);

	// Clear all GizmoActors if there is no Landscape in World
	bool bIsLandscapeExist = false;
	for (TActorIterator<ALandscapeProxy> It(GetWorld()); It; ++It)
	{
		bIsLandscapeExist = true;
		break;
	}

	if (!bIsLandscapeExist)
	{
		for (TActorIterator<ALandscapeGizmoActor> It(GetWorld()); It; ++It)
		{
			GetWorld()->DestroyActor(*It, false, false);
		}
	}

	// Redraw one last time to remove any landscape editor stuff from view
	GEditor->RedrawLevelEditingViewports();

	// Call parent implementation
	FEdMode::Exit();
}

/** FEdMode: Called once per frame */
void FEdModeLandscape::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	FEdMode::Tick(ViewportClient, DeltaTime);

	if (!IsEditingEnabled())
	{
		return;
	}

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		bool bStaleTargetLandscapeInfo = CurrentToolTarget.LandscapeInfo.IsStale();
		bool bStaleTargetLandscape = CurrentToolTarget.LandscapeInfo.IsValid() && CurrentToolTarget.LandscapeInfo->LandscapeActor.IsStale();

		if (bStaleTargetLandscapeInfo || bStaleTargetLandscape)
		{
			UpdateLandscapeList();
		}

		if (CurrentToolTarget.LandscapeInfo.IsValid())
		{
			ALandscapeProxy* LandscapeProxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
			if (LandscapeProxy == NULL ||
				LandscapeProxy->GetLandscapeMaterial() != CachedLandscapeMaterial)
			{
				UpdateTargetList();
			}
		}

		if (CurrentTool)
		{
			CurrentTool->Tick(ViewportClient, DeltaTime);
		}
		if (CurrentBrush)
		{
			CurrentBrush->Tick(ViewportClient, DeltaTime);
		}
		if (CurrentBrush != GizmoBrush && CurrentGizmoActor.IsValid() && GizmoBrush && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo))
		{
			GizmoBrush->Tick(ViewportClient, DeltaTime);
		}
	}
}


/** FEdMode: Called when the mouse is moved over the viewport */
bool FEdModeLandscape::MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	if (bToolActive && !Viewport->KeyState(EKeys::LeftMouseButton))
	{
		if (CurrentTool)
		{
			CurrentTool->EndTool(ViewportClient);
			bToolActive = false;
		}
	}

	if (!IsEditingEnabled())
	{
		return false;
	}

	bool Result = false;
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool)
		{
			Result = CurrentTool->MouseMove(ViewportClient, Viewport, MouseX, MouseY);
			//ViewportClient->Invalidate( false, false );
		}
	}
	return Result;
}

bool FEdModeLandscape::DisallowMouseDeltaTracking() const
{
	// We never want to use the mouse delta tracker while painting
	return bToolActive;
}

/**
 * Called when the mouse is moved while a window input capture is in effect
 *
 * @param	InViewportClient	Level editor viewport client that captured the mouse input
 * @param	InViewport			Viewport that captured the mouse input
 * @param	InMouseX			New mouse cursor X coordinate
 * @param	InMouseY			New mouse cursor Y coordinate
 *
 * @return	true if input was handled
 */
bool FEdModeLandscape::CapturedMouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 MouseX, int32 MouseY)
{
	return MouseMove(ViewportClient, Viewport, MouseX, MouseY);
}

namespace
{
	bool GIsGizmoDragging = false;
}

/** FEdMode: Called when a mouse button is pressed */
bool FEdModeLandscape::StartTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo)
	{
		GIsGizmoDragging = true;
		return true;
	}
	return false;
}



/** FEdMode: Called when the a mouse button is released */
bool FEdModeLandscape::EndTracking(FEditorViewportClient* InViewportClient, FViewport* InViewport)
{
	if (GIsGizmoDragging)
	{
		GIsGizmoDragging = false;
		return true;
	}
	return false;
}

namespace
{
	bool RayIntersectTriangle(const FVector& Start, const FVector& End, const FVector& A, const FVector& B, const FVector& C, FVector& IntersectPoint)
	{
		const FVector BA = A - B;
		const FVector CB = B - C;
		const FVector TriNormal = BA ^ CB;

		bool bCollide = FMath::SegmentPlaneIntersection(Start, End, FPlane(A, TriNormal), IntersectPoint);
		if (!bCollide)
		{
			return false;
		}

		FVector BaryCentric = FMath::ComputeBaryCentric2D(IntersectPoint, A, B, C);
		if (BaryCentric.X > 0.0f && BaryCentric.Y > 0.0f && BaryCentric.Z > 0.0f)
		{
			return true;
		}
		return false;
	}
};

/** Trace under the mouse cursor and return the landscape hit and the hit location (in landscape quad space) */
bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, float& OutHitX, float& OutHitY)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapeMouseTrace(ViewportClient, MouseX, MouseY, OutHitX, OutHitY);
}

bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapeMouseTrace(ViewportClient, MouseX, MouseY, OutHitLocation);
}

/** Trace under the specified coordinates and return the landscape hit and the hit location (in landscape quad space) */
bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, float& OutHitX, float& OutHitY)
{
	FVector HitLocation;
	bool bResult = LandscapeMouseTrace(ViewportClient, MouseX, MouseY, HitLocation);
	OutHitX = HitLocation.X;
	OutHitY = HitLocation.Y;
	return bResult;
}

bool FEdModeLandscape::LandscapeMouseTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, FVector& OutHitLocation)
{
	// Cache a copy of the world pointer	
	UWorld* World = ViewportClient->GetWorld();

	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamilyContext::ConstructionValues(ViewportClient->Viewport, ViewportClient->GetScene(), ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));

	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRay.GetDirection();

	static FName TraceTag = FName(TEXT("LandscapeMouseTrace"));
	TArray<FHitResult> Results;
	// Each landscape component has 2 collision shapes, 1 of them is specific to landscape editor
	// Trace only ECC_Visibility channel, so we do hit only Editor specific shape
	World->LineTraceMultiByObjectType(Results, Start, End, FCollisionObjectQueryParams(ECollisionChannel::ECC_Visibility), FCollisionQueryParams(TraceTag, true));

	for (int32 i = 0; i < Results.Num(); i++)
	{
		const FHitResult& Hit = Results[i];
		ULandscapeHeightfieldCollisionComponent* CollisionComponent = Cast<ULandscapeHeightfieldCollisionComponent>(Hit.Component.Get());
		if (CollisionComponent)
		{
			ALandscapeProxy* HitLandscape = CollisionComponent->GetLandscapeProxy();
			if (HitLandscape &&
				CurrentToolTarget.LandscapeInfo.IsValid() &&
				CurrentToolTarget.LandscapeInfo->LandscapeGuid == HitLandscape->GetLandscapeGuid())
			{
				OutHitLocation = HitLandscape->LandscapeActorToWorld().InverseTransformPosition(Hit.Location);
				return true;
			}
		}
	}

	// For Add Landscape Component Mode
	if (CurrentTool->GetToolName() == FName("AddComponent") &&
		CurrentToolTarget.LandscapeInfo.IsValid())
	{
		bool bCollided = false;
		FVector IntersectPoint;
		LandscapeRenderAddCollision = NULL;
		// Need to optimize collision for AddLandscapeComponent...?
		for (auto& XYToAddCollisionPair : CurrentToolTarget.LandscapeInfo->XYtoAddCollisionMap)
		{
			FLandscapeAddCollision& AddCollision = XYToAddCollisionPair.Value;
			// Triangle 1
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[3], AddCollision.Corners[1], IntersectPoint);
			if (bCollided)
			{
				LandscapeRenderAddCollision = &AddCollision;
				break;
			}
			// Triangle 2
			bCollided = RayIntersectTriangle(Start, End, AddCollision.Corners[0], AddCollision.Corners[2], AddCollision.Corners[3], IntersectPoint);
			if (bCollided)
			{
				LandscapeRenderAddCollision = &AddCollision;
				break;
			}
		}

		if (bCollided &&
			CurrentToolTarget.LandscapeInfo.IsValid())
		{
			ALandscapeProxy* Proxy = CurrentToolTarget.LandscapeInfo.Get()->GetCurrentLevelLandscapeProxy(true);
			if (Proxy)
			{
				OutHitLocation = Proxy->LandscapeActorToWorld().InverseTransformPosition(IntersectPoint);
				return true;
			}
		}
	}

	return false;
}

bool FEdModeLandscape::LandscapePlaneTrace(FEditorViewportClient* ViewportClient, const FPlane& Plane, FVector& OutHitLocation)
{
	int32 MouseX = ViewportClient->Viewport->GetMouseX();
	int32 MouseY = ViewportClient->Viewport->GetMouseY();

	return LandscapePlaneTrace(ViewportClient, MouseX, MouseY, Plane, OutHitLocation);
}

bool FEdModeLandscape::LandscapePlaneTrace(FEditorViewportClient* ViewportClient, int32 MouseX, int32 MouseY, const FPlane& Plane, FVector& OutHitLocation)
{
	// Cache a copy of the world pointer
	UWorld* World = ViewportClient->GetWorld();
	// Compute a world space ray from the screen space mouse coordinates
	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		ViewportClient->Viewport,
		ViewportClient->GetScene(),
		ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(ViewportClient->IsRealtime()));
	FSceneView* View = ViewportClient->CalcSceneView(&ViewFamily);
	FViewportCursorLocation MouseViewportRay(View, ViewportClient, MouseX, MouseY);

	FVector Start = MouseViewportRay.GetOrigin();
	FVector End = Start + WORLD_MAX * MouseViewportRay.GetDirection();

	OutHitLocation = FMath::LinePlaneIntersection(Start, End, Plane);

	return true;
}

namespace
{
	const int32 SelectionSizeThresh = 2 * 256 * 256;
	FORCEINLINE bool IsSlowSelect(ULandscapeInfo* LandscapeInfo)
	{
		if (LandscapeInfo)
		{
			int32 MinX = MAX_int32, MinY = MAX_int32, MaxX = MIN_int32, MaxY = MIN_int32;
			LandscapeInfo->GetSelectedExtent(MinX, MinY, MaxX, MaxY);
			return (MinX != MAX_int32 && ((MaxX - MinX) * (MaxY - MinY)));
		}
		return false;
	}
};

EEditAction::Type FEdModeLandscape::GetActionEditDuplicate()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDuplicate();
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditDelete()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditDelete();
		}

		if (Result == EEditAction::Skip)
		{
			// Prevent deleting Gizmo during LandscapeEdMode
			if (CurrentGizmoActor.IsValid())
			{
				if (CurrentGizmoActor->IsSelected())
				{
					if (GEditor->GetSelectedActors()->Num() > 1)
					{
						GEditor->GetSelectedActors()->Deselect(CurrentGizmoActor.Get());
						Result = EEditAction::Skip;
					}
					else
					{
						Result = EEditAction::Halt;
					}
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditCut()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCut();
		}
	}

	if (Result == EEditAction::Skip)
	{
		// Special case: we don't want the 'normal' cut operation to be possible at all while in this mode, 
		// so we need to stop evaluating the others in-case they come back as true.
		return EEditAction::Halt;
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditCopy()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditCopy();
		}

		if (Result == EEditAction::Skip)
		{
			if (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo || GLandscapeEditRenderMode & (ELandscapeEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetLandscapeInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

EEditAction::Type FEdModeLandscape::GetActionEditPaste()
{
	EEditAction::Type Result = EEditAction::Skip;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->GetActionEditPaste();
		}

		if (Result == EEditAction::Skip)
		{
			if (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo || GLandscapeEditRenderMode & (ELandscapeEditRenderMode::Select))
			{
				if (CurrentGizmoActor.IsValid() && GizmoBrush && CurrentGizmoActor->TargetLandscapeInfo)
				{
					Result = EEditAction::Process;
				}
			}
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditDuplicate()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDuplicate();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditDelete()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditDelete();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditCut()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCut();
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditCopy()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditCopy();
		}

		if (!Result)
		{
			bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetLandscapeInfo);
			if (IsSlowTask)
			{
				GWarn->BeginSlowTask(LOCTEXT("BeginFitGizmoAndCopy", "Fit Gizmo to Selected Region and Copy Data..."), true);
			}

			FScopedTransaction Transaction(LOCTEXT("LandscapeGizmo_Copy", "Copy landscape data to Gizmo"));
			CurrentGizmoActor->Modify();
			CurrentGizmoActor->FitToSelection();
			CopyDataToGizmo();
			SetCurrentTool(FName("CopyPaste"));

			if (IsSlowTask)
			{
				GWarn->EndSlowTask();
			}

			Result = true;
		}
	}

	return Result;
}

bool FEdModeLandscape::ProcessEditPaste()
{
	if (!IsEditingEnabled())
	{
		return true;
	}

	bool Result = false;

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		if (CurrentTool != NULL)
		{
			Result = CurrentTool->ProcessEditPaste();
		}

		if (!Result)
		{
			bool IsSlowTask = IsSlowSelect(CurrentGizmoActor->TargetLandscapeInfo);
			if (IsSlowTask)
			{
				GWarn->BeginSlowTask(LOCTEXT("BeginPasteGizmoDataTask", "Paste Gizmo Data..."), true);
			}
			PasteDataFromGizmo();
			SetCurrentTool(FName("CopyPaste"));
			if (IsSlowTask)
			{
				GWarn->EndSlowTask();
			}

			Result = true;
		}
	}

	return Result;
}

bool FEdModeLandscape::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return false;
	}

	// Override Click Input for Splines Tool
	if (CurrentTool && CurrentTool->HandleClick(HitProxy, Click))
	{
		return true;
	}

	return false;
}

/** FEdMode: Called when a key is pressed */
bool FEdModeLandscape::InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		if (Key == EKeys::LeftMouseButton)
		{
			// Press mouse button
			if (Event == IE_Pressed && !IsAltDown(Viewport))
			{
				// See if we clicked on a new landscape handle..
				int32 HitX = Viewport->GetMouseX();
				int32 HitY = Viewport->GetMouseY();
				HHitProxy*	HitProxy = Viewport->GetHitProxy(HitX, HitY);
				if (HitProxy)
				{
					if (HitProxy->IsA(HNewLandscapeGrabHandleProxy::StaticGetType()))
					{
						HNewLandscapeGrabHandleProxy* EdgeProxy = (HNewLandscapeGrabHandleProxy*)HitProxy;
						DraggingEdge = EdgeProxy->Edge;
						DraggingEdge_Remainder = 0;

						return false; // false to let FEditorViewportClient.InputKey start mouse tracking and enable InputDelta() so we can use it
					}
				}
			}
			else if (Event == IE_Released)
			{
				if (DraggingEdge)
				{
					DraggingEdge = ELandscapeEdge::None;
					DraggingEdge_Remainder = 0;

					return false; // false to let FEditorViewportClient.InputKey end mouse tracking
				}
			}
		}
	}
	else
	{
		// Override Key Input for Selection Brush
		if (CurrentBrush)
		{
			TOptional<bool> BrushKeyOverride = CurrentBrush->InputKey(ViewportClient, Viewport, Key, Event);
			if (BrushKeyOverride.IsSet())
			{
				return BrushKeyOverride.GetValue();
			}
		}

		if (CurrentTool && CurrentTool->InputKey(ViewportClient, Viewport, Key, Event) == true)
		{
			return true;
		}

		// Require Ctrl or not as per user preference
		ELandscapeFoliageEditorControlType LandscapeEditorControlType = GetDefault<ULevelEditorViewportSettings>()->LandscapeEditorControlType;

		// HACK - Splines tool has not yet been updated to support not using ctrl
		if (CurrentBrush->GetBrushType() == ELandscapeBrushType::Splines)
		{
			LandscapeEditorControlType = ELandscapeFoliageEditorControlType::RequireCtrl;
		}

		if (Key == EKeys::LeftMouseButton && Event == IE_Pressed)
		{
			// Only activate tool if we're not already moving the camera and we're not trying to drag a transform widget
			// Not using "if (!ViewportClient->IsMovingCamera())" because it's wrong in ortho viewports :D
			bool bMovingCamera = Viewport->KeyState(EKeys::MiddleMouseButton) || Viewport->KeyState(EKeys::RightMouseButton) || IsAltDown(Viewport);

			if ((Viewport->IsPenActive() && Viewport->GetTabletPressure() > 0.0f) ||
				(!bMovingCamera && ViewportClient->GetCurrentWidgetAxis() == EAxisList::None &&
					((LandscapeEditorControlType == ELandscapeFoliageEditorControlType::IgnoreCtrl) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl   && IsCtrlDown(Viewport)) ||
					 (LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireNoCtrl && !IsCtrlDown(Viewport)))))
			{
				if (CurrentTool && (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA || CurrentToolTarget.TargetType != ELandscapeToolTargetType::Invalid))
				{
					FVector HitLocation;
					if (LandscapeMouseTrace(ViewportClient, HitLocation))
					{
						if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap && CurrentToolTarget.LayerInfo == NULL)
						{
							FMessageDialog::Open(EAppMsgType::Ok,
								NSLOCTEXT("UnrealEd", "LandscapeNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer."));
							// TODO: FName to LayerInfo: do we want to create the layer info here?
							//if (FMessageDialog::Open(EAppMsgType::YesNo, NSLOCTEXT("UnrealEd", "LandscapeNeedToCreateLayerInfo", "This layer has no layer info assigned yet. You must create or assign a layer info before you can paint this layer.")) == EAppReturnType::Yes)
							//{
							//	CurrentToolTarget.LandscapeInfo->LandscapeProxy->CreateLayerInfo(*CurrentToolTarget.PlaceholderLayerName.ToString());
							//}
						}
						else
						{
							bToolActive = CurrentTool->BeginTool(ViewportClient, CurrentToolTarget, HitLocation);
							return bToolActive;
						}
					}
				}
				return true;
			}
		}

		if (Key == EKeys::LeftMouseButton ||
			(LandscapeEditorControlType == ELandscapeFoliageEditorControlType::RequireCtrl && (Key == EKeys::LeftControl || Key == EKeys::RightControl)))
		{
			if (Event == IE_Released && CurrentTool && bToolActive)
			{
				//Set the cursor position to that of the slate cursor so it wont snap back
				Viewport->SetPreCaptureMousePosFromSlateCursor();
				CurrentTool->EndTool(ViewportClient);
				bToolActive = false;
				return true;
			}
		}

		// Change Brush Size
		if ((Event == IE_Pressed || Event == IE_Repeat) && (Key == EKeys::LeftBracket || Key == EKeys::RightBracket))
		{
			if (CurrentBrush->GetBrushType() == ELandscapeBrushType::Component)
			{
				int32 Radius = UISettings->BrushComponentSize;
				if (Key == EKeys::LeftBracket)
				{
					--Radius;
				}
				else
				{
					++Radius;
				}
				Radius = (int32)FMath::Clamp(Radius, 1, 64);
				UISettings->BrushComponentSize = Radius;
			}
			else
			{
				float Radius = UISettings->BrushRadius;
				float SliderMin = 0.0f;
				float SliderMax = 8192.0f;
				float LogPosition = FMath::Clamp(Radius / SliderMax, 0.0f, 1.0f);
				float Diff = 0.05f; //6.0f / SliderMax;
				if (Key == EKeys::LeftBracket)
				{
					Diff = -Diff;
				}

				float NewValue = Radius*(1.0f + Diff);

				if (Key == EKeys::LeftBracket)
				{
					NewValue = FMath::Min(NewValue, Radius - 1.0f);
				}
				else
				{
					NewValue = FMath::Max(NewValue, Radius + 1.0f);
				}

				NewValue = (int32)FMath::Clamp(NewValue, SliderMin, SliderMax);
				// convert from Exp scale to linear scale
				//float LinearPosition = 1.0f - FMath::Pow(1.0f - LogPosition, 1.0f / 3.0f);
				//LinearPosition = FMath::Clamp(LinearPosition + Diff, 0.0f, 1.0f);
				//float NewValue = FMath::Clamp((1.0f - FMath::Pow(1.0f - LinearPosition, 3.0f)) * SliderMax, SliderMin, SliderMax);
				//float NewValue = FMath::Clamp((SliderMax - SliderMin) * LinearPosition + SliderMin, SliderMin, SliderMax);

				UISettings->BrushRadius = NewValue;
			}
			return true;
		}

		// Prev tool 
		if (Event == IE_Pressed && Key == EKeys::Comma)
		{
			if (CurrentTool && bToolActive)
			{
				CurrentTool->EndTool(ViewportClient);
				bToolActive = false;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Max(OldToolIndex - 1, 0);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}

		// Next tool 
		if (Event == IE_Pressed && Key == EKeys::Period)
		{
			if (CurrentTool && bToolActive)
			{
				CurrentTool->EndTool(ViewportClient);
				bToolActive = false;
			}

			int32 OldToolIndex = CurrentToolMode->ValidTools.Find(CurrentTool->GetToolName());
			int32 NewToolIndex = FMath::Min(OldToolIndex + 1, CurrentToolMode->ValidTools.Num() - 1);
			SetCurrentTool(CurrentToolMode->ValidTools[NewToolIndex]);

			return true;
		}
	}

	return false;
}

/** FEdMode: Called when mouse drag input is applied */
bool FEdModeLandscape::InputDelta(FEditorViewportClient* InViewportClient, FViewport* InViewport, FVector& InDrag, FRotator& InRot, FVector& InScale)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		if (InViewportClient->GetCurrentWidgetAxis() != EAxisList::None)
		{
			FVector DeltaScale = InScale;
			DeltaScale.X = DeltaScale.Y = (FMath::Abs(InScale.X) > FMath::Abs(InScale.Y)) ? InScale.X : InScale.Y;

			UISettings->Modify();
			UISettings->NewLandscape_Location += InDrag;
			UISettings->NewLandscape_Rotation += InRot;
			UISettings->NewLandscape_Scale += DeltaScale;

			return true;
		}
		else if (DraggingEdge != ELandscapeEdge::None)
		{
			FVector HitLocation;
			LandscapePlaneTrace(InViewportClient, FPlane(UISettings->NewLandscape_Location, FVector(0, 0, 1)), HitLocation);

			FTransform Transform(UISettings->NewLandscape_Rotation, UISettings->NewLandscape_Location, UISettings->NewLandscape_Scale * UISettings->NewLandscape_QuadsPerSection * UISettings->NewLandscape_SectionsPerComponent);
			HitLocation = Transform.InverseTransformPosition(HitLocation);

			UISettings->Modify();
			switch (DraggingEdge)
			{
			case ELandscapeEdge::X_Negative:
			case ELandscapeEdge::X_Negative_Y_Negative:
			case ELandscapeEdge::X_Negative_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewLandscape_ComponentCount.X;
				const int32 Delta = FMath::RoundToInt(HitLocation.X + (float)InitialComponentCountX / 2);
				UISettings->NewLandscape_ComponentCount.X = InitialComponentCountX - Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.X - InitialComponentCountX;
				UISettings->NewLandscape_Location -= Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			case ELandscapeEdge::X_Positive:
			case ELandscapeEdge::X_Positive_Y_Negative:
			case ELandscapeEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountX = UISettings->NewLandscape_ComponentCount.X;
				int32 Delta = FMath::RoundToInt(HitLocation.X - (float)InitialComponentCountX / 2);
				UISettings->NewLandscape_ComponentCount.X = InitialComponentCountX + Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.X - InitialComponentCountX;
				UISettings->NewLandscape_Location += Transform.TransformVector(FVector(((float)ActualDelta / 2), 0, 0));
			}
				break;
			}

			switch (DraggingEdge)
			{
			case ELandscapeEdge::Y_Negative:
			case ELandscapeEdge::X_Negative_Y_Negative:
			case ELandscapeEdge::X_Positive_Y_Negative:
			{
				const int32 InitialComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y + (float)InitialComponentCountY / 2);
				UISettings->NewLandscape_ComponentCount.Y = InitialComponentCountY - Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewLandscape_Location -= Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			case ELandscapeEdge::Y_Positive:
			case ELandscapeEdge::X_Negative_Y_Positive:
			case ELandscapeEdge::X_Positive_Y_Positive:
			{
				const int32 InitialComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
				int32 Delta = FMath::RoundToInt(HitLocation.Y - (float)InitialComponentCountY / 2);
				UISettings->NewLandscape_ComponentCount.Y = InitialComponentCountY + Delta;
				UISettings->NewLandscape_ClampSize();
				const int32 ActualDelta = UISettings->NewLandscape_ComponentCount.Y - InitialComponentCountY;
				UISettings->NewLandscape_Location += Transform.TransformVector(FVector(0, (float)ActualDelta / 2, 0));
			}
				break;
			}

			return true;
		}
	}

	if (CurrentTool && CurrentTool->InputDelta(InViewportClient, InViewport, InDrag, InRot, InScale))
	{
		return true;
	}

	return false;
}

void FEdModeLandscape::SetCurrentToolMode(FName ToolModeName, bool bRestoreCurrentTool /*= true*/)
{
	if (CurrentToolMode == NULL || ToolModeName != CurrentToolMode->ToolModeName)
	{
		for (int32 i = 0; i < LandscapeToolModes.Num(); ++i)
		{
			if (LandscapeToolModes[i].ToolModeName == ToolModeName)
			{
				CurrentToolMode = &LandscapeToolModes[i];
				if (bRestoreCurrentTool)
				{
					if (CurrentToolMode->CurrentToolName == NAME_None)
					{
						CurrentToolMode->CurrentToolName = CurrentToolMode->ValidTools[0];
					}
					SetCurrentTool(CurrentToolMode->CurrentToolName);
				}
				break;
			}
		}
	}
}

void FEdModeLandscape::SetCurrentTool(FName ToolName)
{
	// Several tools have identically named versions for sculpting and painting
	// Prefer the one with the same target type as the current mode

	int32 BackupToolIndex = INDEX_NONE;
	int32 ToolIndex = INDEX_NONE;
	for (int32 i = 0; i < LandscapeTools.Num(); ++i)
	{
		FLandscapeTool* Tool = LandscapeTools[i].Get();
		if (ToolName == Tool->GetToolName())
		{
			if ((Tool->GetSupportedTargetTypes() & CurrentToolMode->SupportedTargetTypes) != 0)
			{
				ToolIndex = i;
				break;
			}
			else if (BackupToolIndex == INDEX_NONE)
			{
				BackupToolIndex = i;
			}
		}
	}

	if (ToolIndex == INDEX_NONE)
	{
		checkf(BackupToolIndex != INDEX_NONE, TEXT("Tool '%s' not found, please check name is correct!"), *ToolName.ToString());
		ToolIndex = BackupToolIndex;
	}
	check(ToolIndex != INDEX_NONE);

	SetCurrentTool(ToolIndex);
}

void FEdModeLandscape::SetCurrentTool(int32 ToolIndex)
{
	if (CurrentTool)
	{
		CurrentTool->PreviousBrushIndex = CurrentBrushSetIndex;
		if (CurrentTool)
		{
			CurrentTool->ExitTool();
		}
	}
	CurrentToolIndex = LandscapeTools.IsValidIndex(ToolIndex) ? ToolIndex : 0;
	CurrentTool = LandscapeTools[CurrentToolIndex].Get();
	if (!CurrentToolMode->ValidTools.Contains(CurrentTool->GetToolName()))
	{
		// if tool isn't valid for this mode then automatically switch modes
		// this mostly happens with shortcut keys
		bool bFoundValidMode = false;
		for (int32 i = 0; i < LandscapeToolModes.Num(); ++i)
		{
			if (LandscapeToolModes[i].ValidTools.Contains(CurrentTool->GetToolName()))
			{
				SetCurrentToolMode(LandscapeToolModes[i].ToolModeName, false);
				bFoundValidMode = true;
				break;
			}
		}
		check(bFoundValidMode);
	}

	// Set target type appropriate for tool
	if (CurrentTool->GetSupportedTargetTypes() == ELandscapeToolTargetTypeMask::NA)
	{
		CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
		CurrentToolTarget.LayerInfo = nullptr;
		CurrentToolTarget.LayerName = NAME_None;
	}
	else
	{
		const uint8 TargetTypeMask = CurrentToolMode->SupportedTargetTypes & CurrentTool->GetSupportedTargetTypes();
		checkSlow(TargetTypeMask != 0);

		if ((TargetTypeMask & ELandscapeToolTargetTypeMask::FromType(CurrentToolTarget.TargetType)) == 0)
		{
			auto filter = [TargetTypeMask](const TSharedRef<FLandscapeTargetListInfo>& Target){ return (TargetTypeMask & ELandscapeToolTargetTypeMask::FromType(Target->TargetType)) != 0; };
			const TSharedRef<FLandscapeTargetListInfo>* Target = LandscapeTargetList.FindByPredicate(filter);
			if (Target != nullptr)
			{
				check(CurrentToolTarget.LandscapeInfo == (*Target)->LandscapeInfo);
				CurrentToolTarget.TargetType = (*Target)->TargetType;
				CurrentToolTarget.LayerInfo = (*Target)->LayerInfoObj;
				CurrentToolTarget.LayerName = (*Target)->LayerName;
			}
			else // can happen with for example paint tools if there are no paint layers defined
			{
				CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
				CurrentToolTarget.LayerInfo = nullptr;
				CurrentToolTarget.LayerName = NAME_None;
			}
		}
	}

	if (CurrentTool)
	{
		CurrentTool->EnterTool();

		CurrentTool->SetEditRenderType();
		//bool MaskEnabled = CurrentTool->SupportsMask() && CurrentToolTarget.LandscapeInfo.IsValid() && CurrentToolTarget.LandscapeInfo->SelectedRegion.Num();
	}
	CurrentToolMode->CurrentToolName = CurrentTool->GetToolName();

	// Set Brush
	if (!LandscapeBrushSets.IsValidIndex(CurrentTool->PreviousBrushIndex))
	{
		SetCurrentBrushSet(CurrentTool->ValidBrushes[0]);
	}
	else
	{
		SetCurrentBrushSet(CurrentTool->PreviousBrushIndex);
	}

	// Update GizmoActor Landscape Target (is this necessary?)
	if (CurrentGizmoActor.IsValid() && CurrentToolTarget.LandscapeInfo.IsValid())
	{
		CurrentGizmoActor->SetTargetLandscape(CurrentToolTarget.LandscapeInfo.Get());
	}

	if (Toolkit.IsValid())
	{
		StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->NotifyToolChanged();
	}
}

void FEdModeLandscape::SetCurrentBrushSet(FName BrushSetName)
{
	for (int32 BrushIndex = 0; BrushIndex < LandscapeBrushSets.Num(); BrushIndex++)
	{
		if (BrushSetName == LandscapeBrushSets[BrushIndex].BrushSetName)
		{
			SetCurrentBrushSet(BrushIndex);
			return;
		}
	}
}

void FEdModeLandscape::SetCurrentBrushSet(int32 BrushSetIndex)
{
	if (CurrentBrushSetIndex != BrushSetIndex)
	{
		LandscapeBrushSets[CurrentBrushSetIndex].PreviousBrushIndex = LandscapeBrushSets[CurrentBrushSetIndex].Brushes.IndexOfByKey(CurrentBrush);

		CurrentBrushSetIndex = BrushSetIndex;
		if (CurrentTool)
		{
			CurrentTool->PreviousBrushIndex = BrushSetIndex;
		}

		SetCurrentBrush(LandscapeBrushSets[CurrentBrushSetIndex].PreviousBrushIndex);
	}
}

void FEdModeLandscape::SetCurrentBrush(FName BrushName)
{
	for (int32 BrushIndex = 0; BrushIndex < LandscapeBrushSets[CurrentBrushSetIndex].Brushes.Num(); BrushIndex++)
	{
		if (BrushName == LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex]->GetBrushName())
		{
			SetCurrentBrush(BrushIndex);
			return;
		}
	}
}

void FEdModeLandscape::SetCurrentBrush(int32 BrushIndex)
{
	if (CurrentBrush != LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex])
	{
		CurrentBrush->LeaveBrush();
		CurrentBrush = LandscapeBrushSets[CurrentBrushSetIndex].Brushes[BrushIndex];
		CurrentBrush->EnterBrush();

		if (Toolkit.IsValid())
		{
			StaticCastSharedPtr<FLandscapeToolKit>(Toolkit)->NotifyBrushChanged();
		}
	}
}

const TArray<TSharedRef<FLandscapeTargetListInfo>>& FEdModeLandscape::GetTargetList()
{
	return LandscapeTargetList;
}

const TArray<FLandscapeListInfo>& FEdModeLandscape::GetLandscapeList()
{
	return LandscapeList;
}

void FEdModeLandscape::AddLayerInfo(ULandscapeLayerInfoObject* LayerInfo)
{
	if (CurrentToolTarget.LandscapeInfo.IsValid() && CurrentToolTarget.LandscapeInfo->GetLayerInfoIndex(LayerInfo) == INDEX_NONE)
	{
		ALandscapeProxy* Proxy = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy();
		CurrentToolTarget.LandscapeInfo->Layers.Add(FLandscapeInfoLayerSettings(LayerInfo, Proxy));
		UpdateTargetList();
	}
}

int32 FEdModeLandscape::UpdateLandscapeList()
{
	LandscapeList.Empty();

	if (!CurrentGizmoActor.IsValid())
	{
		ALandscapeGizmoActiveActor* GizmoActor = NULL;
		for (TActorIterator<ALandscapeGizmoActiveActor> It(GetWorld()); It; ++It)
		{
			GizmoActor = *It;
			break;
		}
	}

	int32 CurrentIndex = INDEX_NONE;
	UWorld* World = GetWorld();
	
	if (World)
	{
		int32 Index = 0;
		auto& LandscapeInfoMap = GetLandscapeInfoMap(World);

		for (auto It = LandscapeInfoMap.Map.CreateIterator(); It; ++It)
		{
			ULandscapeInfo* LandscapeInfo = It.Value();
			if (LandscapeInfo && !LandscapeInfo->IsPendingKill())
			{
				ALandscapeProxy* LandscapeProxy = LandscapeInfo->GetLandscapeProxy();
				if (LandscapeProxy)
				{
					if (CurrentToolTarget.LandscapeInfo == LandscapeInfo)
					{
						CurrentIndex = Index;

						// Update GizmoActor Landscape Target (is this necessary?)
						if (CurrentGizmoActor.IsValid())
						{
							CurrentGizmoActor->SetTargetLandscape(LandscapeInfo);
						}
					}

					int32 MinX, MinY, MaxX, MaxY;
					int32 Width = 0, Height = 0;
					if (LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
					{
						Width = MaxX - MinX + 1;
						Height = MaxY - MinY + 1;
					}

					LandscapeList.Add(FLandscapeListInfo(*LandscapeProxy->GetName(), LandscapeInfo,
						LandscapeInfo->ComponentSizeQuads, LandscapeInfo->ComponentNumSubsections, Width, Height));
					Index++;
				}
			}
		}
	}

	if (CurrentIndex == INDEX_NONE)
	{
		if (LandscapeList.Num() > 0)
		{
			if (CurrentTool != NULL)
			{
				CurrentBrush->LeaveBrush();
				CurrentTool->ExitTool();
			}
			CurrentToolTarget.LandscapeInfo = LandscapeList[0].Info;
			CurrentIndex = 0;
			if (CurrentTool != NULL)
			{
				CurrentTool->EnterTool();
				CurrentBrush->EnterBrush();
			}
		}
		else
		{
			CurrentToolTarget.LandscapeInfo = NULL;
		}
	}

	return CurrentIndex;
}

void FEdModeLandscape::UpdateTargetList()
{
	LandscapeTargetList.Empty();

	if (CurrentToolTarget.LandscapeInfo.IsValid() &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy())
	{
		CachedLandscapeMaterial = CurrentToolTarget.LandscapeInfo->GetLandscapeProxy()->GetLandscapeMaterial();

		bool bFoundSelected = false;

		// Add heightmap
		LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(LOCTEXT("Heightmap", "Heightmap"), ELandscapeToolTargetType::Heightmap, CurrentToolTarget.LandscapeInfo.Get())));

		if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Heightmap)
		{
			bFoundSelected = true;
		}

		// Add visibility
		FLandscapeInfoLayerSettings VisibilitySettings(ALandscapeProxy::VisibilityLayer, CurrentToolTarget.LandscapeInfo->GetLandscapeProxy());
		LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(LOCTEXT("Visibility", "Visibility"), ELandscapeToolTargetType::Visibility, VisibilitySettings)));

		if (CurrentToolTarget.TargetType == ELandscapeToolTargetType::Visibility)
		{
			bFoundSelected = true;
		}

		// Add layers
		UTexture2D* ThumbnailWeightmap = NULL;
		UTexture2D* ThumbnailHeightmap = NULL;

		for (auto It = CurrentToolTarget.LandscapeInfo->Layers.CreateIterator(); It; It++)
		{
			FLandscapeInfoLayerSettings& LayerSettings = *It;

			FName LayerName = LayerSettings.GetLayerName();

			if (LayerSettings.LayerInfoObj == ALandscapeProxy::VisibilityLayer)
			{
				// Already handled above
				continue;
			}

			if (!bFoundSelected &&
				CurrentToolTarget.TargetType == ELandscapeToolTargetType::Weightmap &&
				CurrentToolTarget.LayerInfo == LayerSettings.LayerInfoObj &&
				CurrentToolTarget.LayerName == LayerSettings.LayerName)
			{
				bFoundSelected = true;
			}

			// Ensure thumbnails are up valid
			if (LayerSettings.ThumbnailMIC == NULL)
			{
				if (ThumbnailWeightmap == NULL)
				{
					ThumbnailWeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailWeightmap.LandscapeThumbnailWeightmap"), NULL, LOAD_None, NULL);
				}
				if (ThumbnailHeightmap == NULL)
				{
					ThumbnailHeightmap = LoadObject<UTexture2D>(NULL, TEXT("/Engine/EditorLandscapeResources/LandscapeThumbnailHeightmap.LandscapeThumbnailHeightmap"), NULL, LOAD_None, NULL);
				}

				// Construct Thumbnail MIC
				UMaterialInterface* LandscapeMaterial = LayerSettings.Owner ? LayerSettings.Owner->GetLandscapeMaterial() : UMaterial::GetDefaultMaterial(MD_Surface);
				LayerSettings.ThumbnailMIC = ALandscapeProxy::GetLayerThumbnailMIC(LandscapeMaterial, LayerName, ThumbnailWeightmap, ThumbnailHeightmap, LayerSettings.Owner);
			}

			// Add the layer
			LandscapeTargetList.Add(MakeShareable(new FLandscapeTargetListInfo(FText::FromName(LayerName), ELandscapeToolTargetType::Weightmap, LayerSettings)));
		}

		if (!bFoundSelected)
		{
			CurrentToolTarget.TargetType = ELandscapeToolTargetType::Invalid;
			CurrentToolTarget.LayerInfo = nullptr;
			CurrentToolTarget.LayerName = NAME_None;
		}
	}

	TargetsListUpdated.Broadcast();
}

FEdModeLandscape::FTargetsListUpdated FEdModeLandscape::TargetsListUpdated;

void FEdModeLandscape::OnWorldChange()
{
	UpdateLandscapeList();
	UpdateTargetList();

	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None &&
		CurrentToolTarget.LandscapeInfo == NULL)
	{
		RequestDeletion();
	}
}

void FEdModeLandscape::OnMaterialCompilationFinished(UMaterialInterface* MaterialInterface)
{
	if (CurrentToolTarget.LandscapeInfo.IsValid() &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy() != NULL &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy()->GetLandscapeMaterial() != NULL &&
		CurrentToolTarget.LandscapeInfo->GetLandscapeProxy()->GetLandscapeMaterial()->IsDependent(MaterialInterface))
	{
		CurrentToolTarget.LandscapeInfo->UpdateLayerInfoMap();
		UpdateTargetList();
	}
}

/** FEdMode: Render the mesh paint tool */
void FEdModeLandscape::Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI)
{
	/** Call parent implementation */
	FEdMode::Render(View, Viewport, PDI);

	if (!IsEditingEnabled())
	{
		return;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		static const float        CornerSize = 0.33f;
		static const FLinearColor CornerColour(1.0f, 1.0f, 0.5f);
		static const FLinearColor EdgeColour(1.0f, 1.0f, 0.0f);
		static const FLinearColor ComponentBorderColour(0.0f, 0.85f, 0.0f);
		static const FLinearColor SectionBorderColour(0.0f, 0.4f, 0.0f);
		static const FLinearColor InnerColour(0.0f, 0.25f, 0.0f);

		const ELevelViewportType ViewportType = ((FEditorViewportClient*)Viewport->GetClient())->ViewportType;

		const int32 ComponentCountX = UISettings->NewLandscape_ComponentCount.X;
		const int32 ComponentCountY = UISettings->NewLandscape_ComponentCount.Y;
		const int32 QuadsPerComponent = UISettings->NewLandscape_SectionsPerComponent * UISettings->NewLandscape_QuadsPerSection;
		const float ComponentSize = QuadsPerComponent;
		const FVector Offset = UISettings->NewLandscape_Location + FTransform(UISettings->NewLandscape_Rotation, FVector::ZeroVector, UISettings->NewLandscape_Scale).TransformVector(FVector(-ComponentCountX * ComponentSize / 2, -ComponentCountY * ComponentSize / 2, 0));
		const FTransform Transform = FTransform(UISettings->NewLandscape_Rotation, Offset, UISettings->NewLandscape_Scale);

		if (NewLandscapePreviewMode == ENewLandscapePreviewMode::ImportLandscape)
		{
			const TArray<uint16>& ImportHeights = UISettings->GetImportLandscapeData();
			if (ImportHeights.Num() != 0)
			{
				const float InvQuadsPerComponent = 1.0f / (float)QuadsPerComponent;
				const int32 SizeX = ComponentCountX * QuadsPerComponent + 1;
				const int32 SizeY = ComponentCountY * QuadsPerComponent + 1;
				const int32 ImportSizeX = UISettings->ImportLandscape_Width;
				const int32 ImportSizeY = UISettings->ImportLandscape_Height;
				const int32 OffsetX = (SizeX - ImportSizeX) / 2;
				const int32 OffsetY = (SizeY - ImportSizeY) / 2;

				for (int32 ComponentY = 0; ComponentY < ComponentCountY; ComponentY++)
				{
					const int32 Y0 = ComponentY * QuadsPerComponent;
					const int32 Y1 = (ComponentY + 1) * QuadsPerComponent;

					const int32 ImportY0 = FMath::Clamp<int32>(Y0 - OffsetY, 0, ImportSizeY - 1);
					const int32 ImportY1 = FMath::Clamp<int32>(Y1 - OffsetY, 0, ImportSizeY - 1);

					for (int32 ComponentX = 0; ComponentX < ComponentCountX; ComponentX++)
					{
						const int32 X0 = ComponentX * QuadsPerComponent;
						const int32 X1 = (ComponentX + 1) * QuadsPerComponent;
						const int32 ImportX0 = FMath::Clamp<int32>(X0 - OffsetX, 0, ImportSizeX - 1);
						const int32 ImportX1 = FMath::Clamp<int32>(X1 - OffsetX, 0, ImportSizeX - 1);
						const float Z00 = ((float)ImportHeights[ImportX0 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z01 = ((float)ImportHeights[ImportX0 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z10 = ((float)ImportHeights[ImportX1 + ImportY0 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;
						const float Z11 = ((float)ImportHeights[ImportX1 + ImportY1 * ImportSizeX] - 32768.0f) * LANDSCAPE_ZSCALE;

						if (ComponentX == 0)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X0, Y1, Z01)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentX == ComponentCountX - 1)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X1, Y0, Z10)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						if (ComponentY == 0)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Negative));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0, Z00)), Transform.TransformPosition(FVector(X1, Y0, Z10)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}

						if (ComponentY == ComponentCountY - 1)
						{
							PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Positive));
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
							PDI->SetHitProxy(NULL);
						}
						else
						{
							PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y1, Z01)), Transform.TransformPosition(FVector(X1, Y1, Z11)), ComponentBorderColour, SDPG_Foreground);
						}

						// intra-component lines - too slow for big landscapes
						/*
						for (int32 x=1;x<QuadsPerComponent;x++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0+x, Y0, FMath::Lerp(Z00,Z10,(float)x*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X0+x, Y1, FMath::Lerp(Z01,Z11,(float)x*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						for (int32 y=1;y<QuadsPerComponent;y++)
						{
						PDI->DrawLine(Transform.TransformPosition(FVector(X0, Y0+y, FMath::Lerp(Z00,Z01,(float)y*InvQuadsPerComponent))), Transform.TransformPosition(FVector(X1, Y0+y, FMath::Lerp(Z10,Z11,(float)y*InvQuadsPerComponent))), ComponentBorderColour, SDPG_World);
						}
						*/
					}
				}
			}
		}
		else //if (NewLandscapePreviewMode == ENewLandscapePreviewMode::NewLandscape)
		{
			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 x = 0; x <= ComponentCountX * QuadsPerComponent; x++)
				{
					if (x == 0)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x == ComponentCountX * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, CornerSize * ComponentSize, 0)), Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(x, (ComponentCountY - CornerSize) * ComponentSize, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (x % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (x % UISettings->NewLandscape_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(x, 0, 0)), Transform.TransformPosition(FVector(x, ComponentCountY * ComponentSize, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(0, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * QuadsPerComponent, ComponentCountY * ComponentSize, 0)), EdgeColour, SDPG_World);
			}

			if (ViewportType == LVT_Perspective || ViewportType == LVT_OrthoXY || ViewportType == LVT_OrthoNegativeXY)
			{
				for (int32 y = 0; y <= ComponentCountY * QuadsPerComponent; y++)
				{
					if (y == 0)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Negative));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y == ComponentCountY * QuadsPerComponent)
					{
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Negative_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector(CornerSize * ComponentSize, y, 0)), Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), EdgeColour, SDPG_Foreground);
						PDI->SetHitProxy(new HNewLandscapeGrabHandleProxy(ELandscapeEdge::X_Positive_Y_Positive));
						PDI->DrawLine(Transform.TransformPosition(FVector((ComponentCountX - CornerSize) * ComponentSize, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), CornerColour, SDPG_Foreground);
						PDI->SetHitProxy(NULL);
					}
					else if (y % QuadsPerComponent == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), ComponentBorderColour, SDPG_Foreground);
					}
					else if (y % UISettings->NewLandscape_QuadsPerSection == 0)
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), SectionBorderColour, SDPG_Foreground);
					}
					else
					{
						PDI->DrawLine(Transform.TransformPosition(FVector(0, y, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, y, 0)), InnerColour, SDPG_World);
					}
				}
			}
			else
			{
				// Don't allow dragging to resize in side-view
				// and there's no point drawing the inner lines as only the outer is visible
				PDI->DrawLine(Transform.TransformPosition(FVector(0, 0, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, 0, 0)), EdgeColour, SDPG_World);
				PDI->DrawLine(Transform.TransformPosition(FVector(0, ComponentCountY * QuadsPerComponent, 0)), Transform.TransformPosition(FVector(ComponentCountX * ComponentSize, ComponentCountY * QuadsPerComponent, 0)), EdgeColour, SDPG_World);
			}
		}

		return;
	}

	if (LandscapeRenderAddCollision)
	{
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[0], LandscapeRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[3], LandscapeRenderAddCollision->Corners[1], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[1], LandscapeRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);

		PDI->DrawLine(LandscapeRenderAddCollision->Corners[0], LandscapeRenderAddCollision->Corners[2], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[2], LandscapeRenderAddCollision->Corners[3], FColor(0, 255, 128), SDPG_Foreground);
		PDI->DrawLine(LandscapeRenderAddCollision->Corners[3], LandscapeRenderAddCollision->Corners[0], FColor(0, 255, 128), SDPG_Foreground);
	}

	// Override Rendering for Splines Tool
	if (CurrentTool)
	{
		CurrentTool->Render(View, Viewport, PDI);
	}
}

/** FEdMode: Render HUD elements for this tool */
void FEdModeLandscape::DrawHUD(FEditorViewportClient* ViewportClient, FViewport* Viewport, const FSceneView* View, FCanvas* Canvas)
{

}

bool FEdModeLandscape::UsesTransformWidget() const
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool && CurrentTool->UsesTransformWidget())
	{
		return true;
	}

	return (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected() && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo));
}

bool FEdModeLandscape::ShouldDrawWidget() const
{
	return UsesTransformWidget();
}

EAxisList::Type FEdModeLandscape::GetWidgetAxisToDraw(FWidget::EWidgetMode InWidgetMode) const
{
	if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None)
	{
		// Override Widget for Splines Tool
		if (CurrentTool)
		{
			return CurrentTool->GetWidgetAxisToDraw(InWidgetMode);
		}
	}

	switch (InWidgetMode)
	{
	case FWidget::WM_Translate:
		return EAxisList::XYZ;
	case FWidget::WM_Rotate:
		return EAxisList::Z;
	case FWidget::WM_Scale:
		return EAxisList::XYZ;
	default:
		return EAxisList::None;
	}
}

FVector FEdModeLandscape::GetWidgetLocation() const
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return UISettings->NewLandscape_Location;
	}

	if (CurrentGizmoActor.IsValid() && (GLandscapeEditRenderMode & ELandscapeEditRenderMode::Gizmo) && CurrentGizmoActor->IsSelected())
	{
		if (CurrentGizmoActor->TargetLandscapeInfo && CurrentGizmoActor->TargetLandscapeInfo->GetLandscapeProxy())
		{
			// Apply Landscape transformation when it is available
			ULandscapeInfo* Info = CurrentGizmoActor->TargetLandscapeInfo;
			return CurrentGizmoActor->GetActorLocation()
				+ FRotationMatrix(Info->GetLandscapeProxy()->GetActorRotation()).TransformPosition(FVector(0, 0, CurrentGizmoActor->GetLength()));
		}
		return CurrentGizmoActor->GetActorLocation();
	}

	// Override Widget for Splines Tool
	if (CurrentTool)
	{
		return CurrentTool->GetWidgetLocation();
	}

	return FEdMode::GetWidgetLocation();
}

bool FEdModeLandscape::GetCustomDrawingCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		InMatrix = FRotationMatrix(UISettings->NewLandscape_Rotation);
		return true;
	}

	// Override Widget for Splines Tool
	if (CurrentTool)
	{
		InMatrix = CurrentTool->GetWidgetRotation();
		return true;
	}

	return false;
}

bool FEdModeLandscape::GetCustomInputCoordinateSystem(FMatrix& InMatrix, void* InData)
{
	return GetCustomDrawingCoordinateSystem(InMatrix, InData);
}

/** FEdMode: Handling SelectActor */
bool FEdModeLandscape::Select(AActor* InActor, bool bInSelected)
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return false;
	}

	if (InActor->IsA<ALandscapeProxy>() && bInSelected)
	{
		ALandscapeProxy* Landscape = CastChecked<ALandscapeProxy>(InActor);

		if (CurrentToolTarget.LandscapeInfo != Landscape->GetLandscapeInfo())
		{
			CurrentToolTarget.LandscapeInfo = Landscape->GetLandscapeInfo();
			UpdateTargetList();
		}
	}

	if (IsSelectionAllowed(InActor, bInSelected))
	{
		return false;
	}
	else if (!bInSelected)
	{
		return false;
	}
	return true;
}

/** FEdMode: Check to see if an actor can be selected in this mode - no side effects */
bool FEdModeLandscape::IsSelectionAllowed(AActor* InActor, bool bInSelection) const
{
	if (!IsEditingEnabled())
	{
		return false;
	}

	if (NewLandscapePreviewMode != ENewLandscapePreviewMode::None)
	{
		return false;
	}

	// Override Selection for Splines Tool
	if (CurrentTool && CurrentTool->OverrideSelection())
	{
		return CurrentTool->IsSelectionAllowed(InActor, bInSelection);
	}

	if (InActor->IsA(ALandscapeProxy::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALandscapeGizmoActor::StaticClass()))
	{
		return true;
	}
	else if (InActor->IsA(ALight::StaticClass()))
	{
		return true;
	}

	return false;
}

/** FEdMode: Called when the currently selected actor has changed */
void FEdModeLandscape::ActorSelectionChangeNotify()
{
	if (CurrentGizmoActor.IsValid() && CurrentGizmoActor->IsSelected())
	{
		GEditor->SelectNone(false, true);
		GEditor->SelectActor(CurrentGizmoActor.Get(), true, false, true);
	}
	/*
		USelection* EditorSelection = GEditor->GetSelectedActors();
		for ( FSelectionIterator Itor(EditorSelection) ; Itor ; ++Itor )
		{
		if (((*Itor)->IsA(ALandscapeGizmoActor::StaticClass())) )
		{
		bIsGizmoSelected = true;
		break;
		}
		}
	*/
}

void FEdModeLandscape::ActorMoveNotify()
{
	//GUnrealEd->UpdateFloatingPropertyWindows();
}

void FEdModeLandscape::PostUndo()
{
	OnWorldChange();
}

/** Forces all level editor viewports to realtime mode */
void FEdModeLandscape::ForceRealTimeViewports(const bool bEnable, const bool bStoreCurrentState)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	TSharedPtr<ILevelEditor> LevelEditor = LevelEditorModule.GetFirstLevelEditor();
	if (LevelEditor.IsValid())
	{
		TArray<TSharedPtr<ILevelViewport>> Viewports = LevelEditor->GetViewports();
		for (const TSharedPtr<ILevelViewport>& ViewportWindow : Viewports)
		{
			if (ViewportWindow.IsValid())
			{
				FEditorViewportClient& Viewport = ViewportWindow->GetLevelViewportClient();
				if (bEnable)
				{
					Viewport.SetRealtime(bEnable, bStoreCurrentState);
					Viewport.SetGameView(false);
				}
				else
				{
					const bool bAllowDisable = true;
					Viewport.RestoreRealtime(bAllowDisable);
				}
			}
		}
	}
}

void FEdModeLandscape::ReimportData(const FLandscapeTargetListInfo& TargetInfo)
{
	const FString& SourceFilePath = TargetInfo.ReimportFilePath();
	if (SourceFilePath.Len())
	{
		ImportData(TargetInfo, SourceFilePath);
	}
	else
	{
		FMessageDialog::Open(EAppMsgType::Ok, NSLOCTEXT("UnrealEd", "LandscapeReImport_BadFileName", "Reimport Source Filename is invalid"));
	}
}

void FEdModeLandscape::ImportData(const FLandscapeTargetListInfo& TargetInfo, const FString& Filename)
{
	ULandscapeInfo* LandscapeInfo = TargetInfo.LandscapeInfo.Get();
	if (LandscapeInfo != NULL)
	{
		TArray<uint8> Data;
		int32 MinX, MinY, MaxX, MaxY;
		if (FFileHelper::LoadFileToArray(Data, *Filename) &&
			LandscapeInfo->GetLandscapeExtent(MinX, MinY, MaxX, MaxY))
		{
			const int32 SizeX = (1 + MaxX - MinX);
			const int32 SizeY = (1 + MaxY - MinY);

			IImageWrapperPtr ImageWrapper = nullptr;
			const TArray<uint8>* RawData = nullptr; // Pointer to actual used data, actual store could be in Data or ImageWrapper

			if (Filename.EndsWith(".png"))
			{
				IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
				ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);

				if (ImageWrapper->SetCompressed(Data.GetData(), Data.Num()))
				{
					const int32 ImportSizeX = ImageWrapper->GetWidth();
					const int32 ImportSizeY = ImageWrapper->GetHeight();

					if (ImportSizeX != SizeX || ImportSizeY != SizeY)
					{
						FFormatNamedArguments Args;
						Args.Add(TEXT("FileSizeX"), ImportSizeX);
						Args.Add(TEXT("FileSizeY"), ImportSizeY);
						Args.Add(TEXT("LandscapeSizeX"), SizeX);
						Args.Add(TEXT("LandscapeSizeY"), SizeY);

						auto Result = FMessageDialog::Open(EAppMsgType::OkCancel,
							FText::Format(NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_SizeMismatch", "File's size ({FileSizeX}\u00D7{FileSizeY}) does not match with current Landscape extent ({LandscapeSizeX}\u00D7{LandscapeSizeY}), if you continue it will be padded/clipped to fit"), Args));

						if (Result != EAppReturnType::Ok)
						{
							return;
						}
					}

					if (TargetInfo.TargetType == ELandscapeToolTargetType::Heightmap)
					{
						if (ImageWrapper->GetFormat() != ERGBFormat::Gray)
						{
							EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel, NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_HeightmapFileColorPng", "The Heightmap file appears to be a color png, grayscale is expected. The import *can* continue, but the result may not be what you expect..."));
							if (Result != EAppReturnType::Ok)
							{
								return;
							}
						}
						else if (ImageWrapper->GetBitDepth() != 16)
						{
							EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel, NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_HeightmapFileLowBitDepth", "The Heightmap file appears to be an 8-bit png, 16-bit is preferred. The import *can* continue, but the result may be lower quality than desired."));
							if (Result != EAppReturnType::Ok)
							{
								return;
							}
						}

						if (ImageWrapper->GetBitDepth() <= 8)
						{
							ImageWrapper->GetRaw(ERGBFormat::Gray, 8, RawData);
							Data.Reset();
							Data.SetNumUninitialized(RawData->Num() * sizeof(uint16));
							uint16* DataPtr = (uint16*)Data.GetData();
							for (int32 i = 0; i < RawData->Num(); i++)
							{
								DataPtr[i] = (*RawData)[i] * 0x101; // Expand to 16-bit
							}
							RawData = &Data;
						}
						else
						{
							ImageWrapper->GetRaw(ERGBFormat::Gray, 16, RawData);
						}
					}
					else
					{
						if (ImageWrapper->GetFormat() != ERGBFormat::Gray)
						{
							EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel, NSLOCTEXT("LandscapeEditor.NewLandscape", "Import_LayerColorPng", "The Layer file appears to be a color png, grayscale is expected. The import *can* continue, but the result may not be what you expect..."));
							if (Result != EAppReturnType::Ok)
							{
								return;
							}
						}

						ImageWrapper->GetRaw(ERGBFormat::Gray, 8, RawData);
					}

					if (ImportSizeX != SizeX || ImportSizeY != SizeY)
					{
						// Cloned from FLandscapeEditorDetailCustomization_NewLandscape.OnCreateButtonClicked
						// so that reimports behave the same as the initial import :)

						const int32 OffsetX = (SizeX - ImportSizeX) / 2;
						const int32 OffsetY = (SizeY - ImportSizeY) / 2;

						if (TargetInfo.TargetType == ELandscapeToolTargetType::Heightmap)
						{
							// have to use a TempData because RawData could be pointing to Data
							TArray<uint8> TempData;
							TempData.SetNumUninitialized(SizeX * SizeY * sizeof(uint16));

							LandscapeEditorUtils::ExpandData<uint16>((uint16*)TempData.GetData(), (const uint16*)RawData->GetData(),
								0, 0, ImportSizeX - 1, ImportSizeY - 1,
								-OffsetX, -OffsetY, SizeX - OffsetX - 1, SizeY - OffsetY - 1);

							Data = MoveTemp(TempData);
							RawData = &Data;
						}
						else
						{
							Data = LandscapeEditorUtils::ExpandData<uint8>(*RawData,
								0, 0, ImportSizeX - 1, ImportSizeY - 1,
								-OffsetX, -OffsetY, SizeX - OffsetX - 1, SizeY - OffsetY - 1);
							RawData = &Data;
						}
					}
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(LOCTEXT("ImportData_CorruptPngFile", "Import of {0} failed (corrupt png?)"), FText::FromString(Filename)));
					return;
				}
			}
			else
			{
				// I would love to deprecate the raw/r8/r16 support for landscape, r16 doesn't even handle endianness issues...
				RawData = &Data;
			}

			if (TargetInfo.TargetType == ELandscapeToolTargetType::Heightmap)
			{
				if (RawData->Num() == SizeX * SizeY * sizeof(uint16))
				{
					FHeightmapAccessor<false> HeightmapAccessor(LandscapeInfo);
					HeightmapAccessor.SetData(MinX, MinY, MaxX, MaxY, (const uint16*)RawData->GetData());
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("UnrealEd", "LandscapeReImport_BadFileSize", "{0}'s filesize does not match with current Landscape extent"), FText::FromString(Filename)));
				}
			}
			else
			{
				if (RawData->Num() == SizeX * SizeY)
				{
					FAlphamapAccessor<false, true> AlphamapAccessor(LandscapeInfo, TargetInfo.LayerInfoObj.Get());
					AlphamapAccessor.SetData(MinX, MinY, MaxX, MaxY, RawData->GetData(), ELandscapeLayerPaintingRestriction::None);
				}
				else
				{
					FMessageDialog::Open(EAppMsgType::Ok,
						FText::Format(NSLOCTEXT("UnrealEd", "LandscapeReImport_BadFileSize", "{0}'s filesize does not match with current Landscape extent"), FText::FromString(Filename)));
				}
			}
		}
	}
}

ALandscape* FEdModeLandscape::ChangeComponentSetting(int32 NumComponentsX, int32 NumComponentsY, int32 NumSubsections, int32 SubsectionSizeQuads, bool bResample)
{
	check(NumComponentsX > 0);
	check(NumComponentsY > 0);
	check(NumSubsections > 0);
	check(SubsectionSizeQuads > 0);

	ALandscape* Landscape = NULL;

	ULandscapeInfo* LandscapeInfo = CurrentToolTarget.LandscapeInfo.Get();
	if (ensure(LandscapeInfo != NULL))
	{
		int32 OldMinX, OldMinY, OldMaxX, OldMaxY;
		if (LandscapeInfo->GetLandscapeExtent(OldMinX, OldMinY, OldMaxX, OldMaxY))
		{
			ALandscapeProxy* OldLandscapeProxy = LandscapeInfo->GetLandscapeProxy();

			const int32 OldVertsX = OldMaxX - OldMinX + 1;
			const int32 OldVertsY = OldMaxY - OldMinY + 1;
			const int32 NewVertsX = NumComponentsX * NumSubsections * SubsectionSizeQuads + 1;
			const int32 NewVertsY = NumComponentsY * NumSubsections * SubsectionSizeQuads + 1;

			FLandscapeEditDataInterface LandscapeEdit(LandscapeInfo);
			TArray<uint16> HeightData;
			TArray<FLandscapeImportLayerInfo> ImportLayerInfos;
			FVector LandscapeOffset = FVector::ZeroVector;
			float LandscapeScaleFactor = 1.0f;

			if (bResample)
			{
				HeightData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint16));

				// GetHeightData alters its args, so make temp copies to avoid screwing things up
				int32 TMinX = OldMinX, TMinY = OldMinY, TMaxX = OldMaxX, TMaxY = OldMaxY;
				LandscapeEdit.GetHeightData(TMinX, TMinY, TMaxX, TMaxY, HeightData.GetData(), 0);

				HeightData = LandscapeEditorUtils::ResampleData(HeightData,
					OldVertsX, OldVertsY, NewVertsX, NewVertsY);

				for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
				{
					if (LayerSettings.LayerInfoObj != NULL)
					{
						auto ImportLayerInfo = new(ImportLayerInfos)FLandscapeImportLayerInfo(LayerSettings);
						ImportLayerInfo->LayerData.AddZeroed(OldVertsX * OldVertsY * sizeof(uint8));

						TMinX = OldMinX; TMinY = OldMinY; TMaxX = OldMaxX; TMaxY = OldMaxY;
						LandscapeEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

						ImportLayerInfo->LayerData = LandscapeEditorUtils::ResampleData(ImportLayerInfo->LayerData,
							OldVertsX, OldVertsY,
							NewVertsX, NewVertsY);
					}
				}

				LandscapeScaleFactor = (float)OldLandscapeProxy->ComponentSizeQuads / (NumSubsections * SubsectionSizeQuads);
			}
			else
			{
				const int32 NewMinX = OldMinX + (OldVertsX - NewVertsX) / 2;
				const int32 NewMinY = OldMinY + (OldVertsY - NewVertsY) / 2;
				const int32 NewMaxX = NewMinX + NewVertsX - 1;
				const int32 NewMaxY = NewMinY + NewVertsY - 1;
				const int32 RequestedMinX = FMath::Max(OldMinX, NewMinX);
				const int32 RequestedMinY = FMath::Max(OldMinY, NewMinY);
				const int32 RequestedMaxX = FMath::Min(OldMaxX, NewMaxX);
				const int32 RequestedMaxY = FMath::Min(OldMaxY, NewMaxY);

				const int32 RequestedVertsX = RequestedMaxX - RequestedMinX + 1;
				const int32 RequestedVertsY = RequestedMaxY - RequestedMinY + 1;

				HeightData.AddZeroed(RequestedVertsX * RequestedVertsY * sizeof(uint16));

				// GetHeightData alters its args, so make temp copies to avoid screwing things up
				int32 TMinX = RequestedMinX, TMinY = RequestedMinY, TMaxX = RequestedMaxX, TMaxY = RequestedMaxY;
				LandscapeEdit.GetHeightData(TMinX, TMinY, TMaxX, OldMaxY, HeightData.GetData(), 0);

				HeightData = LandscapeEditorUtils::ExpandData(HeightData,
					RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY,
					NewMinX, NewMinY, NewMaxX, NewMaxY);

				for (const FLandscapeInfoLayerSettings& LayerSettings : LandscapeInfo->Layers)
				{
					if (LayerSettings.LayerInfoObj != NULL)
					{
						auto ImportLayerInfo = new(ImportLayerInfos)FLandscapeImportLayerInfo(LayerSettings);
						ImportLayerInfo->LayerData.AddZeroed(NewVertsX * NewVertsY * sizeof(uint8));

						TMinX = RequestedMinX; TMinY = RequestedMinY; TMaxX = RequestedMaxX; TMaxY = RequestedMaxY;
						LandscapeEdit.GetWeightData(LayerSettings.LayerInfoObj, TMinX, TMinY, TMaxX, TMaxY, ImportLayerInfo->LayerData.GetData(), 0);

						ImportLayerInfo->LayerData = LandscapeEditorUtils::ExpandData(ImportLayerInfo->LayerData,
							RequestedMinX, RequestedMinY, RequestedMaxX, RequestedMaxY,
							NewMinX, NewMinY, NewMaxX, NewMaxY);
					}
				}

				LandscapeOffset = FVector(NewMinX - OldMinX, NewMinY - OldMinY, 0) * OldLandscapeProxy->GetActorScale();
			}

			const FVector Location = OldLandscapeProxy->GetActorLocation() + LandscapeOffset;
			Landscape = OldLandscapeProxy->GetWorld()->SpawnActor<ALandscape>(Location, OldLandscapeProxy->GetActorRotation());

			const FVector OldScale = OldLandscapeProxy->GetActorScale();
			Landscape->SetActorRelativeScale3D(FVector(OldScale.X * LandscapeScaleFactor, OldScale.Y * LandscapeScaleFactor, OldScale.Z));

			Landscape->LandscapeMaterial = OldLandscapeProxy->LandscapeMaterial;
			Landscape->CollisionMipLevel = OldLandscapeProxy->CollisionMipLevel;
			Landscape->Import(FGuid::NewGuid(), NewVertsX, NewVertsY, NumSubsections*SubsectionSizeQuads, NumSubsections, SubsectionSizeQuads, HeightData.GetData(), *OldLandscapeProxy->ReimportHeightmapFilePath, ImportLayerInfos);

			Landscape->MaxLODLevel = OldLandscapeProxy->MaxLODLevel;
			Landscape->ExportLOD = OldLandscapeProxy->ExportLOD;
			Landscape->StaticLightingLOD = OldLandscapeProxy->StaticLightingLOD;
			Landscape->DefaultPhysMaterial = OldLandscapeProxy->DefaultPhysMaterial;
			Landscape->StreamingDistanceMultiplier = OldLandscapeProxy->StreamingDistanceMultiplier;
			Landscape->LandscapeHoleMaterial = OldLandscapeProxy->LandscapeHoleMaterial;
			Landscape->LODDistanceFactor = OldLandscapeProxy->LODDistanceFactor;
			Landscape->StaticLightingResolution = OldLandscapeProxy->StaticLightingResolution;
			Landscape->bCastStaticShadow = OldLandscapeProxy->bCastStaticShadow;
			Landscape->bCastShadowAsTwoSided = OldLandscapeProxy->bCastShadowAsTwoSided;
			Landscape->LightmassSettings = OldLandscapeProxy->LightmassSettings;
			Landscape->CollisionThickness = OldLandscapeProxy->CollisionThickness;
			Landscape->BodyInstance.SetCollisionProfileName(OldLandscapeProxy->BodyInstance.GetCollisionProfileName());
			if (Landscape->BodyInstance.DoesUseCollisionProfile() == false)
			{
				Landscape->BodyInstance.SetCollisionEnabled(OldLandscapeProxy->BodyInstance.GetCollisionEnabled());
				Landscape->BodyInstance.SetObjectType(OldLandscapeProxy->BodyInstance.GetObjectType());
				Landscape->BodyInstance.SetResponseToChannels(OldLandscapeProxy->BodyInstance.GetResponseToChannels());
			}
			Landscape->EditorLayerSettings = OldLandscapeProxy->EditorLayerSettings;
			Landscape->bUsedForNavigation = OldLandscapeProxy->bUsedForNavigation;
			Landscape->MaxPaintedLayersPerComponent = OldLandscapeProxy->MaxPaintedLayersPerComponent;

			// Clone landscape splines
			TLazyObjectPtr<ALandscape> OldLandscapeActor = LandscapeInfo->LandscapeActor;
			if (OldLandscapeActor.IsValid() && OldLandscapeActor->SplineComponent != NULL)
			{
				ULandscapeSplinesComponent* OldSplines = OldLandscapeActor->SplineComponent;
				ULandscapeSplinesComponent* NewSplines = DuplicateObject<ULandscapeSplinesComponent>(OldSplines, Landscape, *OldSplines->GetName());
				NewSplines->AttachTo(Landscape->GetRootComponent(), NAME_None, EAttachLocation::KeepWorldPosition);

				const FVector OldSplineScale = OldSplines->GetRelativeTransform().GetScale3D();
				NewSplines->SetRelativeScale3D(FVector(OldSplineScale.X / LandscapeScaleFactor, OldSplineScale.Y / LandscapeScaleFactor, OldSplineScale.Z));
				Landscape->SplineComponent = NewSplines;
				NewSplines->RegisterComponent();

				// TODO: Foliage on spline meshes
			}

			if (bResample)
			{
				// Remap foliage to the resampled components
				ULandscapeInfo* NewLandscapeInfo = Landscape->GetLandscapeInfo();
				for (const TPair<FIntPoint, ULandscapeComponent*>& Entry : LandscapeInfo->XYtoComponentMap)
				{
					ULandscapeComponent* NewComponent = NewLandscapeInfo->XYtoComponentMap.FindRef(Entry.Key);
					if (NewComponent)
					{
						ULandscapeHeightfieldCollisionComponent* OldCollisionComponent = Entry.Value->CollisionComponent.Get();
						ULandscapeHeightfieldCollisionComponent* NewCollisionComponent = NewComponent->CollisionComponent.Get();

						if (OldCollisionComponent && NewCollisionComponent)
						{
							AInstancedFoliageActor::MoveInstancesToNewComponent(OldCollisionComponent->GetWorld(), OldCollisionComponent, NewCollisionComponent);
							NewCollisionComponent->SnapFoliageInstances(FBox(FVector(-WORLD_MAX), FVector(WORLD_MAX)));
						}
					}
				}
			}
			else
			{
				// TODO: remap foliage when not resampling (i.e. when there isn't a 1:1 mapping between old and new component)
			}

			// Delete the old Landscape and all its proxies
			for (TActorIterator<ALandscapeProxy> It(OldLandscapeProxy->GetWorld()); It; ++It)
			{
				ALandscapeProxy* Proxy = (*It);
				if (Proxy && Proxy->LandscapeActor == OldLandscapeActor)
				{
					Proxy->Destroy();
				}
			}
			OldLandscapeProxy->Destroy();
		}
	}

	return Landscape;
}

ELandscapeEditingState FEdModeLandscape::GetEditingState() const
{
	UWorld* World = GetWorld();

	if (GEditor->bIsSimulatingInEditor)
	{
		return ELandscapeEditingState::SIEWorld;
	}
	else if (GEditor->PlayWorld != NULL)
	{
		return ELandscapeEditingState::PIEWorld;
	}
	else if (World == nullptr)
	{
		return ELandscapeEditingState::Unknown;
	}
	else if (World->FeatureLevel < ERHIFeatureLevel::SM4)
	{
		return ELandscapeEditingState::BadFeatureLevel;
	}
	else if (NewLandscapePreviewMode == ENewLandscapePreviewMode::None &&
		!CurrentToolTarget.LandscapeInfo.IsValid())
	{
		return ELandscapeEditingState::NoLandscape;
	}

	return ELandscapeEditingState::Enabled;
}

bool LandscapeEditorUtils::SetHeightmapData(ALandscapeProxy* Landscape, const TArray<uint16>& Data)
{
	FIntRect ComponentsRect = Landscape->GetBoundingRect() + Landscape->LandscapeSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FHeightmapAccessor<false> HeightmapAccessor(Landscape->GetLandscapeInfo());
		HeightmapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData());
		return true;
	}

	return false;
}

bool LandscapeEditorUtils::SetWeightmapData(ALandscapeProxy* Landscape, ULandscapeLayerInfoObject* LayerObject, const TArray<uint8>& Data)
{
	FIntRect ComponentsRect = Landscape->GetBoundingRect() + Landscape->LandscapeSectionOffset;

	if (Data.Num() == (1 + ComponentsRect.Width())*(1 + ComponentsRect.Height()))
	{
		FAlphamapAccessor<false, true> AlphamapAccessor(Landscape->GetLandscapeInfo(), LayerObject);
		AlphamapAccessor.SetData(ComponentsRect.Min.X, ComponentsRect.Min.Y, ComponentsRect.Max.X, ComponentsRect.Max.Y, Data.GetData(), ELandscapeLayerPaintingRestriction::None);
		return true;
	}

	return false;
}

FName FLandscapeTargetListInfo::GetLayerName() const
{
	return LayerInfoObj.IsValid() ? LayerInfoObj->LayerName : LayerName;
}

#undef LOCTEXT_NAMESPACE
