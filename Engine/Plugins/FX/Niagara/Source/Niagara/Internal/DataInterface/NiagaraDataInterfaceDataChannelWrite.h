// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

/**

Data Channel Write Interface.
Enables Niagara Systems to push data into a NiagaraDataChannel for access later by another System or Game code/BP.

We write into an intermediate buffer on the DI which is then published to the Data Channel on post tick.
This is simple and avoids race conditions and synchronization headaches but does introduce additional copying work that in many cases may be avoided.
In the future we may allow for direct writes into the data channel buffers and/or avoiding separate writes entirely by publishing the owning emitter particle buffers.
Though these other options have their own downsides.

Accessor functions on the Data Channel Read and Write DIs can have any number of parameters, allowing a single function call to access arbitrary data from the Channel.
This avoids cumbersome work in the graph to access data but requires special handling inside the DI.

*/


#include "NiagaraDataInterfaceDataChannelCommon.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceDataChannelWrite.generated.h"

/** Additional compile time information used by the Write DI. */
USTRUCT()
struct FNDIDataChannelWriteCompiledData : public FNDIDataChannelCompiledData
{
	GENERATED_BODY()

	/** Internal buffer layout. Contains only the data actually written by this DI. */
	UPROPERTY()
	FNiagaraDataSetCompiledData DataLayout;

	bool Init(UNiagaraSystem* System, UNiagaraDataInterfaceDataChannelWrite* OwnerDI);
};

UCLASS(Experimental, EditInlineNew, Category = "Data Channels", CollapseCategories, meta = (DisplayName = "Data Channel Writer"), MinimalAPI)
class UNiagaraDataInterfaceDataChannelWrite : public UNiagaraDataInterface
{
	GENERATED_UCLASS_BODY()

public:

	/** How should we allocate the buffer into which we write data. */
	UPROPERTY(EditAnywhere, Category = "Data Channel")
	ENiagaraDataChannelAllocationMode AllocationMode = ENiagaraDataChannelAllocationMode::Static;

	/** How many elements to allocate for writing per frame? Usage is defendant on AllocationMode. TODO: Allow allocation count to be controlled dynamically from script? */
	UPROPERTY(EditAnywhere, Category = "Data Channel")
	uint32 AllocationCount = 0;

	/** Whether the data generated by this DI should be published to the world game data channel. This is require to allow game BP and C++ to read this data. */
	UPROPERTY(EditAnywhere, Category = "Data Channel")
	bool bPublishToGame = false;

	/** Whether the data generated by this DI should be published to the world CPUSim data channel. This is required for CPU emitters in other Niagara Systems to read this data. */
	UPROPERTY(EditAnywhere, Category = "Data Channel")
	bool bPublishToCPU = false;

	/** Whether the data generated by this DI should be published to a world data channel. This is required to allow GPU emitters in other Niagara Systems to read this data.  */
	UPROPERTY(EditAnywhere, Category = "Data Channel")
	bool bPublishToGPU = false;
	
	/**
	Whether this DI should request updated destination data from the Data Channel each tick.
	Some Data Channels have multiple separate data elements for things such as spacial subdivision. 
	Each DI will request the correct one for it's owning system instance from the data channel. 
	Depending on the data channel this could be an expensive search so we should avoid doing this every tick if possible.
	*/
	UPROPERTY(EditAnywhere, Category = "Data Channel", AdvancedDisplay)
	bool bUpdateDestinationDataEveryTick = true;

	/** When writing externally, the channel to use. */
	UPROPERTY(EditAnywhere, Category = "Data Channel", meta = (EditCondition = "bPublishToWorld"))
	TObjectPtr<UNiagaraDataChannelAsset> Channel;

	//UObject Interface
	NIAGARA_API virtual void PostInitProperties() override;
	//UObject Interface End

	//UNiagaraDataInterface Interface
	NIAGARA_API virtual void GetFunctions(TArray<FNiagaraFunctionSignature>& OutFunctions) override;
	NIAGARA_API virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
	virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override { return true; }
#if WITH_EDITORONLY_DATA
	NIAGARA_API virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
	NIAGARA_API virtual void GetCommonHLSL(FString& OutHLSL)override;
	NIAGARA_API virtual bool GetFunctionHLSL(FNiagaraDataInterfaceHlslGenerationContext& HlslGenContext, FString& OutHLSL) override;
	NIAGARA_API virtual void GetParameterDefinitionHLSL(FNiagaraDataInterfaceHlslGenerationContext& HlslGenContext, FString& OutHLSL) override;

	NIAGARA_API virtual void PostCompile()override;
#endif
	NIAGARA_API virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
	NIAGARA_API virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;

#if WITH_EDITOR
	NIAGARA_API virtual void GetFeedback(UNiagaraSystem* InAsset, UNiagaraComponent* InComponent, TArray<FNiagaraDataInterfaceError>& OutErrors, TArray<FNiagaraDataInterfaceFeedback>& OutWarnings, TArray<FNiagaraDataInterfaceFeedback>& OutInfo);
	NIAGARA_API virtual void ValidateFunction(const FNiagaraFunctionSignature& Function, TArray<FText>& OutValidationErrors);
#endif

	NIAGARA_API virtual bool Equals(const UNiagaraDataInterface* Other) const override;

	NIAGARA_API virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	NIAGARA_API virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
	NIAGARA_API virtual int32 PerInstanceDataSize() const override;
	virtual bool HasPreSimulateTick() const override { return true; }
	virtual bool HasPreStageTick(ENiagaraScriptUsage Usage) const override { return true; }
	virtual bool HasPostStageTick(ENiagaraScriptUsage Usage) const override { return true; }
	NIAGARA_API virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	NIAGARA_API virtual bool PerInstanceTickPostSimulate(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
	NIAGARA_API virtual void PreStageTick(FNDICpuPreStageContext& Context) override;
	NIAGARA_API virtual void PostStageTick(FNDICpuPostStageContext& Context) override;
	NIAGARA_API virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;

	//We cannot overlap frames as we must correctly sync up with the data channel manager on Begin/End frame etc.
	virtual bool PostSimulateCanOverlapFrames() const { return false; }
	//We cannot have post stage overlap tick groups so that the write DI can publish it's contents to the data channel at the correct time to allow same frame reads.
	virtual bool PostStageCanOverlapTickGroups() const { return false; }
	//UNiagaraDataInterface Interface

	NIAGARA_API void Num(FVectorVMExternalFunctionContext& Context);
	NIAGARA_API void Write(FVectorVMExternalFunctionContext& Context, int32 FuncIdx);
	NIAGARA_API void Append(FVectorVMExternalFunctionContext& Context, int32 FuncIdx);

	const FNDIDataChannelWriteCompiledData& GetCompiledData()const { return CompiledData; }

	bool ShouldPublish()const { return bPublishToGame || bPublishToCPU || bPublishToGPU; }

protected:
	NIAGARA_API virtual bool CopyToInternal(UNiagaraDataInterface* Destination) const override;

	UPROPERTY()
	FNDIDataChannelWriteCompiledData CompiledData;
};
