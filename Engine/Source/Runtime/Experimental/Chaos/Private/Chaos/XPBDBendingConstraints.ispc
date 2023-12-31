// Copyright Epic Games, Inc. All Rights Reserved.

#define EXPLICIT_VECTOR4 1

#include "Math/Vector.isph"
#include "Chaos/PBDSofts.isph"

static inline FVector3f SafeDivide(const FVector3f& Numerator, const float Denominator)
{
	return VectorSelect(Denominator <= FLOAT_SMALL_NUMBER, FloatZeroVector, Numerator / Denominator);
}

static inline varying float SafeDivide(const varying float Numerator, const varying float Denominator)
{
	return select(Denominator <= FLOAT_SMALL_NUMBER, (uniform float)0, Numerator / Denominator);
}

// Returns Angle
static inline float CalcGradientsAndAngle(const FVector3f& P1, const FVector3f& P2, const FVector3f& P3, const FVector3f& P4, FVector3f& Grad1, FVector3f& Grad2, FVector3f& Grad3, FVector3f& Grad4)
{
	const FVector3f SharedEdgeNormalized = VectorGetSafeNormal(P2 - P1);
	const FVector3f P13CrossP23 = VectorCross(P1 - P3, P2 - P3);
	const float Normal1Len = VectorSize(P13CrossP23);
	const FVector3f Normal1 = SafeDivide(P13CrossP23, Normal1Len);
	const FVector3f P24CrossP14 = VectorCross(P2 - P4, P1 - P4);
	const float Normal2Len = VectorSize(P24CrossP14);
	const FVector3f Normal2 = SafeDivide(P24CrossP14, Normal2Len);

	const FVector3f N2CrossN1 = VectorCross(Normal2, Normal1);

	const float CosPhi = clamp(VectorDot(Normal1, Normal2), (uniform float)(-1), (uniform float)(1));
	const float SinPhi = clamp(VectorDot(N2CrossN1, SharedEdgeNormalized), (uniform float)(-1), (uniform float)(1));

	const FVector3f DPhiDN1_OverNormal1Len = SafeDivide(CosPhi * VectorCross(SharedEdgeNormalized, Normal2) - SinPhi * Normal2, Normal1Len);
	const FVector3f DPhiDN2_OverNormal2Len = SafeDivide(CosPhi * VectorCross(Normal1, SharedEdgeNormalized) - SinPhi * Normal1, Normal2Len);

	const FVector3f DPhiDP13 = VectorCross(P2 - P3, DPhiDN1_OverNormal1Len);
	const FVector3f DPhiDP23 = VectorCross(DPhiDN1_OverNormal1Len, P1 - P3);
	const FVector3f DPhiDP24 = VectorCross(P1 - P4, DPhiDN2_OverNormal2Len);
	const FVector3f DPhiDP14 = VectorCross(DPhiDN2_OverNormal2Len, P2 - P4);

	Grad1 = DPhiDP13 + DPhiDP14;
	Grad2 = DPhiDP23 + DPhiDP24;
	Grad3 = -1 * DPhiDP13 - DPhiDP23;
	Grad4 = -1 * DPhiDP14 - DPhiDP24;

	return atan2(SinPhi, CosPhi);
}


export void ApplyXPBDBendingConstraints(uniform FVector4f PandInvM[],
									const uniform FIntVector4 Constraints[],
									const uniform float RestAngles[],
									const uniform bool IsBuckled[],
									uniform float Lambdas[],
									const uniform float Dt,
									const uniform float MinStiffness,
									const uniform float Stiffness,
									const uniform float BucklingStiffness,
									const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector4 Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];
		const varying int32 i4 = Constraint.V[3];
		
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);
		const varying FVector4f PandInvM3 = VectorGather(&PandInvM[i3]);
		const varying FVector4f PandInvM4 = VectorGather(&PandInvM[i4]);

		varying FVector3f P1, P2, P3, P4;
		varying float M1, M2, M3, M4;

		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);
		UnzipPandInvM(PandInvM3, P3, M3);
		UnzipPandInvM(PandInvM4, P4, M4);

		varying FVector3f Grad1, Grad2, Grad3, Grad4;
		const varying float Angle = CalcGradientsAndAngle(P1, P2, P3, P4, Grad1, Grad2, Grad3, Grad4);
		
		const varying float RestAngle = RestAngles[i];
		const varying float StiffnessValue = select(IsBuckled[i], BucklingStiffness, Stiffness);
		
		const varying float Lambda = Lambdas[i];
		const varying float Alpha  = 1.f / (StiffnessValue * Dt * Dt);
		const varying float Denom = M1 * VectorSizeSquared(Grad1) + M2 * VectorSizeSquared(Grad2) + M3 * VectorSizeSquared(Grad3) + M4 * VectorSizeSquared(Grad4) + Alpha;

		const varying float ComputedDLambda = (Angle - RestAngle - Alpha * Lambda) / Denom;
		const varying float DLambda = select(StiffnessValue < MinStiffness, (uniform float)0, ComputedDLambda);
		Lambdas[i] = Lambda + DLambda;

		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - DLambda * M1 * Grad1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - DLambda * M2 * Grad2, M2));
		}
		if (M3 > 0)
		{
			VectorScatter(&PandInvM[i3], SetVector4(P3 - DLambda * M3 * Grad3, M3));
		}
		if (M4 > 0)
		{
			VectorScatter(&PandInvM[i4], SetVector4(P4 - DLambda * M4 * Grad4, M4));
		}
	}
}

export void ApplyXPBDBendingConstraintsWithDamping(uniform FVector4f PandInvM[],
									const uniform FVector3f X[],
									const uniform FIntVector4 Constraints[],
									const uniform float RestAngles[],
									const uniform bool IsBuckled[],
									uniform float Lambdas[],
									const uniform float Dt,
									const uniform float MinStiffness,
									const uniform float Stiffness,
									const uniform float BucklingStiffness,
									const uniform float DampingRatio,
									const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector4 Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];
		const varying int32 i4 = Constraint.V[3];
		
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);
		const varying FVector4f PandInvM3 = VectorGather(&PandInvM[i3]);
		const varying FVector4f PandInvM4 = VectorGather(&PandInvM[i4]);

		varying FVector3f P1, P2, P3, P4;
		varying float M1, M2, M3, M4;

		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);
		UnzipPandInvM(PandInvM3, P3, M3);
		UnzipPandInvM(PandInvM4, P4, M4);

		varying FVector3f Grad1, Grad2, Grad3, Grad4;
		const varying float Angle = CalcGradientsAndAngle(P1, P2, P3, P4, Grad1, Grad2, Grad3, Grad4);
		
		const varying float RestAngle = RestAngles[i];
		const varying float StiffnessValue = select(IsBuckled[i], BucklingStiffness, Stiffness);

		const varying FVector3f X1 = VectorGather(&X[i1]);
		const varying FVector3f X2 = VectorGather(&X[i2]);
		const varying FVector3f X3 = VectorGather(&X[i3]);
		const varying FVector3f X4 = VectorGather(&X[i4]);

		const varying float CombinedInvMass = M1 + M2 + M3 + M4;
		const varying float Damping = 2.f * DampingRatio * sqrt(StiffnessValue / CombinedInvMass);

		const varying float Lambda = Lambdas[i];
		const varying float Alpha  = 1.f / (StiffnessValue * Dt * Dt);
		const varying float Gamma = Alpha * Damping * Dt;

		const varying FVector3f V1TimesDt = P1 - X1;
		const varying FVector3f V2TimesDt = P2 - X2;
		const varying FVector3f V3TimesDt = P3 - X3;
		const varying FVector3f V4TimesDt = P4 - X4;
		const varying float DampingTerm = Gamma * (VectorDot(V1TimesDt, Grad1) + VectorDot(V2TimesDt, Grad2) + VectorDot(V3TimesDt, Grad3) + VectorDot(V4TimesDt, Grad4));

		const varying float Denom = (1.f + Gamma)*(M1 * VectorSizeSquared(Grad1) + M2 * VectorSizeSquared(Grad2) + M3 * VectorSizeSquared(Grad3) + M4 * VectorSizeSquared(Grad4)) + Alpha;

		const varying float ComputedDLambda = (Angle - RestAngle - Alpha * Lambda + DampingTerm) / Denom;
		const varying float DLambda = select(StiffnessValue < MinStiffness, (uniform float)0, ComputedDLambda);
		Lambdas[i] = Lambda + DLambda;

		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - DLambda * M1 * Grad1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - DLambda * M2 * Grad2, M2));
		}
		if (M3 > 0)
		{
			VectorScatter(&PandInvM[i3], SetVector4(P3 - DLambda * M3 * Grad3, M3));
		}
		if (M4 > 0)
		{
			VectorScatter(&PandInvM[i4], SetVector4(P4 - DLambda * M4 * Grad4, M4));
		}
	}
}

export void ApplyXPBDBendingConstraintsWithMaps(uniform FVector4f PandInvM[],
											const uniform FIntVector4 Constraints[],
											const uniform float RestAngles[],
											const uniform bool IsBuckled[],
											uniform float Lambdas[],
											const uniform float Dt,
											const uniform float MinStiffness,
											const uniform bool StiffnessHasMap,
											const uniform uint8 StiffnessIndices[],
											const uniform float StiffnessTable[],
											const uniform bool BucklingStiffnessHasMap,
											const uniform uint8 BucklingStiffnessIndices[],
											const uniform float BucklingStiffnessTable[],
											const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector4 Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];
		const varying int32 i4 = Constraint.V[3];
		const varying int8 StiffnessIndex = StiffnessHasMap ? StiffnessIndices[i] : 0;
		const varying int8 BucklingStiffnessIndex = BucklingStiffnessHasMap ? BucklingStiffnessIndices[i] : 0;

		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);
		const varying FVector4f PandInvM3 = VectorGather(&PandInvM[i3]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM4 = VectorGather(&PandInvM[i4]);

		varying FVector3f P1, P2, P3, P4;
		varying float M1, M2, M3, M4;

		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);
		UnzipPandInvM(PandInvM3, P3, M3);
		UnzipPandInvM(PandInvM4, P4, M4);


		varying FVector3f Grad1, Grad2, Grad3, Grad4;
		const varying float Angle = CalcGradientsAndAngle(P1, P2, P3, P4, Grad1, Grad2, Grad3, Grad4);
		
		#pragma ignore warning(perf)
		const varying float Stiffness = StiffnessTable[StiffnessIndex];
		#pragma ignore warning(perf)
		const varying float BucklingStiffness = BucklingStiffnessTable[BucklingStiffnessIndex];
		
		const varying float RestAngle = RestAngles[i];
		const varying float StiffnessValue = select(IsBuckled[i], BucklingStiffness, Stiffness);
		
		const varying float Lambda = Lambdas[i];
		const varying float Alpha  = 1.f / (StiffnessValue * Dt * Dt);
		const varying float Denom = M1 * VectorSizeSquared(Grad1) + M2 * VectorSizeSquared(Grad2) + M3 * VectorSizeSquared(Grad3) + M4 * VectorSizeSquared(Grad4) + Alpha;

		const varying float ComputedDLambda = (Angle - RestAngle - Alpha * Lambda) / Denom;
		const varying float DLambda = select( StiffnessValue < MinStiffness, (uniform float)0, ComputedDLambda);
		Lambdas[i] = Lambda + DLambda;

		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - DLambda * M1 * Grad1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - DLambda * M2 * Grad2, M2));
		}
		if (M3 > 0)
		{
			VectorScatter(&PandInvM[i3], SetVector4(P3 - DLambda * M3 * Grad3, M3));
		}
		if (M4 > 0)
		{
			VectorScatter(&PandInvM[i4], SetVector4(P4 - DLambda * M4 * Grad4, M4));
		}
	}
}

export void ApplyXPBDBendingConstraintsWithDampingAndMaps(uniform FVector4f PandInvM[],
											const uniform FVector3f X[],
											const uniform FIntVector4 Constraints[],
											const uniform float RestAngles[],
											const uniform bool IsBuckled[],
											uniform float Lambdas[],
											const uniform float Dt,
											const uniform float MinStiffness,
											const uniform bool StiffnessHasMap,
											const uniform uint8 StiffnessIndices[],
											const uniform float StiffnessTable[],
											const uniform bool BucklingStiffnessHasMap,
											const uniform uint8 BucklingStiffnessIndices[],
											const uniform float BucklingStiffnessTable[],
											const uniform bool DampingHasMap,
											const uniform uint8 DampingIndices[],
											const uniform float DampingTable[],
											const uniform int32 NumConstraints)
{

	foreach(i = 0 ... NumConstraints)
	{
		const varying FIntVector4 Constraint = VectorLoad(&Constraints[extract(i,0)]);

		const varying int32 i1 = Constraint.V[0];
		const varying int32 i2 = Constraint.V[1];
		const varying int32 i3 = Constraint.V[2];
		const varying int32 i4 = Constraint.V[3];
		const varying int8 StiffnessIndex = StiffnessHasMap ? StiffnessIndices[i] : 0;
		const varying int8 BucklingStiffnessIndex = BucklingStiffnessHasMap ? BucklingStiffnessIndices[i] : 0;
		const varying int8 DampingIndex = DampingHasMap ? DampingIndices[i] : 0;

		const varying FVector4f PandInvM2 = VectorGather(&PandInvM[i2]);
		const varying FVector4f PandInvM3 = VectorGather(&PandInvM[i3]);
		const varying FVector4f PandInvM1 = VectorGather(&PandInvM[i1]);
		const varying FVector4f PandInvM4 = VectorGather(&PandInvM[i4]);

		varying FVector3f P1, P2, P3, P4;
		varying float M1, M2, M3, M4;

		UnzipPandInvM(PandInvM1, P1, M1);
		UnzipPandInvM(PandInvM2, P2, M2);
		UnzipPandInvM(PandInvM3, P3, M3);
		UnzipPandInvM(PandInvM4, P4, M4);


		varying FVector3f Grad1, Grad2, Grad3, Grad4;
		const varying float Angle = CalcGradientsAndAngle(P1, P2, P3, P4, Grad1, Grad2, Grad3, Grad4);
		
		#pragma ignore warning(perf)
		const varying float Stiffness = StiffnessTable[StiffnessIndex];
		#pragma ignore warning(perf)
		const varying float BucklingStiffness = BucklingStiffnessTable[BucklingStiffnessIndex];		
		#pragma ignore warning(perf)
		const varying float DampingRatio = DampingTable[DampingIndex];
		
		const varying float RestAngle = RestAngles[i];
		const varying float StiffnessValue = select(IsBuckled[i], BucklingStiffness, Stiffness);

		const varying FVector3f X1 = VectorGather(&X[i1]);
		const varying FVector3f X2 = VectorGather(&X[i2]);
		const varying FVector3f X3 = VectorGather(&X[i3]);
		const varying FVector3f X4 = VectorGather(&X[i4]);

		const varying float CombinedInvMass = M1 + M2 + M3 + M4;
		const varying float Damping = 2.f * DampingRatio * sqrt(StiffnessValue / CombinedInvMass);
		
		const varying float Lambda = Lambdas[i];
		const varying float Alpha  = 1.f / (StiffnessValue * Dt * Dt);
		const varying float Gamma = Alpha * Damping * Dt;

		const varying FVector3f V1TimesDt = P1 - X1;
		const varying FVector3f V2TimesDt = P2 - X2;
		const varying FVector3f V3TimesDt = P3 - X3;
		const varying FVector3f V4TimesDt = P4 - X4;
		const varying float DampingTerm = Gamma * (VectorDot(V1TimesDt, Grad1) + VectorDot(V2TimesDt, Grad2) + VectorDot(V3TimesDt, Grad3) + VectorDot(V4TimesDt, Grad4));

		const varying float Denom = (1.f + Gamma)*(M1 * VectorSizeSquared(Grad1) + M2 * VectorSizeSquared(Grad2) + M3 * VectorSizeSquared(Grad3) + M4 * VectorSizeSquared(Grad4)) + Alpha;

		const varying float ComputedDLambda = (Angle - RestAngle - Alpha * Lambda + DampingTerm) / Denom;
		const varying float DLambda = select( StiffnessValue < MinStiffness, (uniform float)0, ComputedDLambda);
		Lambdas[i] = Lambda + DLambda;

		if (M1 > 0)
		{
			VectorScatter(&PandInvM[i1], SetVector4(P1 - DLambda * M1 * Grad1, M1));
		}
		if (M2 > 0)
		{
			VectorScatter(&PandInvM[i2], SetVector4(P2 - DLambda * M2 * Grad2, M2));
		}
		if (M3 > 0)
		{
			VectorScatter(&PandInvM[i3], SetVector4(P3 - DLambda * M3 * Grad3, M3));
		}
		if (M4 > 0)
		{
			VectorScatter(&PandInvM[i4], SetVector4(P4 - DLambda * M4 * Grad4, M4));
		}
	}
}