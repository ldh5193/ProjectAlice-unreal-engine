// Copyright Epic Games, Inc. All Rights Reserved.

#include "NearestNeighborEditorModel.h"
#include "IDetailsView.h"
#include "NearestNeighborModel.h"
#include "NearestNeighborModelInstance.h"
#include "NearestNeighborTrainingModel.h"
#include "NearestNeighborModelInputInfo.h"
#include "NearestNeighborEditorModelActor.h"
#include "NearestNeighborModelStyle.h"
#include "NearestNeighborGeomCacheSampler.h"
#include "Components/ExternalMorphSet.h"
#include "MLDeformerComponent.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerAsset.h"
#include "GeometryCache.h"
#include "GeometryCacheComponent.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/MorphTarget.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimInstance.h"
#include "BonePose.h"
#include "PackageTools.h"
#include "ObjectTools.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"


#define LOCTEXT_NAMESPACE "NearestNeighborEditorModel"

namespace UE::NearestNeighborModel
{
	using namespace UE::MLDeformer;

	FMLDeformerEditorModel* FNearestNeighborEditorModel::MakeInstance()
	{
		return new FNearestNeighborEditorModel();
	}

	void FNearestNeighborEditorModel::Init(const InitSettings& InitSettings)
	{
		FMLDeformerEditorModel::Init(InitSettings);
		InitInputInfo(Model->GetInputInfo());
	}

	FMLDeformerSampler* FNearestNeighborEditorModel::CreateSampler() const
	{
		FNearestNeighborGeomCacheSampler* NewSampler = new FNearestNeighborGeomCacheSampler();
		NewSampler->OnGetGeometryCache().BindLambda([this] { return GetGeomCacheModel()->GetGeometryCache(); });
		return NewSampler;
	}

	void FNearestNeighborEditorModel::OnPropertyChanged(FPropertyChangedEvent& PropertyChangedEvent)
	{
		const FProperty* Property = PropertyChangedEvent.Property;
		if (Property == nullptr)
		{
			return;
		}

		FMLDeformerMorphModelEditorModel::OnPropertyChanged(PropertyChangedEvent);

		if (Property->GetFName() == UMLDeformerModel::GetSkeletalMeshPropertyName() ||
			Property->GetFName() == UMLDeformerModel::GetAnimSequencePropertyName() ||
			Property->GetFName() == UMLDeformerGeomCacheModel::GetGeometryCachePropertyName() ||
			Property->GetFName() == UNearestNeighborModel::GetClothPartEditorDataPropertyName()
			|| (PropertyChangedEvent.MemberProperty != nullptr && PropertyChangedEvent.MemberProperty->GetFName() == UNearestNeighborModel::GetClothPartEditorDataPropertyName()))
		{
			GetNearestNeighborModel()->InvalidateClothPartData();
			GetEditor()->GetModelDetailsView()->ForceRefresh();
		}

		if (Property->GetFName() == UNearestNeighborModel::GetNearestNeighborDataPropertyName() || (PropertyChangedEvent.MemberProperty != nullptr && PropertyChangedEvent.MemberProperty->GetFName() == UNearestNeighborModel::GetNearestNeighborDataPropertyName()))
		{
			GetNearestNeighborModel()->InvalidateNearestNeighborData();
			GetEditor()->GetModelDetailsView()->ForceRefresh();
		}

		if (Property->GetFName() == UNearestNeighborModel::GetMorphCompressionLevelPropertyName() || Property->GetFName() == UNearestNeighborModel::GetMorphDeltaZeroThresholdPropertyName())
		{
			GetNearestNeighborModel()->InvalidateMorphTargetData();
			GetEditor()->GetModelDetailsView()->ForceRefresh();
		}
	}

	void FNearestNeighborEditorModel::OnPostTraining(ETrainingResult TrainingResult, bool bUsePartiallyTrainedWhenAborted)
	{
		FMLDeformerEditorModel::OnPostTraining(TrainingResult, bUsePartiallyTrainedWhenAborted);
		if (TrainingResult == ETrainingResult::Success || (TrainingResult == ETrainingResult::Aborted && bUsePartiallyTrainedWhenAborted))
		{
			GetNearestNeighborModel()->InvalidateNearestNeighborData();
			InitTestMLDeformerPreviousWeights();
			GetEditor()->GetModelDetailsView()->ForceRefresh();
		}
	}
	
	bool FNearestNeighborEditorModel::LoadTrainedNetwork() const
	{
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		if (NearestNeighborModel)
		{	
			const FString OnnxFile = GetTrainedNetworkOnnxFile();
			if (NearestNeighborModel->ShouldUseOptimizedNetwork())
			{
				const bool bSuccess = NearestNeighborModel->LoadOptimizedNetwork(OnnxFile);
				if (bSuccess)
				{
					UNearestNeighborModelInstance* ModelInstance = static_cast<UNearestNeighborModelInstance*>(GetTestMLDeformerModelInstance());
					if (ModelInstance)
					{
						ModelInstance->InitOptimizedNetworkInstance();
						NearestNeighborModel->SetUseOptimizedNetwork(true);
						return true;
					}
				}
			}
		}
		return false;
	}

	bool FNearestNeighborEditorModel::IsTrained() const
	{
		const UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		if (NearestNeighborModel)
		{
			return NearestNeighborModel->DoesUseOptimizedNetwork() && NearestNeighborModel->GetOptimizedNetwork() != nullptr;
		}
		return false;
	}

	FString FNearestNeighborEditorModel::GetTrainedNetworkOnnxFile() const
	{
		const UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		if (NearestNeighborModel)
		{
			return NearestNeighborModel->GetModelDir() + TEXT("/NearestNeighborModel.onnx");
		}
		else
		{
			return FMLDeformerEditorModel::GetTrainedNetworkOnnxFile();
		}
	}

	int32 FNearestNeighborEditorModel::GetNumTrainingFrames() const
	{
		if (NumTrainingFramesOverride >= 0)
		{
			return NumTrainingFramesOverride;
		}
		else
		{
			return FMLDeformerMorphModelEditorModel::GetNumTrainingFrames();
		}
	}

	UNearestNeighborModelVizSettings* FNearestNeighborEditorModel::GetNearestNeighborModelVizSettings() const
	{
		return Cast<UNearestNeighborModelVizSettings>(GetMorphModel()->GetVizSettings()); 
	}

	void FNearestNeighborEditorModel::CreateActors(const TSharedRef<IPersonaPreviewScene>& InPersonaPreviewScene)
	{
		FMLDeformerMorphModelEditorModel::CreateActors(InPersonaPreviewScene);

		UWorld* World = InPersonaPreviewScene->GetWorld();
		CreateNearestNeighborActors(World);
		EditorWorld = World;
	}

	void FNearestNeighborEditorModel::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
	{
		FMLDeformerEditorModel::Tick(ViewportClient, DeltaTime);
		if (!NearestNeighborActors.IsEmpty())
		{
			UNearestNeighborModelVizSettings* NNViz = GetNearestNeighborModelVizSettings();
			const float Offset = NNViz->GetNearestNeighborActorsOffset();
			for (FNearestNeighborEditorModelActor* NearestNeighborActor : NearestNeighborActors)
			{
				if (NearestNeighborActor)
				{
					NearestNeighborActor->TickNearestNeighborActor();
					NearestNeighborActor->SetMeshOffsetFactor(Offset);
				}
			}
			const UNearestNeighborModelInstance* ModelInstance = static_cast<UNearestNeighborModelInstance*>(GetTestMLDeformerModelInstance());
			if (ModelInstance)
			{
				NNViz->SetNearestNeighborIds(ModelInstance->GetNearestNeighborIds());
			}
		}
	}

	void FNearestNeighborEditorModel::CreateNearestNeighborActors(UWorld* World, int32 StartIndex)
	{
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		const int32 NumParts = NearestNeighborModel->GetNumParts();

		if (StartIndex == 0)
		{
			NearestNeighborActors.Reset();
		}
		NearestNeighborActors.SetNumZeroed(NumParts);

		const UNearestNeighborModelVizSettings* NNViz = GetNearestNeighborModelVizSettings();
		const float Offset = NNViz->GetNearestNeighborActorsOffset();
		
		for(int32 PartId = StartIndex; PartId < NumParts; PartId++)
		{
			UGeometryCache* GeomCache = NearestNeighborModel->GetNearestNeighborCache(PartId);
			const FLinearColor LabelColor = FNearestNeighborModelEditorStyle::Get().GetColor("NearestNeighborModel.NearestNeighborActors.LabelColor");
			const FLinearColor WireframeColor = FNearestNeighborModelEditorStyle::Get().GetColor("NearestNeighborModel.NearestNeighborActors.WireframeColor");
			CreateGeomCacheActor(
				World, 
				ActorID_NearestNeighborActors,
				*FString::Printf(TEXT("NearestNeighbors%d"), PartId), 
				GeomCache, 
				LabelColor,					
				WireframeColor,
				LOCTEXT("TestNearestNeighborLabelText", "Nearest Neigbors"),
				false);
			FNearestNeighborEditorModelActor* NearestNeighborActor = static_cast<FNearestNeighborEditorModelActor*>(EditorActors.Last());
			UMLDeformerComponent* MLDeformerComponent = GetTestMLDeformerComponent();
			NearestNeighborActor->InitNearestNeighborActor(PartId, MLDeformerComponent);
			NearestNeighborActor->SetMeshOffsetFactor(Offset);
			NearestNeighborActors[PartId] = NearestNeighborActor;
		}
	}

	FMLDeformerEditorActor* FNearestNeighborEditorModel::CreateEditorActor(const FMLDeformerEditorActor::FConstructSettings& Settings) const
	{
		return new FNearestNeighborEditorModelActor(Settings);
	}

	void FNearestNeighborEditorModel::InitInputInfo(UMLDeformerInputInfo* InputInfo)
	{
		FMLDeformerEditorModel::InitInputInfo(InputInfo);
		UNearestNeighborModelInputInfo* NearestNeighborInputInfo = static_cast<UNearestNeighborModelInputInfo*>(InputInfo);
		NearestNeighborInputInfo->InitRefBoneRotations(Model->GetSkeletalMesh());
	}

	ETrainingResult FNearestNeighborEditorModel::Train()
	{
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		MorphTargetUpdateResult = EUpdateResult::SUCCESS;
		if (!NearestNeighborModel->IsClothPartDataValid())
		{
			MorphTargetUpdateResult |= NearestNeighborModel->UpdateClothPartData();
			if (HasError(MorphTargetUpdateResult))
			{
				UpdateNearestNeighborActors();
				return ETrainingResult::FailOnData;
			}
		}

		return TrainModel<UNearestNeighborTrainingModel>(this);
	}

	uint8 FNearestNeighborEditorModel::SetSamplerPartData(const int32 PartId)
	{
		FNearestNeighborGeomCacheSampler* GeomCacheSampler = static_cast<FNearestNeighborGeomCacheSampler*>(GetGeomCacheSampler());
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		if (GeomCacheSampler && NearestNeighborModel && PartId < NearestNeighborModel->GetNumParts())
		{
			TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = GeomCacheSampler->GetSkeletalMeshComponent();
			TObjectPtr<UGeometryCacheComponent> GeometryCacheComponent = GeomCacheSampler->GetGeometryCacheComponent();

			const TObjectPtr<UAnimSequence> AnimSequence = NearestNeighborModel->GetNearestNeighborSkeletons(PartId);
			const TObjectPtr<UGeometryCache> GeometryCache = NearestNeighborModel->GetNearestNeighborCache(PartId);

			if (SkeletalMeshComponent && GeometryCacheComponent && AnimSequence && GeometryCache)
			{
				const int32 NumNeighborsFromGeomCache = NearestNeighborModel->GetNumNeighborsFromGeometryCache(PartId);
				const int32 NumNeighborsFromAnimSequence = NearestNeighborModel->GetNumNeighborsFromAnimSequence(PartId);
				const int32 NumFrames = FMath::Min(NumNeighborsFromGeomCache, NumNeighborsFromAnimSequence);
				if (NumFrames > 0)
				{
					AnimSequence->Interpolation = EAnimInterpolationType::Step;

					SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					SkeletalMeshComponent->SetAnimation(AnimSequence);
					SkeletalMeshComponent->SetPosition(0.0f);
					SkeletalMeshComponent->SetPlayRate(1.0f);
					SkeletalMeshComponent->Play(false);
					SkeletalMeshComponent->RefreshBoneTransforms();
					if (SkeletalMeshComponent->GetAnimInstance())
					{
						SkeletalMeshComponent->GetAnimInstance()->GetRequiredBones().SetUseRAWData(true);
					}

					// assuming MeshMappings do not change
					GeometryCacheComponent->SetGeometryCache(GeometryCache);
					GeometryCacheComponent->ResetAnimationTime();
					GeometryCacheComponent->SetLooping(false);
					GeometryCacheComponent->SetManualTick(true);
					GeometryCacheComponent->SetPlaybackSpeed(1.0f);
					GeometryCacheComponent->Play();
					if (uint8 ReturnCode = GeomCacheSampler->GenerateMeshMappings(); HasError(ReturnCode))
					{
						return ReturnCode;
					}

					NumTrainingFramesOverride = NumFrames;

					if (NumNeighborsFromGeomCache == NumNeighborsFromAnimSequence)
					{
						return EUpdateResult::SUCCESS;
					}
					else
					{
						UE_LOG(LogNearestNeighborModel, Warning, TEXT("NearestNeighborData: part %d frame mismatch: AnimSequence has %d frames and GeometryCache has %d frames. Using %d frames only."), PartId, NumNeighborsFromAnimSequence, NumNeighborsFromGeomCache, NumFrames);
						return EUpdateResult::WARNING;
					}
				}
				else
				{
					UE_LOG(LogNearestNeighborModel, Warning, TEXT("Part %d: AnimSequence or GeometryCache has zero frames"), PartId);
					return EUpdateResult::WARNING;
				}
			}
			else if (AnimSequence == nullptr && GeometryCache == nullptr)
			{
				return EUpdateResult::SUCCESS;
			}
			else
			{
				UE_LOG(LogNearestNeighborModel, Warning, TEXT("Part %d is skipped because AnimSequence or GeometryCache is None"), PartId);
				return EUpdateResult::WARNING;
			}
		}
		UE_LOG(LogNearestNeighborModel, Error, TEXT("SetSamplerPartData: unknown error"));
		return EUpdateResult::ERROR;
	}

	int32 FNearestNeighborEditorModel::GetNumParts()
	{
		UNearestNeighborModel *NearestNeighborModel = GetNearestNeighborModel();
		if (NearestNeighborModel != nullptr)
		{
			return NearestNeighborModel->GetNumParts();
		}
		else
		{
			return 0;
		}
	}

	void FNearestNeighborEditorModel::ResetSamplerData()
	{
		FNearestNeighborGeomCacheSampler* GeomCacheSampler = static_cast<FNearestNeighborGeomCacheSampler*>(GetGeomCacheSampler());
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		if (GeomCacheSampler && NearestNeighborModel)
		{
			TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = GeomCacheSampler->GetSkeletalMeshComponent();
			const TObjectPtr<UAnimSequence> AnimSequence = NearestNeighborModel->GetAnimSequence();

			if (SkeletalMeshComponent && AnimSequence)
			{
				SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
				SkeletalMeshComponent->SetAnimation(AnimSequence);
				SkeletalMeshComponent->SetPosition(0.0f);
				SkeletalMeshComponent->SetPlayRate(1.0f);
				SkeletalMeshComponent->Play(false);
				SkeletalMeshComponent->RefreshBoneTransforms();
			}
			NumTrainingFramesOverride = -1;
			GeomCacheSampler->RegisterTargetComponents();
		}
	}

	void FNearestNeighborEditorModel::UpdateNearestNeighborActors()
	{
		if (EditorWorld != nullptr)
		{
			const UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
			const int32 TargetNumActors = NearestNeighborModel->GetNumParts();
			if (NearestNeighborActors.Num() > TargetNumActors)
			{
				for (int32 i = NearestNeighborActors.Num() - 1; i >= TargetNumActors; i--)
				{
					FNearestNeighborEditorModelActor* EditorActor = NearestNeighborActors[i];
					NearestNeighborActors.RemoveAt(i);
					EditorActors.Remove(EditorActor);
					EditorWorld->RemoveActor(EditorActor->GetActor(), true/*ShouldModifyLevel*/);
					delete EditorActor;
				}
			}
			if (NearestNeighborActors.Num() < TargetNumActors)
			{
				CreateNearestNeighborActors(EditorWorld, NearestNeighborActors.Num());
			}

			for (int32 PartId = 0; PartId < NearestNeighborActors.Num(); PartId++)
			{
				const TObjectPtr<UGeometryCache> GeometryCache = GetNearestNeighborModel()->GetNearestNeighborCache(PartId);
				NearestNeighborActors[PartId]->GetGeometryCacheComponent()->SetGeometryCache(GeometryCache);
			}
		}
	}

	uint8 FNearestNeighborEditorModel::UpdateNearestNeighborData()
	{
		UNearestNeighborModel *NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		check(NearestNeighborModel != nullptr);

		if (NearestNeighborModel->GetNumParts() == 0)
		{
			return EUpdateResult::SUCCESS;
		}

		FNearestNeighborGeomCacheSampler* GeomCacheSampler = static_cast<FNearestNeighborGeomCacheSampler*>(GetGeomCacheSampler());
		if (GeomCacheSampler)
		{
			TObjectPtr<USkeletalMeshComponent> SkeletalMeshComponent = GeomCacheSampler->GetSkeletalMeshComponent();
			TObjectPtr<UGeometryCacheComponent> GeometryCacheComponent = GeomCacheSampler->GetGeometryCacheComponent();

			if(SkeletalMeshComponent && GeometryCacheComponent)
			{
				UNearestNeighborTrainingModel *TrainingModel = InitTrainingModel<UNearestNeighborTrainingModel>(this);
				check(TrainingModel != nullptr);
				uint8 ReturnCode = TrainingModel->UpdateNearestNeighborData();
				ResetSamplerData();
				if (HasError(ReturnCode) == 0)
				{
					NearestNeighborModel->ValidateNearestNeighborData();
				}
				return ReturnCode;
			}
		}
		UE_LOG(LogNearestNeighborModel, Warning, TEXT("GeomCacheSampler is empty. Nearest neighbor data is not updated."))
		return EUpdateResult::ERROR;
	}

	template<typename T>
	T* CreateObjectInstance(const FString& PackageName)
	{
		// Parent package to place new mesh
		UPackage* Package = nullptr;
		FString NewPackageName = PackageName;
		const FString ObjectName = FPaths::GetBaseFilename(PackageName);

		// Setup package name and create one accordingly
		NewPackageName = UPackageTools::SanitizePackageName(NewPackageName);
		Package = CreatePackage(*NewPackageName);

		const FString SanitizedObjectName = ObjectTools::SanitizeObjectName(ObjectName);

		T* ExistingTypedObject = FindObject<T>(Package, *SanitizedObjectName);
		UObject* ExistingObject = FindObject<UObject>(Package, *SanitizedObjectName);

		if (ExistingTypedObject != nullptr)
		{
			ExistingTypedObject->PreEditChange(nullptr);
		}
		else if (ExistingObject != nullptr)
		{
			// Replacing an object.  Here we go!
			// Delete the existing object
			const bool bDeleteSucceeded = ObjectTools::DeleteSingleObject(ExistingObject);

			if (bDeleteSucceeded)
			{
				// Force GC so we can cleanly create a new asset (and not do an 'in place' replacement)
				CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

				// Create a package for each mesh
				Package = CreatePackage(*NewPackageName);
			}
			else
			{
				// failed to delete
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Failed to delete existing object %s"), *SanitizedObjectName);
				return nullptr;
			}
		}

		if (Package == nullptr)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Failed to create package %s"), *NewPackageName);
			return nullptr;
		}

		return NewObject<T>(Package, FName(*SanitizedObjectName),  RF_Public | RF_Standalone);
	}

	void FNearestNeighborEditorModel::KMeansClusterPoses()
	{
		KMeansClusterResult = EUpdateResult::SUCCESS;
		UNearestNeighborModel* NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		check(NearestNeighborModel != nullptr);

		if (NearestNeighborModel->KMeansPartId >= NearestNeighborModel->GetNumParts())
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("KMeansPartId %d is out of range [0, %d). Nothing will be done."), NearestNeighborModel->KMeansPartId, NearestNeighborModel->GetNumParts());
			KMeansClusterResult |= EUpdateResult::ERROR;
			return;
		}
		if (NearestNeighborModel->SourceAnims.Num() == 0)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("No source anims found."));
			KMeansClusterResult |= EUpdateResult::ERROR;
			return;
		}
		for (int32 i = 0; i < NearestNeighborModel->SourceAnims.Num(); i++)
		{
			if (NearestNeighborModel->SourceAnims[i] == nullptr)
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("Source anim %d is null."), i);
				KMeansClusterResult |= EUpdateResult::ERROR;
				return;
			}
		}

		UNearestNeighborTrainingModel *TrainingModel = InitTrainingModel<UNearestNeighborTrainingModel>(this);
		check(TrainingModel != nullptr);
		KMeansClusterResult |= TrainingModel->KmeansClusterPoses(NearestNeighborModel->KMeansPartId);
		if (HasError(KMeansClusterResult))
		{
			return;
		}
		ResetSamplerData();
		TArray<int32> KmeansResults = TrainingModel->KmeansResults;

		const FString PackageName = GetTestMLDeformerComponent()->GetDeformerAsset()->GetPackage()->GetName();
		const FString DirName = FPackageName::GetLongPackagePath(PackageName);
		const FString FileName = FPaths::GetBaseFilename(PackageName);
		const FString SavePath = FString::Printf(TEXT("%s/%s_PartId_%d"), *DirName, *FileName, NearestNeighborModel->KMeansPartId);

		TPair<UAnimSequence*, uint8> AnimAndFlag = CreateAnimOfClusterCenters(SavePath, KmeansResults);
		UAnimSequence* Anim = AnimAndFlag.Get<0>();
		KMeansClusterResult |= AnimAndFlag.Get<1>();
		if (HasError(KMeansClusterResult))
		{
			return;
		}

		UPackage* Package = Anim->GetPackage();
		const bool bSaveSucced = UPackage::SavePackage(Package, Anim, *SavePath, FSavePackageArgs());

		KmeansResults.Reset();
	}

	template<typename T>
	TArray<T> Range(T End)
	{
		TArray<T> Result;
		Result.SetNum(End);
		for (uint32 i = 0; i < End; i++)
		{
			Result[i] = i;
		}
		return Result;
	}

	class FAnimEvaluator
	{
	public:
		FAnimEvaluator(USkeleton* Skeleton)
		{
			const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
			const int32 NumBones = ReferenceSkeleton.GetNum();
			TArray<uint16> BoneIndices = Range<uint16>(NumBones);
			BoneContainer.SetUseRAWData(true);
			BoneContainer.InitializeTo(BoneIndices, UE::Anim::FCurveFilterSettings(), *Skeleton);
			OutPose.SetBoneContainer(&BoneContainer);
			OutCurve.InitFrom(BoneContainer);
		}

		TArray<FTransform> GetBoneTransforms(const UAnimSequence* Anim, int32 Frame)
		{
			const double Time = FMath::Clamp(Anim->GetSamplingFrameRate().AsSeconds(Frame), 0., (double)Anim->GetPlayLength());
			FAnimExtractContext ExtractionContext(Time);
			FAnimationPoseData AnimationPoseData(OutPose, OutCurve, TempAttributes);
			Anim->GetAnimationPose(AnimationPoseData, ExtractionContext);

			const int32 NumBones = BoneContainer.GetNumBones();
			TArray<FTransform> Transforms;
			Transforms.SetNum(NumBones);
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				const FCompactPoseBoneIndex CompactIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIndex));
				const FTransform BoneTransform = OutPose[CompactIndex];
				Transforms[BoneIndex] = BoneTransform;
			}
			return Transforms;
		}

	private:
		FBoneContainer BoneContainer;
		FCompactPose OutPose;
		FBlendedCurve OutCurve;
		UE::Anim::FStackAttributeContainer TempAttributes;
	};

	TPair<UAnimSequence*, uint8> FNearestNeighborEditorModel::CreateAnimOfClusterCenters(const FString& PackageName, const TArray<int32>& KmeansResults)
	{
		uint8 ReturnCode = EUpdateResult::SUCCESS;
		UNearestNeighborModel *NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);
		TTuple<UAnimSequence*, uint8> None = TTuple<UAnimSequence*, uint8>(nullptr, EUpdateResult::ERROR);

		if (KmeansResults.Num() != NearestNeighborModel->NumClusters * 2)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("KmeansClusterPoses returned %d clusters whereas %d are expected."), KmeansResults.Num() / 2, NearestNeighborModel->NumClusters);
			return None;
		}
		if (NearestNeighborModel->SourceAnims.Num() == 0)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("No source anims found."));
			return None;
		}
		const UAnimSequence* DefaultAnim = NearestNeighborModel->SourceAnims[0];
		if (DefaultAnim == nullptr)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Source anim 0 is null."));
			return None;
		}

		UAnimSequence* Anim = CreateObjectInstance<UAnimSequence>(PackageName);
		if (Anim == nullptr)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Failed to create AnimSequence."));
			return None;
		}

		USkeleton* const Skeleton = DefaultAnim->GetSkeleton();
		if (Skeleton == nullptr)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Skeleton is null."));
			return None;
		}
		const FReferenceSkeleton& ReferenceSkeleton = Skeleton->GetReferenceSkeleton();
		const int32 NumBones = ReferenceSkeleton.GetNum();

		Anim->SetSkeleton(Skeleton);
		IAnimationDataController& Controller = Anim->GetController();
		Controller.OpenBracket(LOCTEXT("CreateNewAnim_Bracket", "Create New Anim"));
		Controller.InitializeModel();
		Anim->ResetAnimation();
		Anim->SetPreviewMesh(NearestNeighborModel->GetSkeletalMesh());
		const IAnimationDataModel* AnimData = Anim->GetDataModel();
		const int32 NumKeys = NearestNeighborModel->NumClusters;
		Controller.SetNumberOfFrames(NumKeys - 1);
		Controller.SetFrameRate(FFrameRate(30, 1));

		FMemMark Mark(FMemStack::Get());
		FAnimEvaluator AnimEval(Skeleton);

		TArray<TArray<FVector3f>> PosKeys;
		TArray<TArray<FQuat4f>> RotKeys;
		TArray<TArray<FVector3f>> ScaleKeys;
		PosKeys.SetNum(NumBones);
		RotKeys.SetNum(NumBones);
		ScaleKeys.SetNum(NumBones);
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			PosKeys[BoneIndex].SetNum(NumKeys);
			RotKeys[BoneIndex].SetNum(NumKeys);
			ScaleKeys[BoneIndex].SetNum(NumKeys);
		}

		for (int32 KeyIndex = 0; KeyIndex < NumKeys; ++KeyIndex)
		{
			const int32 PickedAnimId = KmeansResults[KeyIndex * 2];
			const int32 PickedFrame = KmeansResults[KeyIndex * 2 + 1];
			if (PickedAnimId < 0 || PickedAnimId >= NearestNeighborModel->SourceAnims.Num())
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("CreateAnimOfClusterCenters: PickedAnimId %d is out of range."), PickedAnimId);
				return None;
			}
			
			const UAnimSequence* PickedAnim = NearestNeighborModel->SourceAnims[PickedAnimId];
			if (PickedAnim == nullptr)
			{
				UE_LOG(LogNearestNeighborModel, Error, TEXT("CreateAnimOfClusterCenters: PickedAnim %d is null."), PickedAnimId);
				return None;
			}

			const TArray<FTransform> BoneTransforms = AnimEval.GetBoneTransforms(PickedAnim, PickedFrame);
			for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
			{
				const FTransform& BoneTransform = BoneTransforms[BoneIndex];
				PosKeys[BoneIndex][KeyIndex] = FVector3f(BoneTransform.GetLocation());
				RotKeys[BoneIndex][KeyIndex] = FQuat4f(BoneTransform.GetRotation());
				ScaleKeys[BoneIndex][KeyIndex] = FVector3f(BoneTransform.GetScale3D());
			}
		}
		for (int32 BoneIndex = 0; BoneIndex < NumBones; ++BoneIndex)
		{
			const FName BoneName = ReferenceSkeleton.GetBoneName(BoneIndex);
			Controller.AddBoneCurve(BoneName);
			Controller.SetBoneTrackKeys(BoneName, PosKeys[BoneIndex], RotKeys[BoneIndex], ScaleKeys[BoneIndex]);
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket();
		return MakeTuple(Anim, ReturnCode);
	}

	void FNearestNeighborEditorModel::AddFloatArrayToDeltaArray(const TArray<float>& FloatArr, const TArray<uint32>& VertexMap, TArray<FVector3f>& DeltaArr, int32 DeltaArrayOffset, float ScaleFactor)
	{
		const int32 NumBaseMeshVerts = Model->GetNumBaseMeshVerts();
		const UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();
		const int32 PartNumVerts = VertexMap.Num();
		if (PartNumVerts == 0)
		{
			return;
		}
		const int32 NumShapes = FloatArr.Num() / (PartNumVerts * 3);
		if (DeltaArrayOffset < 0)
		{
			DeltaArrayOffset = DeltaArr.Num();
		}
		DeltaArr.SetNumZeroed(FMath::Max(DeltaArrayOffset + NumShapes * NumBaseMeshVerts, DeltaArr.Num()), false);

		for(int32 ShapeId = 0; ShapeId < NumShapes; ShapeId++)
		{
			for(int32 VertexId = 0; VertexId < PartNumVerts; VertexId++)
			{
				const int32 DeltaId = ShapeId * NumBaseMeshVerts + VertexMap[VertexId];
				const int32 FloatId = (ShapeId * PartNumVerts + VertexId) * 3;
				DeltaArr[DeltaArrayOffset + DeltaId] = FVector3f(FloatArr[FloatId], FloatArr[FloatId + 1], FloatArr[FloatId + 2]) * ScaleFactor;
			}
		}
	}
	
	void FNearestNeighborEditorModel::OnMorphTargetUpdate()
	{
		MorphTargetUpdateResult = EUpdateResult::SUCCESS;

		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();

		if (!NearestNeighborModel->IsClothPartDataValid())
		{
			MorphTargetUpdateResult |= NearestNeighborModel->UpdateClothPartData();
			if (HasError(MorphTargetUpdateResult))
			{
				UpdateNearestNeighborActors();
				return;
			}
		}
		MorphTargetUpdateResult |= WarnIfNetworkInvalid();

		if (!NearestNeighborModel->IsNearestNeighborDataValid())
		{
			MorphTargetUpdateResult |= UpdateNearestNeighborData();
			UpdateNearestNeighborActors();
			if (HasError(MorphTargetUpdateResult))
			{
				return;
			}
		}

		if (IsTrained())
		{
			MorphTargetUpdateResult |= InitMorphTargets();
			if (HasError(MorphTargetUpdateResult))
			{
				return;
			}
			RefreshMorphTargets();
			GetNearestNeighborModel()->UpdateNetworkSize();
			GetNearestNeighborModel()->UpdateMorphTargetSize();
		}

		InitTestMLDeformerPreviousWeights();
		GetNearestNeighborModel()->ValidateMorphTargetData();

		GetNearestNeighborModel()->UpdateInputMultipliers();
	}

	uint8 FNearestNeighborEditorModel::InitMorphTargets()
	{
		uint8 Result = EUpdateResult::SUCCESS;
		UNearestNeighborModel* NearestNeighborModel = GetNearestNeighborModel();

		const USkeletalMesh* SkelMesh = Model->GetSkeletalMesh();
		const int32 NumBaseMeshVerts = Model->GetNumBaseMeshVerts();
		if (!SkelMesh || NumBaseMeshVerts == 0)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("SkeletalMesh is empty. No morph targets are generated"));
			return EUpdateResult::ERROR;
		}

		if (Model->GetVertexMap().IsEmpty())
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("VertexMap of the skeletal mesh is empty. No morph targets are generated"));
			return EUpdateResult::ERROR;
		}

		const int32 NumParts = NearestNeighborModel->GetNumParts();
		if (NumParts == 0)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("There are no cloth parts. No morph targets are generated"));
			return EUpdateResult::ERROR;
		}

		const int32 NumImportedModelVerts = FMath::Max(NearestNeighborModel->GetVertexMap()) + 1;
		if (NumImportedModelVerts != NumBaseMeshVerts)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("Vertex count mismatch: imported model of SkeletalMesh has %d vertices and cached SkeletalMesh has %d vertices"), NumImportedModelVerts, NumBaseMeshVerts);
			return EUpdateResult::ERROR;
		}

		TArray<FVector3f> Deltas;
		Deltas.Reset();

		int32 NumPCACoeff = 0;
		for (int32 PartId = 0; PartId < NumParts; PartId++)
		{
			NumPCACoeff += NearestNeighborModel->GetPCACoeffNum(PartId);
		}
		Deltas.Reserve((1 + NumPCACoeff) * NumBaseMeshVerts);

		for (int32 PartId = 0; PartId < NumParts; PartId++)
		{
			const TArray<uint32>& VertexMap = NearestNeighborModel->PartVertexMap(PartId);
			if (VertexMap.IsEmpty())
			{
				UE_LOG(LogNearestNeighborModel, Warning, TEXT("Cloth part %d has empty vertex map. No morph targets are generated for this part."), PartId);
				Result = EUpdateResult::WARNING;
			}
			AddFloatArrayToDeltaArray(NearestNeighborModel->ClothPartData[PartId].VertexMean, VertexMap, Deltas, 0);
			AddFloatArrayToDeltaArray(NearestNeighborModel->ClothPartData[PartId].PCABasis, VertexMap, Deltas);
		}

		for (int32 PartId = 0; PartId < NumParts; PartId++)
		{
			const TArray<uint32>& VertexMap = NearestNeighborModel->PartVertexMap(PartId);
			AddFloatArrayToDeltaArray(NearestNeighborModel->ClothPartData[PartId].NeighborOffsets, VertexMap, Deltas);
		}

		if (Deltas.Num() == 0)
		{
			UE_LOG(LogNearestNeighborModel, Error, TEXT("All cloth parts are empty. No morph targets are generated."));
			return EUpdateResult::ERROR;
		}

		const int32 LOD = 0;
		TArray<UMorphTarget*> MorphTargets;
		CreateMorphTargets(
			MorphTargets, 
			Deltas, 
			FString("NNMorphTarget_"),
			LOD,
			NearestNeighborModel->GetMorphDeltaZeroThreshold(),
			NearestNeighborModel->GetIncludeMorphTargetNormals(),
			NearestNeighborModel->GetMaskChannel(),
			NearestNeighborModel->GetInvertMaskChannel());

		check(NearestNeighborModel->GetMorphTargetSet().IsValid());
		FMorphTargetVertexInfoBuffers& MorphBuffers = NearestNeighborModel->GetMorphTargetSet()->MorphBuffers;
		CompressMorphTargets(MorphBuffers, MorphTargets, LOD, NearestNeighborModel->GetMorphCompressionLevel());

		if (MorphBuffers.GetNumBatches() <= 0)
		{
			UE_LOG(LogNearestNeighborModel, Warning, TEXT("Morph buffer is empty. It is possible that all deltas are zero. No morph targets are generated."));
			Result = EUpdateResult::WARNING;
			NearestNeighborModel->ResetMorphBuffers();
		}

		// Remove the morph targets again, as we don't need them anymore.
		for (UMorphTarget* MorphTarget : MorphTargets)
		{
			MorphTarget->ConditionalBeginDestroy();
		}

		NearestNeighborModel->SetMorphTargetDeltas(Deltas);
		return Result;
	}

	void FNearestNeighborEditorModel::RefreshMorphTargets()
	{
		USkeletalMeshComponent* SkelMeshComponent = FindEditorActor(ActorID_Test_MLDeformed)->GetSkeletalMeshComponent() ;
		if (SkelMeshComponent)
		{
			check(GetNearestNeighborModel()->GetMorphTargetSet().IsValid());
			FMorphTargetVertexInfoBuffers& MorphBuffers = GetNearestNeighborModel()->GetMorphTargetSet()->MorphBuffers;
			BeginReleaseResource(&MorphBuffers);
			if (MorphBuffers.IsMorphCPUDataValid() && MorphBuffers.GetNumMorphs() > 0 && MorphBuffers.GetNumBatches() > 0)
			{
				BeginInitResource(&MorphBuffers);
			}
			SkelMeshComponent->RefreshExternalMorphTargetWeights();
		}
	}

	UMLDeformerComponent* FNearestNeighborEditorModel::GetTestMLDeformerComponent() const
	{
		return FindMLDeformerComponent(ActorID_Test_MLDeformed);
	}


	void FNearestNeighborEditorModel::InitTestMLDeformerPreviousWeights() 
	{
		UNearestNeighborModelInstance* ModelInstance = static_cast<UNearestNeighborModelInstance*>(GetTestMLDeformerModelInstance());
		if (ModelInstance)
		{
			ModelInstance->InitPreviousWeights();
		}
	}

	uint8 FNearestNeighborEditorModel::WarnIfNetworkInvalid()
	{
		const UNearestNeighborModel *NearestNeighborModel = static_cast<UNearestNeighborModel*>(Model);

		if (NearestNeighborModel->DoesUseOptimizedNetwork())
		{
			if (NearestNeighborModel->GetOptimizedNetwork() == nullptr)
			{
				UE_LOG(LogNearestNeighborModel, Warning, TEXT("Optimized network is not set. Model needs to be re-trained."));
				return EUpdateResult::WARNING;
			}

			const int32 NumNetworkWeights = NearestNeighborModel->GetOptimizedNetworkNumOutputs();
			const int32 NumPCACoeffs = NearestNeighborModel->GetTotalNumPCACoeffs();
			if (NumNetworkWeights != NumPCACoeffs)
			{
				UE_LOG(LogNearestNeighborModel, Warning, TEXT("Network output dimension %d is not equal to number of morph targets %d. Network needs to be re-trained and no deformation will be applied."), NumNetworkWeights, NumPCACoeffs);
				return EUpdateResult::WARNING;
			}
		}
		return EUpdateResult::SUCCESS;
	}


	UMLDeformerModelInstance* FNearestNeighborEditorModel::GetTestMLDeformerModelInstance() const
	{
		UMLDeformerModelInstance* ModelInstance = nullptr;
		const UMLDeformerComponent* MLDeformerComponent = FindMLDeformerComponent(ActorID_Test_MLDeformed);
		if (MLDeformerComponent)
		{
			ModelInstance = MLDeformerComponent->GetModelInstance();
		}
		return ModelInstance;
	}
}	// namespace UE::NearestNeighborModel

#undef LOCTEXT_NAMESPACE
