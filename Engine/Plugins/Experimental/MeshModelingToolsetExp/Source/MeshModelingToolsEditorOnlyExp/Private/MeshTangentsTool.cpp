// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshTangentsTool.h"
#include "InteractiveToolManager.h"
#include "ToolBuilderUtil.h"

#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMeshToMeshDescription.h"
#include "MeshDescription.h"
#include "ToolSetupUtil.h"
#include "ToolDataVisualizer.h"

#include "AssetUtils/MeshDescriptionUtil.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"

#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/MeshDescriptionCommitter.h"
#include "TargetInterfaces/MeshDescriptionProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "TargetInterfaces/StaticMeshBackedTarget.h"
#include "ModelingToolTargetUtil.h"
#include "ToolTargetManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(MeshTangentsTool)

using namespace UE::Geometry;

#define LOCTEXT_NAMESPACE "UMeshTangentsTool"

/*
 * ToolBuilder
 */

const FToolTargetTypeRequirements& UMeshTangentsToolBuilder::GetTargetRequirements() const
{
	static FToolTargetTypeRequirements TypeRequirements({
		UMaterialProvider::StaticClass(),
		UMeshDescriptionCommitter::StaticClass(),
		UMeshDescriptionProvider::StaticClass(),
		UPrimitiveComponentBackedTarget::StaticClass(),
		});
	return TypeRequirements;
}

USingleSelectionMeshEditingTool* UMeshTangentsToolBuilder::CreateNewTool(const FToolBuilderState& SceneState) const
{
	return NewObject<UMeshTangentsTool>(SceneState.ToolManager);
}

bool UMeshTangentsToolBuilder::CanBuildTool(const FToolBuilderState& SceneState) const
{
	return USingleSelectionMeshEditingToolBuilder::CanBuildTool(SceneState) &&
		SceneState.TargetManager->CountSelectedAndTargetableWithPredicate(SceneState, GetTargetRequirements(),
			[](UActorComponent& Component) { return !ToolBuilderUtil::IsVolume(Component); }) >= 1;
}

/*
 * Tool
 */
UMeshTangentsTool::UMeshTangentsTool()
{
}



void UMeshTangentsTool::Setup()
{
	UInteractiveTool::Setup();

	UE::ToolTarget::HideSourceObject(Target);

	// make our preview mesh
	AActor* TargetActor = UE::ToolTarget::GetTargetActor(Target);
	PreviewMesh = NewObject<UPreviewMesh>(this);
	PreviewMesh->bBuildSpatialDataStructure = false;
	PreviewMesh->CreateInWorld(TargetActor->GetWorld(), FTransform::Identity);
	PreviewMesh->SetTransform((FTransform)UE::ToolTarget::GetLocalToWorldTransform(Target));
	ToolSetupUtil::ApplyRenderingConfigurationToPreview(PreviewMesh, nullptr);
	// configure materials
	FComponentMaterialSet MaterialSet = UE::ToolTarget::GetMaterialSet(Target);
	PreviewMesh->SetMaterials(MaterialSet.Materials);
	// configure mesh
	PreviewMesh->SetTangentsMode(EDynamicMeshComponentTangentsMode::ExternallyProvided);
	FDynamicMesh3 InputMeshWithTangents = UE::ToolTarget::GetDynamicMeshCopy(Target, true);
	PreviewMesh->ReplaceMesh(MoveTemp(InputMeshWithTangents));

	// make a copy of initialized mesh and tangents
	PreviewMesh->ProcessMesh([&](const FDynamicMesh3& ReadMesh)
	{
		InputMesh = MakeShared<FDynamicMesh3, ESPMode::ThreadSafe>(ReadMesh);
		InitialTangents = MakeShared<FMeshTangentsf, ESPMode::ThreadSafe>(InputMesh.Get());
		InitialTangents->CopyTriVertexTangents(ReadMesh);
	});

	// initialize our properties
	Settings = NewObject<UMeshTangentsToolProperties>(this);
	Settings->RestoreProperties(this);
	AddToolPropertySource(Settings);

	Settings->WatchProperty(Settings->CalculationMethod, [this](EMeshTangentsType) { Compute->InvalidateResult(); });
	Settings->WatchProperty(Settings->LineLength, [this](float) { bLengthDirty = true; });
	Settings->WatchProperty(Settings->LineThickness, [this](float) { bThicknessDirty = true; });
	Settings->WatchProperty(Settings->bShowTangents, [this](bool) { bVisibilityChanged = true; });
	Settings->WatchProperty(Settings->bShowNormals, [this](bool) { bVisibilityChanged = true; });
	Settings->WatchProperty(Settings->bCompareWithMikkt, [this](bool) { ComputeMikkTDeviations(nullptr); });

	PreviewGeometry = NewObject<UPreviewGeometry>(this);
	PreviewGeometry->CreateInWorld(TargetActor->GetWorld(), PreviewMesh->GetTransform());

	Compute = MakeUnique<TGenericDataBackgroundCompute<FMeshTangentsd>>();
	Compute->Setup(this);
	Compute->OnOpCompleted.AddLambda([this](const UE::Geometry::TGenericDataOperator<UE::Geometry::FMeshTangentsd>* Op)
		{
			if (((FCalculateTangentsOp*)(Op))->bNoAttributesError)
			{
				bHasDisplayedNoAttributeError = true;
				GetToolManager()->DisplayMessage(
					LOCTEXT("TangentsNoAttributesError", "Error: Source mesh did not have tangents."),
					EToolMessageLevel::UserWarning);
			}
			else if (bHasDisplayedNoAttributeError)
			{
				bHasDisplayedNoAttributeError = false;
				GetToolManager()->DisplayMessage(
					FText(),
					EToolMessageLevel::UserWarning);
			}
		});
	Compute->OnResultUpdated.AddLambda( [this](const TUniquePtr<FMeshTangentsd>& NewResult) { OnTangentsUpdated(NewResult); } );
	Compute->InvalidateResult();

	SetToolDisplayName(LOCTEXT("ToolName", "Edit Tangents"));
	GetToolManager()->DisplayMessage(
		LOCTEXT("OnStartTool", "Configure or Recalculate Tangents on a Static Mesh Asset (disables autogenerated Tangents and Normals)"),
		EToolMessageLevel::UserNotification);
}



void UMeshTangentsTool::OnShutdown(EToolShutdownType ShutdownType)
{
	PreviewGeometry->Disconnect();
	PreviewMesh->Disconnect();

	Settings->SaveProperties(this);

	// Restore (unhide) the source meshes
	UE::ToolTarget::ShowSourceObject(Target);

	TUniquePtr<FMeshTangentsd> Tangents = Compute->Shutdown();
	if (ShutdownType == EToolShutdownType::Accept)
	{
		GetToolManager()->BeginUndoTransaction(LOCTEXT("UpdateTangents", "Update Tangents"));

		UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(UE::ToolTarget::GetTargetComponent(Target));
		if (StaticMeshComponent != nullptr)
		{
			UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
			if (ensure(StaticMesh != nullptr))
			{
				StaticMesh->Modify();

				// disable auto-generated normals and tangents build settings
				UE::MeshDescription::FStaticMeshBuildSettingChange SettingsChange;
				SettingsChange.AutoGeneratedNormals = UE::MeshDescription::EBuildSettingBoolChange::Disable;
				SettingsChange.AutoGeneratedTangents = UE::MeshDescription::EBuildSettingBoolChange::Disable;
				UE::MeshDescription::ConfigureBuildSettings(StaticMesh, 0, SettingsChange);
			}
		}

		Tangents->CopyToOverlays(*InputMesh);

		FConversionToMeshDescriptionOptions Options;
		Options.bUpdatePositions = false;
		Options.bUpdateNormals = true;
		Options.bUpdateTangents = true;
		UE::ToolTarget::CommitDynamicMeshUpdate(Target, *InputMesh, false, Options);

		GetToolManager()->EndUndoTransaction();
	}
}

void UMeshTangentsTool::OnTick(float DeltaTime)
{
	Compute->Tick(DeltaTime);

	if (bThicknessDirty || bLengthDirty || bVisibilityChanged)
	{
		UpdateVisualization(bThicknessDirty, bLengthDirty);
		bThicknessDirty = bLengthDirty = bVisibilityChanged = false;
	}
}


void UMeshTangentsTool::Render(IToolsContextRenderAPI* RenderAPI)
{
	if (Settings->bCompareWithMikkt && Deviations.Num() > 0)
	{
		FToolDataVisualizer Visualizer;
		Visualizer.BeginFrame(RenderAPI);
		Visualizer.SetTransform(PreviewMesh->GetTransform());
		for (const FMikktDeviation& ErrorPt : Deviations)
		{
			if (ErrorPt.MaxAngleDeg > Settings->CompareWithMikktThreshold)
			{
				Visualizer.DrawPoint(ErrorPt.VertexPos, FLinearColor(0.95f, 0.05f, 0.05f), 4.0f * Settings->LineThickness, false);
				Visualizer.DrawLine<FVector3f>(ErrorPt.VertexPos, ErrorPt.VertexPos + Settings->LineLength * ErrorPt.MikktTangent, FLinearColor(0.95f, 0.05f, 0.05f), 2.0f * Settings->LineThickness, false);
				Visualizer.DrawLine<FVector3f>(ErrorPt.VertexPos, ErrorPt.VertexPos + Settings->LineLength * ErrorPt.MikktBitangent, FLinearColor(0.05f, 0.95f, 0.05f), 2.0f * Settings->LineThickness, false);

				Visualizer.DrawLine<FVector3f>(ErrorPt.VertexPos, ErrorPt.VertexPos + (1.1f * Settings->LineLength) * ErrorPt.OtherTangent, FLinearColor(0.95f, 0.50f, 0.05f), Settings->LineThickness, false);
				Visualizer.DrawLine<FVector3f>(ErrorPt.VertexPos, ErrorPt.VertexPos + (1.1f * Settings->LineLength) * ErrorPt.OtherBitangent, FLinearColor(0.05f, 0.95f, 0.95f), Settings->LineThickness, false);
			}
		}
		Visualizer.EndFrame();
	}
}


bool UMeshTangentsTool::CanAccept() const
{
	return Super::CanAccept() && Compute->HaveValidResult();
}


TUniquePtr<TGenericDataOperator<FMeshTangentsd>> UMeshTangentsTool::MakeNewOperator()
{
	TUniquePtr<FCalculateTangentsOp> TangentsOp = MakeUnique<FCalculateTangentsOp>();

	TangentsOp->SourceMesh = InputMesh;
	TangentsOp->SourceTangents = InitialTangents;
	TangentsOp->CalculationMethod = Settings->CalculationMethod;

	return TangentsOp;
}


void UMeshTangentsTool::UpdateVisualization(bool bThicknessChanged, bool bLengthChanged)
{
	ULineSetComponent* TangentLines = PreviewGeometry->FindLineSet(TEXT("Tangents"));
	ULineSetComponent* NormalLines = PreviewGeometry->FindLineSet(TEXT("Normals"));
	if (TangentLines == nullptr || NormalLines == nullptr)
	{
		return;
	}

	if (bThicknessChanged)
	{
		float Thickness = Settings->LineThickness;
		TangentLines->SetAllLinesThickness(Thickness);
		NormalLines->SetAllLinesThickness(Thickness);
	}

	if (bLengthChanged)
	{
		float LineLength = Settings->LineLength;
		TangentLines->SetAllLinesLength(LineLength);
		NormalLines->SetAllLinesLength(LineLength);
	}

	PreviewGeometry->SetLineSetVisibility(TEXT("Tangents"), Settings->bShowTangents);
	PreviewGeometry->SetLineSetVisibility(TEXT("Normals"), Settings->bShowNormals);
}


void UMeshTangentsTool::OnTangentsUpdated(const TUniquePtr<FMeshTangentsd>& NewResult)
{
	const float LineLength = Settings->LineLength;
	const float Thickness = Settings->LineThickness;

	const TSet<int32> DegenerateTris = ComputeDegenerateTris();

	// update Tangents rendering line set
	PreviewGeometry->CreateOrUpdateLineSet(TEXT("Tangents"), InputMesh->MaxTriangleID(),
		[&](int32 Index, TArray<FRenderableLine>& Lines) 
	{

		if (InputMesh->IsTriangle(Index) && DegenerateTris.Contains(Index) == false)
		{
			FVector3d Verts[3];
			InputMesh->GetTriVertices(Index, Verts[0], Verts[1], Verts[2]);
			for (int j = 0; j < 3; ++j)
			{
				FVector3d Tangent, Bitangent;
				NewResult->GetPerTriangleTangent(Index, j, Tangent, Bitangent);

				Lines.Add(FRenderableLine((FVector)Verts[j], (FVector)Verts[j] + LineLength * (FVector)Tangent, FColor(240,15,15), Thickness));
				Lines.Add(FRenderableLine((FVector)Verts[j], (FVector)Verts[j] + LineLength * (FVector)Bitangent, FColor(15,240,15), Thickness));
			}
		}
	}, 6);


	// update Normals rendering line set
	const FDynamicMeshNormalOverlay* NormalOverlay = InputMesh->Attributes()->PrimaryNormals();
	PreviewGeometry->CreateOrUpdateLineSet(TEXT("Normals"), NormalOverlay->MaxElementID(),
		[&](int32 Index, TArray<FRenderableLine>& Lines) 
	{
		if (NormalOverlay->IsElement(Index))
		{
			FVector3f Normal = NormalOverlay->GetElement(Index);
			int32 ParentVtx = NormalOverlay->GetParentVertex(Index);
			FVector3f Position = (FVector3f)InputMesh->GetVertex(ParentVtx);
			Lines.Add(FRenderableLine((FVector)Position, (FVector)Position + LineLength * (FVector)Normal, FColor(15,15,240), Thickness));
		}
	}, 1);

	ComputeMikkTDeviations(&DegenerateTris);

	PreviewMesh->DeferredEditMesh([&](FDynamicMesh3& EditMesh)
	{
		NewResult.Get()->CopyToOverlays(EditMesh);
	}, false);
	PreviewMesh->NotifyDeferredEditCompleted(UPreviewMesh::ERenderUpdateMode::FastUpdate, EMeshRenderAttributeFlags::VertexNormals, false);

	UpdateVisualization(false, false);
}


TSet<int32> UMeshTangentsTool::ComputeDegenerateTris() const
{
	TSet<int32> DegenerateTris;
	if (!Settings->bShowDegenerates || (Settings->bCompareWithMikkt && Settings->CalculationMethod != EMeshTangentsType::MikkTSpace))
	{
		FMeshTangentsd DegenTangents(InputMesh.Get());
		DegenTangents.ComputeTriangleTangents(InputMesh->Attributes()->GetUVLayer(0));
		DegenerateTris = TSet<int32>(DegenTangents.GetDegenerateTris());
	}
	return DegenerateTris;
}


void UMeshTangentsTool::ComputeMikkTDeviations(const TSet<int32>* DegenerateTris)
{
	// calculate deviation between what we have and MikkT, if necessary
	Deviations.Reset();
	if (Settings->bCompareWithMikkt && Settings->CalculationMethod == EMeshTangentsType::FastMikkTSpace)
	{
		TSet<int32> TempDegenerateTris;
		if (DegenerateTris == nullptr)
		{
			TempDegenerateTris = ComputeDegenerateTris();
			DegenerateTris = &TempDegenerateTris;
		}

		FProgressCancel TmpCancel;
		FCalculateTangentsOp MikktOp;
		MikktOp.SourceMesh = InputMesh;
		MikktOp.CalculationMethod = EMeshTangentsType::MikkTSpace;
		MikktOp.CalculateResult(&TmpCancel);
		TUniquePtr<FMeshTangentsd> MikktTangents = MikktOp.ExtractResult();

		FCalculateTangentsOp NewOp;
		NewOp.SourceMesh = InputMesh;
		NewOp.CalculationMethod = EMeshTangentsType::FastMikkTSpace;
		NewOp.CalculateResult(&TmpCancel);
		TUniquePtr<FMeshTangentsd> NewTangents = NewOp.ExtractResult();

		for (int32 Index : InputMesh->TriangleIndicesItr())
		{
			if (DegenerateTris->Contains(Index) == false)
			{
				FVector3d Verts[3];
				InputMesh->GetTriVertices(Index, Verts[0], Verts[1], Verts[2]);
				for (int j = 0; j < 3; ++j)
				{
					FVector3f TangentMikkt, BitangentMikkt;
					MikktTangents->GetPerTriangleTangent<FVector3f, float>(Index, j, TangentMikkt, BitangentMikkt);
					UE::Geometry::Normalize(TangentMikkt);
					UE::Geometry::Normalize(BitangentMikkt);
					FVector3f TangentNew, BitangentNew;
					NewTangents->GetPerTriangleTangent<FVector3f, float>(Index, j, TangentNew, BitangentNew);
					UE::Geometry::Normalize(TangentNew); 
					UE::Geometry::Normalize(BitangentNew);
					ensure(UE::Geometry::IsNormalized(TangentMikkt) && UE::Geometry::IsNormalized(BitangentMikkt));
					ensure(UE::Geometry::IsNormalized(TangentNew) && UE::Geometry::IsNormalized(BitangentNew));
					float MaxAngleDeg = FMathf::Max(UE::Geometry::AngleD(TangentMikkt, TangentNew), UE::Geometry::AngleD(BitangentMikkt, BitangentNew));
					if (MaxAngleDeg > 0.5f)
					{
						FMikktDeviation Deviation{ MaxAngleDeg, Index, j, (FVector3f)Verts[j], TangentMikkt, BitangentMikkt, TangentNew, BitangentNew };
						Deviations.Add(Deviation);
					}
				}
			}
		}
	}
}


#undef LOCTEXT_NAMESPACE

