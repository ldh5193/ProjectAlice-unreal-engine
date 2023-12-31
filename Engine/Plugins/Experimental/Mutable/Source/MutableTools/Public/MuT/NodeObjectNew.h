// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "MuR/Ptr.h"
#include "MuR/RefCounted.h"
#include "MuR/System.h"
#include "MuT/Node.h"
#include "MuT/NodeObject.h"
#include "MuT/NodeExtensionData.h"


namespace mu
{
	class NodeLOD;

	//! Node that creates a new object by setting its levels-of-detail and its children.
	//! \ingroup model
	class MUTABLETOOLS_API NodeObjectNew : public NodeObject
	{
	public:

		//-----------------------------------------------------------------------------------------
		// Life cycle
		//-----------------------------------------------------------------------------------------

		NodeObjectNew();

		void SerialiseWrapper(OutputArchive& arch) const override;
		static void Serialise( const NodeObjectNew* pNode, OutputArchive& arch );
		static Ptr<NodeObjectNew> StaticUnserialise( InputArchive& arch );


		//-----------------------------------------------------------------------------------------
		// Node Interface
		//-----------------------------------------------------------------------------------------

        

        const NODE_TYPE* GetType() const override;
		static const NODE_TYPE* GetStaticType();

        int GetInputCount() const override;
        Node* GetInputNode( int i ) const override;
        void SetInputNode( int i, NodePtr pNode ) override;

		//-----------------------------------------------------------------------------------------
		// NodeObject Interface
		//-----------------------------------------------------------------------------------------
        const char* GetName() const override;
        void SetName( const char* strName ) override;
        const char* GetUid() const override;
        void SetUid( const char* strUid ) override;

		//-----------------------------------------------------------------------------------------
		// Own Interface
		//-----------------------------------------------------------------------------------------

		//! Get the number of levels of detail in the object.
		int GetLODCount() const;
		void SetLODCount( int );

		//! Get a level of detail node from the object.
		Ptr<NodeLOD> GetLOD( int index ) const;
		void SetLOD( int index, Ptr<NodeLOD>);

		//! Get the number of child objects
		int GetChildCount() const;
		void SetChildCount( int );

		//! Get a child object node
		NodeObjectPtr GetChild( int index ) const;
		void SetChild( int index, NodeObjectPtr );

		//! Set the number of states that the model can be in.
		int GetStateCount() const;
		void SetStateCount( int c );

		//! Set the name of a state
		const char* GetStateName( int s ) const;
		void SetStateName( int s, const char* n );

		//! State optimization options
		void SetStateGPUOptimisation( int s, bool internal, bool external, bool returnGPU = false );
		bool GetStateGPUOptimisationInternal( int s ) const;
		bool GetStateGPUOptimisationExternal( int s ) const;
		bool GetStateGPUOptimisationReturnImages( int s ) const;

		//! See if a state has a parameter as runtime.
		bool HasStateParam( int s, const char* param ) const;

		//! Add a runtime parameter to the state.
		void AddStateParam( int s, const char* param );

		//! Remove a runtime parameter from a state.
		void RemoveStateParam( int s, const char* param );

        //! Set the optimisation properties of a state
		void SetStateProperties(int32 StateIndex,
			ETextureCompressionStrategy TextureCompressionStrategy,
			bool bOnlyFirstLOD,
			uint8 FirstLOD,
			uint8 NumExtraLODsToBuildAfterFirstLOD);

		//! Connect a node that produces ExtensionData to be added to the final Instance, and provide a name to associate with the data
		void AddExtensionDataNode(NodeExtensionDataPtr Node, const char* Name);

		//-----------------------------------------------------------------------------------------
		// Interface pattern
		//-----------------------------------------------------------------------------------------
		class Private;
		Private* GetPrivate() const;
        Node::Private* GetBasePrivate() const override;

	protected:

		//! Forbidden. Manage with the Ptr<> template.
		~NodeObjectNew();

	private:

		Private* m_pD;

	};



}
